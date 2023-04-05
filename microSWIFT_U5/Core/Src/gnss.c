/*
 * gnss.c
 *
 *  Created on: Oct 28, 2022
 *      Author: Phil
 *
 *`TODO: Figure out how to set the RTC
 *`TODO: Update to pull from global_config
 *`TODO: Change arrays to be the same used by waves
 *`TODO: Update gnss_get_running_average_velocities for the no-samples case
 *`TODO: Update velocity rejection criteria
 *`TODO: test that setting the RTC in self-test works
 *`TODO: hunt down the other TODOs
 */



#include "gnss.h"

// Static helper functions
static gnss_error_code_t send_config(GNSS* self, uint8_t* config_array,
		size_t message_size);
static gnss_error_code_t stop_start_gnss(GNSS* self, bool send_stop);
static void internal_process_messages(GNSS* self, uint8_t* process_buf);
static void process_self_test_messages(GNSS* self, uint8_t* process_buf);
static void get_checksum(uint8_t* ck_a, uint8_t* ck_b, uint8_t* buffer,
		uint32_t num_bytes);

/**
 * Initialize the GNSS struct
 *
 * @return void
 */
void gnss_init(GNSS* self, microSWIFT_configuration* global_config,
		UART_HandleTypeDef* gnss_uart_handle, DMA_HandleTypeDef* gnss_dma_handle,
		TX_EVENT_FLAGS_GROUP* event_flags, TX_QUEUE* message_queue,
		RTC_HandleTypeDef* rtc_handle, float* GNSS_N_Array, float* GNSS_E_Array,
		float* GNSS_D_Array)
{
	// initialize everything
	self->global_config = global_config;
	self->gnss_uart_handle = gnss_uart_handle;
	self->gnss_dma_handle = gnss_dma_handle;
	self->rtc_handle = rtc_handle;
	self->GNSS_N_Array = GNSS_N_Array;
	self->GNSS_E_Array = GNSS_E_Array;
	self->GNSS_D_Array = GNSS_D_Array;
	self->messages_processed = 0;
	self->v_north_sum = 0;
	self->v_east_sum = 0;
	self->v_down_sum = 0;
	self->current_latitude = 0;
	self->current_longitude = 0;
	self->total_samples = 0;
	self->total_samples_averaged = 0;
	self->number_cycles_without_data = 0;
	self->is_configured = false;
	self->is_location_valid = false;
	self->is_velocity_valid = false;
	self->is_clock_set = false;
	self->event_flags = event_flags;
	self->message_queue = message_queue;
	self->config = gnss_config;
	self->self_test = gnss_self_test;
	self->get_location = gnss_get_location;
	self->get_running_average_velocities = gnss_get_running_average_velocities;
	self->gnss_process_message = gnss_process_message;
	self->sleep = gnss_sleep;
	self->on_off = gnss_on_off;
	self->set_rtc = gnss_set_rtc;
	self->reset_gnss_uart = reset_gnss_uart;
}

/**
 * Configure the MAX-M10S chip by sending a series of UBX_CFG_VALSET messages
 *
 * @return GNSS_SUCCESS or
 * 		   GNSS_CONFIG_ERROR if response was not received
 */
gnss_error_code_t gnss_config(GNSS* self){
	int fail_counter = 0;
	gnss_error_code_t return_code = GNSS_CONFIG_ERROR;
	// The configuration message, type UBX_CFG_VALSET
	// !!!! This is output from U-Center 2 software, do not change !!!
	uint8_t config[144] =
	{0xB5,0x62,0x06,0x8A,0x88,0x00,0x01,0x01,0x00,0x00,0xBA,0x00,0x91,0x20,0x00,
	 0xBE,0x00,0x91,0x20,0x00,0xBB,0x00,0x91,0x20,0x00,0xC9,0x00,0x91,0x20,0x00,
	 0xCD,0x00,0x91,0x20,0x00,0xCA,0x00,0x91,0x20,0x00,0xBF,0x00,0x91,0x20,0x00,
	 0xC3,0x00,0x91,0x20,0x00,0xC0,0x00,0x91,0x20,0x00,0xC4,0x00,0x91,0x20,0x00,
	 0xC8,0x00,0x91,0x20,0x00,0xC5,0x00,0x91,0x20,0x00,0xAB,0x00,0x91,0x20,0x00,
	 0xAF,0x00,0x91,0x20,0x00,0xAC,0x00,0x91,0x20,0x00,0xB0,0x00,0x91,0x20,0x00,
	 0xB4,0x00,0x91,0x20,0x00,0xB1,0x00,0x91,0x20,0x00,0x07,0x00,0x91,0x20,0x01,
	 0x21,0x00,0x11,0x20,0x08,0x04,0x00,0x93,0x10,0x00,0x01,0x00,0x21,0x30,0xC8,
	 0x00,0x02,0x00,0x21,0x30,0x01,0x00,0x07,0x00,0x92,0x20,0x00,0x06,0x00,0x92,
	 0x20,0x00,0x0A,0x00,0x92,0x20,0x00,0x22,0x04};

	while (fail_counter++ < 10) {

		// Send over the configuration settings for RAM
		if (send_config(self, &(config[0]), sizeof(config)) == GNSS_CONFIG_ERROR) {
			continue;
		}

		// Only one value (configuration layer) and the checksum change between RAM
		// and Battery-backed-RAM, so we'll adjust that now
		config[7] = 0x02;
		config[142] = 0x23;
		config[143] = 0x8B;
		// Send over the BBR config settings
		if (send_config(self, &(config[0]), sizeof(config)) == GNSS_SUCCESS) {
			return_code = GNSS_SUCCESS;
			reset_gnss_uart(self, GNSS_DEFAULT_BAUD_RATE);
			break;
		}

		HAL_Delay(100);
		reset_gnss_uart(self, GNSS_DEFAULT_BAUD_RATE);
	}

	return return_code;
}

/**
 * Called by app_threadx::startup_thread to ensure that good
 * messages are coming across.
 *
 * @return gnss_error_code_t
 */
gnss_error_code_t gnss_self_test(GNSS* self)
{
	gnss_error_code_t return_code = GNSS_SELF_TEST_FAILED;
	uint8_t fail_counter = 0;
	ULONG actual_flags;
	uint8_t msg_buf[SELF_TEST_BUFFER_SIZE];
	memset(&(msg_buf[0]),0,sizeof(msg_buf));

	self->on_off(self, GPIO_PIN_SET);

    // We'll try for a little over 2 mins to get good data before failing out
	while (fail_counter++ < 120) {
		// Grab 5 UBX_NAV_PVT messages
		reset_gnss_uart(self, GNSS_DEFAULT_BAUD_RATE);
		HAL_UART_Receive(self->gnss_uart_handle, &(msg_buf[0]),
				sizeof(msg_buf), ONE_SECOND * 2);

//		HAL_UART_Receive_DMA(self->gnss_uart_handle, &(msg_buf[0]),
//				sizeof(msg_buf));
//		__HAL_DMA_DISABLE_IT(self->gnss_dma_handle, DMA_IT_HT);

//		if (tx_event_flags_get(self->event_flags, GNSS_CONFIG_RECVD, TX_OR_CLEAR,
//				&actual_flags, MAX_THREADX_WAIT_TICKS_FOR_CONFIG) != TX_SUCCESS) {
//			// Insert a prime number delay to sync up UART reception
//			HAL_Delay(13);
//			continue;
//		}

		process_self_test_messages(self, msg_buf);

		// this both ensures we have good satellite reception and that our
		// DMA reception is sync'd up with the GNSS chip
		if (self->messages_processed == 5 &&
				self->number_cycles_without_data == 0 &&
				self->total_samples == 5)
		{
			return_code = GNSS_SUCCESS;
			self->set_rtc(self, msg_buf);
			break;
		} else {
			// Insert a short, prime number delay to get sync'd up
			HAL_Delay(13);
		}
	}

	// Just to be overly sure we're starting the sampling window from a fresh slate
	self->messages_processed = 0;
	self->v_north_sum = 0;
	self->v_east_sum = 0;
	self->v_down_sum = 0;
	self->current_latitude = 0;
	self->current_longitude = 0;
	self->total_samples = 0;
	self->total_samples_averaged = 0;
	self->number_cycles_without_data = 0;
	self->is_configured = false;
	self->is_location_valid = false;
	self->is_velocity_valid = false;
	self->is_clock_set = false;
	tx_queue_flush(self->message_queue);
	self->is_configured = true;
	return return_code;
}

/**
 * Process the messages in the buffer.
 *
 * @return gnss_error_code_t
 */
gnss_error_code_t gnss_process_message(GNSS* self)
{
	ULONG num_msgs_enqueued, available_space;
	uint8_t *message;
	uint8_t **msg_ptr = &message;
	gnss_error_code_t return_code = GNSS_SUCCESS;
	int total_samples = self->total_samples;

	do {
	    // Grab a message -- this will block until a message is on the queue
	    tx_queue_receive(self->message_queue, (VOID*)msg_ptr, TX_WAIT_FOREVER);

	    // Process the contents
	    internal_process_messages(self, message);
		// Make sure we didn't have too many errors processing the messages
		// TODO: figure out how useful this is
		if (self->messages_processed < 8 ||
				self->number_cycles_without_data > 1 ||
				self->total_samples  <= total_samples + 9) {

			return_code = GNSS_MESSAGE_PROCESS_ERROR;
		}
		// How many messages are on the queue?
		tx_queue_info_get(self->message_queue, TX_NULL, &num_msgs_enqueued,
							&available_space, TX_NULL, TX_NULL, TX_NULL);
	// If there are any more messages that need processed, we'll do it now
	} while (num_msgs_enqueued > 0);

    return return_code;
}

/**
 * Get the current lat/long. We're going to return the lat/long no matter what,
 * but the return code will indicate if it's any good
 *
 * @param latitude - return parameter for latitude
 * @param longitude - return parameter for longitude
 * @return gnss_error_code_t
 */
gnss_error_code_t gnss_get_location(GNSS* self, int32_t* latitude,
		int32_t* longitude)
{
	gnss_error_code_t return_code = GNSS_SUCCESS;
	if (!self->is_location_valid) {
		return_code = GNSS_LOCATION_INVALID;
	}

	*latitude = self->current_latitude;
	*longitude = self->current_longitude;

	return return_code;
}

/**
 * If a velocity field > MAX_POSSIBLE_VELOCITY, or the velocity accuracy
 * estimate (vAcc) is outside acceptable range, this function will substitute
 * a running average.
 *
 * @param returnNorth - return parameter for the running average North value
 * @param returnEast - return parameter for the running average East value
 * @param returnDown - return parameter for the running average Down value
 * @return GPS error code (marcos defined in gps_error_codes.h)
 */
gnss_error_code_t gnss_get_running_average_velocities(GNSS* self)
{
	// A bit of an obnoxious amount of casting...
	if (self->total_samples > 0) {
		float substituteNorth = ((float)self->v_north_sum) /
				((float)self->total_samples);
		self->v_north_sum += (int64_t)substituteNorth;
		self->GNSS_N_Array[self->total_samples] = (int16_t)substituteNorth;

		float substituteEast = ((float)self->v_east_sum) /
				((float)self->total_samples);
		self->v_east_sum += (int64_t)substituteEast;
		self->GNSS_E_Array[self->total_samples] = (int16_t)substituteEast;

		float substituteDown = ((float)self->v_down_sum) /
				((float)self->total_samples);
		self->v_down_sum += (int64_t)substituteDown;
		self->GNSS_D_Array[self->total_samples] = (int16_t)substituteDown;

		self->total_samples++;
		self->total_samples_averaged++;
		return GNSS_SUCCESS;
	} else {
		// No valid samples yet, avoid divide by zero error
		return GNSS_NO_SAMPLES_ERROR;
	}
}


/**
 * Send the sleep command to the GNSS unit. This does not remove power, but
 * puts the device in low-power mode.
 *
 * @param self - GNSS struct
 * @param put_to_sleep - true to command sleep, false to wake up
 *
 * @return GNSS_CONFIG_ERROR - command failed
 * 		   GNSS_SUCCESS - command succeeded
 */
gnss_error_code_t gnss_sleep(GNSS* self, bool put_to_sleep)
{
	return stop_start_gnss(self, put_to_sleep);
}

/**
 * Switch the FET controlling power to the GNSS unit.
 *
 * @param self - GNSS struct
 * @param on - true for tuen on, false for turn off
 *
 * @return void
 */
void gnss_on_off(GNSS* self, GPIO_PinState pin_state)
{
	HAL_GPIO_WritePin(GPIOG, GNSS_FET_Pin, pin_state);
}

/**
 * Reinitialize the GNSS UART port. Required when switching between Tx and Rx.
 *
 * @param self - GNSS struct
 * @param baud_rate - baud rate to set port to
 */
gnss_error_code_t reset_gnss_uart(GNSS* self, uint16_t baud_rate)
{
	if (HAL_UART_DeInit(self->gnss_uart_handle) != HAL_OK) {
		return GNSS_UART_ERROR;
	}
	self->gnss_uart_handle->Instance = USART3;
	self->gnss_uart_handle->Init.BaudRate = baud_rate;
	self->gnss_uart_handle->Init.WordLength = UART_WORDLENGTH_8B;
	self->gnss_uart_handle->Init.StopBits = UART_STOPBITS_1;
	self->gnss_uart_handle->Init.Parity = UART_PARITY_NONE;
	self->gnss_uart_handle->Init.Mode = UART_MODE_TX_RX;
	self->gnss_uart_handle->Init.HwFlowCtl = UART_HWCONTROL_NONE;
	self->gnss_uart_handle->Init.OverSampling = UART_OVERSAMPLING_16;
	self->gnss_uart_handle->Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
	self->gnss_uart_handle->Init.ClockPrescaler = UART_PRESCALER_DIV1;
	self->gnss_uart_handle->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

	if (HAL_UART_Init(self->gnss_uart_handle) != HAL_OK) {
		return GNSS_UART_ERROR;
	}

	if (HAL_UARTEx_SetTxFifoThreshold(self->gnss_uart_handle,
			UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
	{
		return GNSS_UART_ERROR;
	}
	if (HAL_UARTEx_SetRxFifoThreshold(self->gnss_uart_handle,
			UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
	{
		return GNSS_UART_ERROR;
	}
	if (HAL_UARTEx_DisableFifoMode(self->gnss_uart_handle) != HAL_OK)
	{
		return GNSS_UART_ERROR;
	}

	LL_DMA_ResetChannel(GPDMA1, LL_DMA_CHANNEL_0);

	return GNSS_SUCCESS;
}

/**
 * Set the RTC clock.
 *
 * @param GNSS - GNSS struct
 * @param msg_payload - UBX_NAV_PVT message payload containing
 *        time information.
 *
 * @return GNSS_SUCCESS or
 * 		   GNSS_RTC_ERROR - if setting RTC returned an error
 */
gnss_error_code_t gnss_set_rtc(GNSS* self, uint8_t* msg_payload)
{
	gnss_error_code_t return_code = GNSS_SUCCESS;
	RTC_DateTypeDef rtc_date;
	RTC_TimeTypeDef rtc_time;

	int32_t tAcc = msg_payload[UBX_NAV_PVT_TACC_INDEX] +
			(msg_payload[UBX_NAV_PVT_TACC_INDEX + 1]<<8) +
			(msg_payload[UBX_NAV_PVT_TACC_INDEX + 2]<<16) +
			(msg_payload[UBX_NAV_PVT_TACC_INDEX + 3]<<24);
	uint16_t year = msg_payload[UBX_NAV_PVT_YEAR_INDEX] +
			(msg_payload[UBX_NAV_PVT_YEAR_INDEX + 1]<<8);
	uint8_t month = msg_payload[UBX_NAV_PVT_MONTH_INDEX];
	uint8_t day = msg_payload[UBX_NAV_PVT_DAY_INDEX];
	uint8_t hour = msg_payload[UBX_NAV_PVT_HOUR_INDEX];
	uint8_t min = msg_payload[UBX_NAV_PVT_MINUTE_INDEX];
	uint8_t sec = msg_payload[UBX_NAV_PVT_SECONDS_INDEX];
	uint8_t time_flags = msg_payload[UBX_NAV_PVT_VALID_FLAGS_INDEX];

	// Set the date
	rtc_date.Date = day;
	rtc_date.Month = month;
	rtc_date.Year = year - 2000; // RTC takes a 2 digit year
	// We are not using weekday, but the time will be set incorrectly if this field is not initialized
	rtc_date.WeekDay = RTC_WEEKDAY_MONDAY;
	if (HAL_RTC_SetDate(self->rtc_handle, &rtc_date, RTC_FORMAT_BCD) != HAL_OK) {
		return_code = GNSS_RTC_ERROR;
		return return_code;
	}
	// Set the time
	rtc_time.Hours = hour;
	rtc_time.Minutes = min;
	rtc_time.Seconds = sec;
	rtc_time.SecondFraction = 0;
	if (HAL_RTC_SetTime(self->rtc_handle, &rtc_time, RTC_FORMAT_BCD) != HAL_OK) {
		return_code = GNSS_RTC_ERROR;
		return return_code;
	}

	// We'll only call the clock as set when the valid flags indicate fully resolved,
	// otherwise we'll still set it, it will just be off by some amount
	if (time_flags & FULLY_RESOLVED_AND_VALID_TIME_MASK) {
		self->is_clock_set = true;
	}

	return return_code;
}

/**
 * Send a configuration to the GNSS chip. Will retry up to 10 times before
 * returning failure.
 *
 * @param self- GNSS struct
 * @param config_array - byte array containing a UBX_CFG_VALSET msg with up to
 * 		  64 keys
 */
static gnss_error_code_t send_config(GNSS* self, uint8_t* config_array,
		size_t message_size)
{
	uint8_t fail_counter = 0;
	uint8_t receive_fail_counter = 0;
	ULONG actual_flags;
	uint8_t msg_buf[600];
	char payload[UBX_NAV_PVT_PAYLOAD_LENGTH];
	const char* buf_start = (const char*)&(msg_buf[0]);
	const char* buf_end = buf_start;
	size_t buf_length = sizeof(msg_buf);
    int32_t message_class = 0;
    int32_t message_id = 0;
    int32_t num_payload_bytes = 0;
	// The configuration message, type UBX_CFG_VALSET

	while (fail_counter++ < 10) {
		// Start with a blank msg_buf -- this will short cycle the for loop
		// below if a message was not received in 10 tries
		memset(&(msg_buf[0]),0,sizeof(msg_buf));
		reset_gnss_uart(self, GNSS_DEFAULT_BAUD_RATE);
		// Send over the configuration settings
		HAL_USART_Transmit(self->gnss_uart_handle, &(config_array[0]),
				message_size, ONE_SECOND);
		// Grab the acknowledgment message
		HAL_USART_Receive(self->gnss_uart_handle, &(msg_buf[0]), sizeof(msg_buf), ONE_SECOND * 2);
//		receive_fail_counter = 0;
//		do {
//			HAL_UART_Receive_DMA(self->gnss_uart_handle, &(msg_buf[0]),
//				sizeof(msg_buf));
//			__HAL_DMA_DISABLE_IT(self->gnss_dma_handle, DMA_IT_HT);
//			HAL_Delay(1250);
//			if (tx_event_flags_get(self->event_flags, GNSS_CONFIG_RECVD, TX_OR_CLEAR,
//					&actual_flags, TX_NO_WAIT) == TX_SUCCESS) {
//				break;
//			}
//		} while (receive_fail_counter++ < 2);
//
//		if (receive_fail_counter > 2) {
//			continue;
//		}
		/* The ack/nak message is guaranteed to be sent within one second, but
		 * we may receive a few navigation messages before the ack is received,
		 * so we have to sift through at least one second worth of messages */
		for (num_payload_bytes = uUbxProtocolDecode(buf_start, buf_length,
				&message_class, &message_id, payload, sizeof(payload), &buf_end);
				num_payload_bytes > 0;
				num_payload_bytes = uUbxProtocolDecode(buf_start, buf_length,
				&message_class, &message_id, payload, sizeof(payload), &buf_end))
		{
			if (message_class == 0x05) {
				// Msg class 0x05 is either an ACK or NAK
				if (message_id == 0x00) {
					// This is a NAK msg, the config did not go through properly
					break;
				}

				if (message_id == 0x01) {
					// This is an ACK message, we're good
					reset_gnss_uart(self, GNSS_DEFAULT_BAUD_RATE);
					return GNSS_SUCCESS;
				}
			}
			// Adjust pointers to continue searching the buffer
			buf_length -= buf_end - buf_start;
			buf_start = buf_end;
		}

		// Reinitialize the UART port and restart DMA receive
		reset_gnss_uart(self, GNSS_DEFAULT_BAUD_RATE);
		// Zero out the buffer to prevent reading old values
		memset(&(msg_buf[0]),0,sizeof(msg_buf));
		buf_start = (const char*)&(msg_buf[0]);
		buf_end = buf_start;
	}
	// If we made it here, config failed 10 attempts
	reset_gnss_uart(self, GNSS_DEFAULT_BAUD_RATE);
	return GNSS_CONFIG_ERROR;
}

/**
 * Send a CFG_RST message to the GNSS chip to either start or stop GNSS
 * processing. This message is not acknowledged, so we just have to trust that
 * it worked.
 *
 * @param self- GNSS struct
 * @param send_stop - true: send a stop message; false: sent a start message.
 */
static gnss_error_code_t stop_start_gnss(GNSS* self, bool send_stop)
{
	// 3rd byte -- 0x08 = Controlled GNSS stop, 0x09 = Controlled GNSS start
	uint8_t message_payload[4] = {0x00, 0x00, (send_stop) ? 0x08 : 0x09, 0x00};
	char cfg_rst_message[sizeof(message_payload) +
						 U_UBX_PROTOCOL_OVERHEAD_LENGTH_BYTES];

	if ((uUbxProtocolEncode(0x06, 0x04, (const char*)&(message_payload[0]),
			sizeof(message_payload),cfg_rst_message)) < 0)
	{
		return GNSS_CONFIG_ERROR;
	}

	if ((HAL_UART_Transmit(self->gnss_uart_handle, (uint8_t*)&(cfg_rst_message[0]),
					sizeof(cfg_rst_message), 1000)) != HAL_OK)
	{
		return GNSS_CONFIG_ERROR;
	}

	return GNSS_SUCCESS;
}

/**
 * Helper function that processes UBX_NAV_PVT messages from a buffer.
 *
 * @param self- GNSS struct
 * @param
 */
static void internal_process_messages(GNSS* self, uint8_t* process_buf)
{
	uint8_t payload[UBX_NAV_PVT_PAYLOAD_LENGTH];
	const char* buf_start = (const char*)&(process_buf[0]);
	const char* buf_end = buf_start;
	// Our input buffer is a message off the queue, 10 UBX_NAV_PVT msgs
	size_t buf_length = UBX_BUFFER_SIZE;
	int32_t message_class = 0;
	int32_t message_id = 0;
	int32_t num_payload_bytes = 0;
	int32_t lat, lon, sAcc, vnorth, veast, vdown;
	int16_t pDOP;
	// Reset the valid message processed counter
	self->messages_processed = 0;

	// Really gross for loop that processes msgs in each iteration
	for (num_payload_bytes = uUbxProtocolDecode(buf_start, buf_length,
				 &message_class, &message_id, (char*)payload, sizeof(payload), &buf_end);
			num_payload_bytes > 0;
			num_payload_bytes = uUbxProtocolDecode(buf_start, buf_length,
				 &message_class, &message_id, (char*)payload, sizeof(payload), &buf_end))
	{
		// UBX_NAV_PVT payload is 92 bytes, message class is 0x01,
		// message ID is 0x07
		if (num_payload_bytes != UBX_NAV_PVT_PAYLOAD_LENGTH ||
				message_class != UBX_NAV_PVT_MESSAGE_CLASS  ||
				message_id    != UBX_NAV_PVT_MESSAGE_ID)
		{
			self->get_running_average_velocities(self);
			self->number_cycles_without_data++;
			buf_length -= buf_end - buf_start;
			buf_start = buf_end;
			continue;
		}

		// Even if we don't end up using the values, we did get a valid message
		self->messages_processed++;
		// Grab a bunch of things from the message
		lon = (int32_t) get_four_bytes(payload, UBX_NAV_PVT_LON_INDEX, AS_LITTLE_ENDIAN);
		lat = (int32_t) get_four_bytes(payload, UBX_NAV_PVT_LAT_INDEX, AS_LITTLE_ENDIAN);
		pDOP = (int16_t) get_two_bytes(payload, UBX_NAV_PVT_PDOP_INDEX, AS_LITTLE_ENDIAN);
		sAcc = (int32_t) get_four_bytes(payload, UBX_NAV_PVT_SACC_INDEX, AS_LITTLE_ENDIAN);
		vnorth = (int32_t) get_four_bytes(payload, UBX_NAV_PVT_V_NORTH_INDEX, AS_LITTLE_ENDIAN);
		veast = (int32_t) get_four_bytes(payload, UBX_NAV_PVT_V_EAST_INDEX, AS_LITTLE_ENDIAN);
		vdown = (int32_t) get_four_bytes(payload, UBX_NAV_PVT_V_DOWN_INDEX, AS_LITTLE_ENDIAN);

		// Set the clock if needed
		if (!self->is_clock_set) {
			self->set_rtc(self, (uint8_t*)payload);
		}

		// reset the location validity flag
		self->is_location_valid = false;
		// Check Lat/Long accuracy, assign to class fields if good
		if (pDOP < MAX_ACCEPTABLE_PDOP) {
			self->current_latitude = lat;
			self->current_longitude = lon;
			self->is_location_valid = true;
		}

		// reset the velocity validity flag
		self->is_velocity_valid = false;
		// Grab velocities, start by checking speed accuracy estimate (sAcc)
		if (sAcc > MAX_ACCEPTABLE_SACC) {
			// This message was not within acceptable parameters,
			self->get_running_average_velocities(self);
			buf_length -= buf_end - buf_start;
			buf_start = buf_end;
			continue;
		}

		// vAcc was within acceptable range, still need to check
		// individual velocities are less than MAX_POSSIBLE_VELOCITY
		if ((vnorth > MAX_POSSIBLE_VELOCITY) ||
			(veast > MAX_POSSIBLE_VELOCITY) ||
			(vdown > MAX_POSSIBLE_VELOCITY)) {
			// One or more velocity component was greater than the
			// max possible velocity. Loop around and try again
			self->get_running_average_velocities(self);
			buf_length -= buf_end - buf_start;
			buf_start = buf_end;
			continue;
		}

		self->is_velocity_valid = true;
		// All velocity values are good to go, convert them to
		// shorts and store them in the arrays
		self->GNSS_N_Array[self->total_samples] = (int16_t)vnorth;
		self->GNSS_E_Array[self->total_samples] = (int16_t)veast;
		self->GNSS_D_Array[self->total_samples] = (int16_t)vdown;

		self->number_cycles_without_data = 0;
		self->total_samples++;

		buf_length -= buf_end - buf_start;
		buf_start = buf_end;
	}
}

static void process_self_test_messages(GNSS* self, uint8_t* process_buf)
{
	char payload[UBX_NAV_PVT_PAYLOAD_LENGTH];
	const char* buf_start = (const char*)&(process_buf[0]);
	const char* buf_end = buf_start;
	// Our input buffer is a message off the queue, 10 UBX_NAV_PVT msgs
	size_t buf_length = SELF_TEST_BUFFER_SIZE;
	int32_t message_class = 0;
	int32_t message_id = 0;
	int32_t num_payload_bytes = 0;
	// Reset the counters
	self->messages_processed = 0;
	self->number_cycles_without_data = 0;
	self->total_samples = 0;

	// Really gross for loop that processes msgs in each iteration
	for (num_payload_bytes = uUbxProtocolDecode(buf_start, buf_length,
				 &message_class, &message_id, payload, sizeof(payload), &buf_end);
			num_payload_bytes > 0;
			num_payload_bytes = uUbxProtocolDecode(buf_start, buf_length,
				 &message_class, &message_id, payload, sizeof(payload), &buf_end))
	{
		// UBX_NAV_PVT payload is 92 bytes, message class is 0x01,
		// message ID is 0x07
		if (num_payload_bytes != UBX_NAV_PVT_PAYLOAD_LENGTH ||
				message_class != UBX_NAV_PVT_MESSAGE_CLASS  ||
				message_id    != UBX_NAV_PVT_MESSAGE_ID)
		{
			self->number_cycles_without_data++;
			buf_length -= buf_end - buf_start;
			buf_start = buf_end;
			continue;
		}

		// need to keep track of how many messges were processsed in the buffer
		self->messages_processed++;

		// Only thing we're checking is that we have more than 4 sattelites in view
		uint8_t num_satellites_tracked = payload[23];

		if (num_satellites_tracked < 4) {
			self->number_cycles_without_data++;
			buf_length -= buf_end - buf_start;
			buf_start = buf_end;
			continue;
		}

		self->number_cycles_without_data = 0;
		self->total_samples++;

		buf_length -= buf_end - buf_start;
		buf_start = buf_end;
	}
}

/**
 * Calculate the two checksum bytes for a UBX message
 *
 * @param ck_a - reference to first checksum byte
 * @param ck_b - reference to second checksum byte
 * @param buffer - address of first byte of array
 * @param num_bytes - number of bytes to calculate checksum over
 */
static void get_checksum(uint8_t* ck_a, uint8_t* ck_b, uint8_t* buffer,
		uint32_t num_bytes)
{
	*ck_a = 0;
	*ck_b = 0;
	for (int i = 0; i < num_bytes; i++) {
		*ck_a = *ck_a + buffer[i];
		*ck_b = *ck_b + *ck_a;
	}
}

