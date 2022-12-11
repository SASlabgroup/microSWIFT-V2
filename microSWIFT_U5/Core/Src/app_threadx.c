/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_threadx.c
  * @author  MCD Application Team
  * @brief   ThreadX applicative file
  ******************************************************************************
    * @attention
  *
  * Copyright (c) 2022 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "app_threadx.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "main.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum thread_priorities{
	HIGHEST = 0,
	VERY_HIGH = 1,
	HIGH = 2,
	MID= 3,
	LOW = 4,
	LOWEST = 5
}thread_priorities_t;

typedef enum status_flags{
	// Ready states
	GNSS_READY = 1 << 0,
	IMU_READY = 1 << 1,
	CT_READY = 1 << 2,
	MODEM_READY = 1 << 3,
	WAVES_READY = 1 << 4,
	// Done states
	GNSS_DONE = 1 << 4,
	IMU_DONE = 1 << 5,
	CT_DONE = 1 << 6,
	MODEM_DONE = 1 << 7,
	// Sleep states
	SET_STOP_2 = 1 << 8,
	RESET_STOP_2 = 1 << 9,
	SET_SHUTDOWN = 1 << 10,
	RESET_SHUTDOWN = 1 << 11,
	// Error states
	GPS_ERROR = 1 << 12,
	IMU_ERROR = 1 << 13,
	CT_ERROR = 1 << 14,
	MODEM_ERROR = 1 << 15,
	MEMORY_ALLOC_ERROR = 1 << 16
}status_flags_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// gps_thread, imu_thread, and waves_thread will get a large stack, startup_thread
// and teardown_thread will get small stacks.
#define THREAD_LARGE_STACK_SIZE 2048
#define THREAD_SMALL_STACK_SIZE 512
// Sensor data arrays -> 2bytes * 5Hz * 2500 seconds = 25,000 bytes
// To get these 32 bit aligned, we'll do 25024 bytes
#define SENSOR_DATA_ARRAY_SIZE 25024
// GPSWaves arrays -> 8 bytes * 5Hz * 2500 seconds = 100,000 bytes.
// There will be a little bit of overhead, so we'll add another 256 bytes
#define WAVES_ARRAY_SIZE 100256
// Size of the CT data array TODO: figure out exact size needed
#define CT_DATA_ARRAY_SIZE 512
// Size of an Iridium message TODO: figure this out
#define IRIDIUM_MESSAGE_SIZE 340
// UBX_NAV_PVT is 100 bytes
#define UBX_MESSAGE_SIZE 200
// The size of our queue
#define UBX_QUEUE_SIZE 5
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
// Out threads
TX_THREAD startup_thread;
TX_THREAD gnss_thread;
TX_THREAD imu_thread;
TX_THREAD ct_thread;
TX_THREAD waves_thread;
TX_THREAD iridium_thread;
TX_THREAD teardown_thread;
// We'll use flags to signal other threads to run/shutdown
TX_EVENT_FLAGS_GROUP thread_flags;
// The UBX message queue, fed by UART via DMA, processed by gnss
TX_QUEUE ubx_queue;
// All our data to store/ process
int16_t* uGNSSArray;
int16_t* vGNSSArray;
int16_t* zGNSSArray;
int16_t* uIMUArray;
int16_t* vIMUArray;
int16_t* zIMUArray;
double* uWavesArray;
double* vWavesArray;
double* zWavesArray;
double* wavesTempCopyArray;
CHAR ubx_DMA_message_buf[UBX_MESSAGE_SIZE + 8];
CHAR queue_message_1[UBX_MESSAGE_SIZE];
CHAR queue_message_2[UBX_MESSAGE_SIZE];
CHAR queue_message_3[UBX_MESSAGE_SIZE];
CHAR queue_message_4[UBX_MESSAGE_SIZE];
CHAR queue_message_5[UBX_MESSAGE_SIZE];
CHAR ct_data;
CHAR iridium_message;
GNSS* gnss;

UART_HandleTypeDef* gnss_uart;
DMA_HandleTypeDef* dma_handle;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
void startup_thread_entry(ULONG thread_input);
void gnss_thread_entry(ULONG thread_input);
void imu_thread_entry(ULONG thread_input);
void ct_thread_entry(ULONG thread_input);
void waves_thread_entry(ULONG thread_input);
void iridium_thread_entry(ULONG thread_input);
void teardown_thread_entry(ULONG thread_input);
/* USER CODE END PFP */

/**
  * @brief  Application ThreadX Initialization.
  * @param memory_ptr: memory pointer
  * @retval int
  */
UINT App_ThreadX_Init(VOID *memory_ptr)
{
  UINT ret = TX_SUCCESS;
  TX_BYTE_POOL *byte_pool = (TX_BYTE_POOL*)memory_ptr;

   /* USER CODE BEGIN App_ThreadX_MEM_POOL */
	(void)byte_pool;
	CHAR *pointer = TX_NULL;

	//
	// Allocate stack for the startup thread
	ret = tx_byte_allocate(byte_pool, (VOID**) &pointer, THREAD_SMALL_STACK_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	// Create the startup thread. HIGHEST priority level and no preemption possible
	ret = tx_thread_create(&startup_thread, "startup thread", startup_thread_entry, 0, pointer,
		  THREAD_SMALL_STACK_SIZE, HIGHEST, HIGHEST, TX_NO_TIME_SLICE, TX_AUTO_START);
	if (ret != TX_SUCCESS){
	  return ret;
	}

	//
	// Allocate stack for the gnss thread
	ret = tx_byte_allocate(byte_pool, (VOID**) &pointer, THREAD_LARGE_STACK_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	// Create the gnss thread. VERY_HIGH priority, no preemption-threshold
	ret = tx_thread_create(&gnss_thread, "gnss thread", gnss_thread_entry, 0, pointer,
		  THREAD_LARGE_STACK_SIZE, VERY_HIGH, VERY_HIGH, TX_NO_TIME_SLICE, TX_AUTO_START);
	if (ret != TX_SUCCESS){
	  return ret;
	}

	//
	// Allocate stack for the imu thread
	ret = tx_byte_allocate(byte_pool, (VOID**) &pointer, THREAD_LARGE_STACK_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	// Create the imu thread. VERY_HIGH priority, no preemption-threshold
	ret = tx_thread_create(&imu_thread, "imu thread", imu_thread_entry, 0, pointer,
		  THREAD_LARGE_STACK_SIZE, HIGH, HIGH, TX_NO_TIME_SLICE, TX_AUTO_START);
	if (ret != TX_SUCCESS){
	  return ret;
	}

	//
	// Allocate stack for the CT thread
	ret = tx_byte_allocate(byte_pool, (VOID**) &pointer, THREAD_SMALL_STACK_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	// Create the CT thread. VERY_HIGH priority, no preemption-threshold
	ret = tx_thread_create(&ct_thread, "ct thread", ct_thread_entry, 0, pointer,
		  THREAD_SMALL_STACK_SIZE, VERY_HIGH, VERY_HIGH, TX_NO_TIME_SLICE, TX_AUTO_START);
	if (ret != TX_SUCCESS){
	  return ret;
	}

	//
	// Allocate stack for the waves thread
	ret = tx_byte_allocate(byte_pool, (VOID**) &pointer, THREAD_LARGE_STACK_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	// Create the waves thread. MID priority, no preemption-threshold
	ret = tx_thread_create(&waves_thread, "waves thread", waves_thread_entry, 0, pointer,
		  THREAD_LARGE_STACK_SIZE, MID, MID, TX_NO_TIME_SLICE, TX_AUTO_START);
	if (ret != TX_SUCCESS){
	  return ret;
	}

	//
	// Allocate stack for the Iridium thread
	ret = tx_byte_allocate(byte_pool, (VOID**) &pointer, THREAD_SMALL_STACK_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	// Create the Iridium thread. VERY_HIGH priority, no preemption-threshold
	ret = tx_thread_create(&iridium_thread, "iridium thread", iridium_thread_entry, 0, pointer,
		  THREAD_SMALL_STACK_SIZE, MID, MID, TX_NO_TIME_SLICE, TX_AUTO_START);
	if (ret != TX_SUCCESS){
	  return ret;
	}

	//
	// Allocate stack for the teardown thread
	ret = tx_byte_allocate(byte_pool, (VOID**) &pointer, THREAD_SMALL_STACK_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	// Create the teardown thread. HIGHEST priority, no preemption-threshold
	ret = tx_thread_create(&teardown_thread, "teardown thread", teardown_thread_entry, 0, pointer,
		  THREAD_SMALL_STACK_SIZE, HIGHEST, HIGHEST, TX_NO_TIME_SLICE, TX_AUTO_START);
	if (ret != TX_SUCCESS){
	  return ret;
	}

	//
	// Create the event flags we'll use for triggering threads
	ret = tx_event_flags_create(&thread_flags, "thread_flags");
	if (ret != TX_SUCCESS) {
	  return ret;
	}

	//
	// Create our UBX message queue
	ret = tx_queue_create(&ubx_queue, "ubx queue", TX_1_ULONG, pointer, (UBX_QUEUE_SIZE * sizeof(ULONG)));
	if (ret != TX_SUCCESS) {
		return ret;
	}

	//
	// Allocate bytes for the sensor derived arrays
	ret = tx_byte_allocate(byte_pool, (VOID**) &uGNSSArray, SENSOR_DATA_ARRAY_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	ret = tx_byte_allocate(byte_pool, (VOID**) &vGNSSArray, SENSOR_DATA_ARRAY_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	ret = tx_byte_allocate(byte_pool, (VOID**) &vGNSSArray, SENSOR_DATA_ARRAY_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	ret = tx_byte_allocate(byte_pool, (VOID**) &uIMUArray, SENSOR_DATA_ARRAY_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	ret = tx_byte_allocate(byte_pool, (VOID**) &vIMUArray, SENSOR_DATA_ARRAY_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	ret = tx_byte_allocate(byte_pool, (VOID**) &zIMUArray, SENSOR_DATA_ARRAY_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	// Allocate bytes for the GPSWaves processing arrays
	ret = tx_byte_allocate(byte_pool, (VOID**) &uWavesArray, WAVES_ARRAY_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	ret = tx_byte_allocate(byte_pool, (VOID**) &vWavesArray, WAVES_ARRAY_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	ret = tx_byte_allocate(byte_pool, (VOID**) &zWavesArray, WAVES_ARRAY_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	ret = tx_byte_allocate(byte_pool, (VOID**) &wavesTempCopyArray, WAVES_ARRAY_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}

	// The UBX message array
	ret = tx_byte_allocate(byte_pool, (VOID**) &ubx_DMA_message_buf, UBX_MESSAGE_SIZE + 8, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	// Queue message 1
	ret = tx_byte_allocate(byte_pool, (VOID**) &queue_message_1, UBX_MESSAGE_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	// Queue message 2
	ret = tx_byte_allocate(byte_pool, (VOID**) &queue_message_2, UBX_MESSAGE_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	// Queue message 3
	ret = tx_byte_allocate(byte_pool, (VOID**) &queue_message_3, UBX_MESSAGE_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	// Queue message 4
	ret = tx_byte_allocate(byte_pool, (VOID**) &queue_message_4, UBX_MESSAGE_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	// Queue message 5
	ret = tx_byte_allocate(byte_pool, (VOID**) &queue_message_5, UBX_MESSAGE_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}

	// The CT data array
	ret = tx_byte_allocate(byte_pool, (VOID**) &ct_data, CT_DATA_ARRAY_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	// The Iridium message array
	ret = tx_byte_allocate(byte_pool, (VOID**) &iridium_message, IRIDIUM_MESSAGE_SIZE, TX_NO_WAIT);
	if (ret != TX_SUCCESS){
	  return ret;
	}
	// The gnss struct
	ret = tx_byte_allocate(byte_pool, (VOID**) &gnss, sizeof(GNSS), TX_NO_WAIT);
	if (ret != TX_SUCCESS){
		return ret;
	}
  /* USER CODE END App_ThreadX_MEM_POOL */

  /* USER CODE BEGIN App_ThreadX_Init */
  /* USER CODE END App_ThreadX_Init */

  return ret;
}

  /**
  * @brief  MX_ThreadX_Init
  * @param  None
  * @retval None
  */
void MX_ThreadX_Init(UART_HandleTypeDef* gnss_uart_handle, DMA_HandleTypeDef* handle_GPDMA1_Channel0)
{
  /* USER CODE BEGIN  Before_Kernel_Start */
  gnss_uart = gnss_uart_handle;
  dma_handle = handle_GPDMA1_Channel0;
  /* USER CODE END  Before_Kernel_Start */

  tx_kernel_enter();

  /* USER CODE BEGIN  Kernel_Start_Error */

  /* USER CODE END  Kernel_Start_Error */
}

/**
  * @brief  App_ThreadX_LowPower_Timer_Setup
  * @param  count : TX timer count
  * @retval None
  */
void App_ThreadX_LowPower_Timer_Setup(ULONG count)
{
  /* USER CODE BEGIN  App_ThreadX_LowPower_Timer_Setup */

  /* USER CODE END  App_ThreadX_LowPower_Timer_Setup */
}

/**
  * @brief  App_ThreadX_LowPower_Enter
  * @param  None
  * @retval None
  */
void App_ThreadX_LowPower_Enter(void)
{
  /* USER CODE BEGIN  App_ThreadX_LowPower_Enter */

  /* USER CODE END  App_ThreadX_LowPower_Enter */
}

/**
  * @brief  App_ThreadX_LowPower_Exit
  * @param  None
  * @retval None
  */
void App_ThreadX_LowPower_Exit(void)
{
  /* USER CODE BEGIN  App_ThreadX_LowPower_Exit */

  /* USER CODE END  App_ThreadX_LowPower_Exit */
}

/**
  * @brief  App_ThreadX_LowPower_Timer_Adjust
  * @param  None
  * @retval Amount of time (in ticks)
  */
ULONG App_ThreadX_LowPower_Timer_Adjust(void)
{
  /* USER CODE BEGIN  App_ThreadX_LowPower_Timer_Adjust */
  return 0;
  /* USER CODE END  App_ThreadX_LowPower_Timer_Adjust */
}

/* USER CODE BEGIN 1 */
/**
  * @brief  startup_thread_entry
  *         This thread will start all peripherals and do a systems check to
  *         make sure we're good to start the processing cycle
  * @param  ULONG thread_input - unused
  * @retval void
  */
void startup_thread_entry(ULONG thread_input){
	// TODO: memset all arrays and such to 0 initialized
	// TODO: figure out self-check
	// TODO: set event flags to "ready" for all threads except waves
	// TODO: start DMA transfer in here, make sure queue overwrites oldest
}

/**
  * @brief  gnss_thread_entry
  *         Thread that governs the GNSS message processing and building of
  *         uGNSSArray, vGNSSArray, zGNSSArray arrays.
  * @param  ULONG thread_input - unused
  * @retval void
  */
void gnss_thread_entry(ULONG thread_input){
	HAL_StatusTypeDef ret;
	// Initialize GNSS
	gnss_init(gnss, gnss_uart, &ubx_queue, uGNSSArray, vGNSSArray, zGNSSArray);
//  No need for the half-transfer complete interrupt, so disable it

	LL_DMA_ResetChannel(GPDMA1, LL_DMA_CHANNEL_0);
	ret = HAL_UART_Receive_DMA(gnss->gnss_uart_handle, ubx_DMA_message_buf, UBX_MESSAGE_SIZE);
	__HAL_DMA_DISABLE_IT(dma_handle, DMA_IT_HT);
	__HAL_UART_ENABLE_IT(gnss->gnss_uart_handle, UART_IT_IDLE);

	while(1){
		gnss->gnss_process_message(gnss);
	}

}

/**
  * @brief  imu_thread_entry
  *         Thread that governs the IMU velocity processing and building of
  *         uIMUArray, vIMUArray, zIMUArray arrays.
  * @param  ULONG thread_input - unused
  * @retval void
  */
void imu_thread_entry(ULONG thread_input){

}

/**
  * @brief  ct_thread_entry
  *         This thread will handle the CT sensor, capture readings, and store
  *         in ct_data.
  * @param  ULONG thread_input - unused
  * @retval void
  */
void ct_thread_entry(ULONG thread_input){

}

/**
  * @brief  waves_thread_entry
  *         This thread will run the GPSWaves algorithm. The arrays uWavesArray,
  *         vWavesArray, zWavesArray, and wavesTempCopyArray are allocated for
  *         this thread to use.
  * @param  ULONG thread_input - unused
  * @retval void
  */
void waves_thread_entry(ULONG thread_input){

}

/**
  * @brief  iridium_thread_entry
  *         This thread will handle message sending via Iridium modem. The
  *         buffer iridium_message is provided for message storage.
  * @param  ULONG thread_input - unused
  * @retval void
  */
void iridium_thread_entry(ULONG thread_input){

}

/**
  * @brief  teardown_thread_entry
  *         This thread will execute when either an error flag is set or all
  *         the done flags are set, indicating we are ready to shutdown until
  *         the next window.
  * @param  ULONG thread_input - unused
  * @retval void
  */
void teardown_thread_entry(ULONG thread_input){
	// TODO: Figure out the right flag combinations to start this thread
	// 	For now, we'll just assume the right combo is that everything is done
	UINT status;
	ULONG retreived_flags;
	ULONG done_flags_to_check = GNSS_DONE &
								IMU_DONE &
								CT_DONE &
								MODEM_DONE;
	ULONG error_flags_to_check = GPS_ERROR &
								 IMU_ERROR &
								 CT_ERROR &
								 MODEM_ERROR &
								 MEMORY_ALLOC_ERROR;

	while(1) {
		retreived_flags = 0x0;
		// Start by checking if we get an error flag. The last argument of "1"
		// means we will check this every tick
		status = tx_event_flags_get(&thread_flags,
				error_flags_to_check,
				TX_OR, &retreived_flags, 1);
		// Clear out all bit positions except for the error bits
		retreived_flags &= 0x1F000;
		if ((status == TX_SUCCESS) && (retreived_flags & error_flags_to_check)) {
			// We received an error flag, restart and try again
			HAL_NVIC_SystemReset();
		}
		// Clear out all bit positions except for the done bits
		retreived_flags &= 0xF0;
		// Now we'll check the done flags
		status = tx_event_flags_get(&thread_flags,
						done_flags_to_check,
						TX_AND, &retreived_flags, 1);
		if ((status == TX_SUCCESS) && ~(retreived_flags ^ done_flags_to_check)) {
			// We received all the done bits, break out of the loop so we can
			// shut everything down
			break;
		}
	}
	// If we made it here, we received all the done bits and we're good to
	// dealloc memory and shutdown. We're not going to check the return value
	// because we're going to standby mode regardless, and all RAM will be lost.
	tx_byte_release(&startup_thread);
	tx_byte_release(&gnss_thread);
	tx_byte_release(&imu_thread);
	tx_byte_release(&ct_thread);
	tx_byte_release(&waves_thread);
	tx_byte_release(&iridium_thread);
	tx_byte_release(&teardown_thread);
	tx_byte_release(&thread_flags);
	tx_byte_release(&uGNSSArray);
	tx_byte_release(&vGNSSArray);
	tx_byte_release(&zGNSSArray);
	tx_byte_release(&uIMUArray);
	tx_byte_release(&vIMUArray);
	tx_byte_release(&zIMUArray);
	tx_byte_release(&uWavesArray);
	tx_byte_release(&vWavesArray);
	tx_byte_release(&zWavesArray);
	tx_byte_release(&wavesTempCopyArray);
	tx_byte_release(&ubx_DMA_message_buf);
	tx_byte_release(&ct_data);
	tx_byte_release(&iridium_message);

	// TODO: figure out how to go into standby mode
	// This is just a placeholder for development/debugging purposes
	HAL_NVIC_SystemReset();
}

/**
  * @brief  UART ISR callback
  *         We are receiving UBX messages via DMA, waiting for IDLE condition.
  *         Once idle occurs, this ISR callback is called. We will take the
  *         received message and push it onto the ubx_queue for processing.
  *         If we receive less than UBX_MESSAGE_SIZE bytes, the message will
  *         be discarded.
  * @param  UART_HandleTypeDef *huart - pointer to the UART handle
  * 		uint16_t Size - number of bytes received
  * @retval void
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
	// !!! Save the thread context
	_tx_thread_context_save();
	HAL_StatusTypeDef HAL_ret;
	// Need to make sure this is being called by USART3 (the GNSS UART port)
	if (huart->Instance == USART3) {
		ULONG num_msgs_enqueued, available_space;
		UINT ret;
		// get info on the number of enqueued messages and available space
		ret = tx_queue_info_get(gnss->ubx_queue, TX_NULL, &num_msgs_enqueued,
				&available_space, TX_NULL, TX_NULL, TX_NULL);
		if ((ret != TX_SUCCESS) || ((num_msgs_enqueued + available_space) != UBX_QUEUE_SIZE)) {
			// Something went wrong trying to get status, restart
			// restart DMA receive to idle
			LL_DMA_ResetChannel(GPDMA1, LL_DMA_CHANNEL_0);
			HAL_ret = HAL_UART_Receive_DMA(gnss->gnss_uart_handle,
					ubx_DMA_message_buf, UBX_MESSAGE_SIZE);
			return;
		}
		// TODO: figure out why the ubx buf is not getting copied to the msg
		CHAR* current_msg;
		// Find the right queue message pointer to assign to
		switch(num_msgs_enqueued){
		case 0:
			current_msg = queue_message_1;
			break;
		case 1:
			current_msg = queue_message_2;
			break;
		case 2:
			current_msg = queue_message_3;
			break;
		case 3:
			current_msg = queue_message_4;
			break;
		case 4:
			current_msg = queue_message_5;
			break;
		default:
			current_msg = queue_message_1;
			break;
		}

		memcpy(current_msg, (void*)ubx_DMA_message_buf, UBX_MESSAGE_SIZE);
		tx_queue_front_send(gnss->ubx_queue, current_msg, TX_NO_WAIT);
//		HAL_DMA_Init(dma_handle);
////	    if (HAL_DMA_Init(dma_handle) != HAL_OK)
////	    {
////	      Error_Handler();
////	    }
	    HAL_UART_Init(gnss->gnss_uart_handle);
//		gnss->gnss_uart_handle->Instance->ICR |= (1 << 4);
		HAL_ret = HAL_UART_Receive_DMA(gnss->gnss_uart_handle,
				ubx_DMA_message_buf, UBX_MESSAGE_SIZE);
		if (HAL_ret != HAL_OK) {
			HAL_ret = HAL_OK;
		}
	}
	_tx_thread_context_restore();
}

/**
  * @brief  UART ISR callback
  *         We are receiving UBX messages via DMA, waiting for IDLE condition.
  *         Once idle occurs, this ISR callback is called. We will take the
  *         received message and push it onto the ubx_queue for processing.
  *         If we receive less than UBX_MESSAGE_SIZE bytes, the message will
  *         be discarded.
  * @param  UART_HandleTypeDef *huart - pointer to the UART handle
  * 		uint16_t Size - number of bytes received
  * @retval void
  */
void HAL_UART_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
	// !!! Save the thread context
	_tx_thread_context_save();
	HAL_StatusTypeDef HAL_ret;
	// Need to make sure this is being called by USART3 (the GNSS UART port)
	if (huart->Instance == USART3) {
		if (Size == 100) {
			ULONG num_msgs_enqueued, available_space;
			UINT ret;
			// get info on the number of enqueued messages and available space
			ret = tx_queue_info_get(gnss->ubx_queue, TX_NULL, &num_msgs_enqueued,
					&available_space, TX_NULL, TX_NULL, TX_NULL);
			if ((ret != TX_SUCCESS) || ((num_msgs_enqueued + available_space) != UBX_QUEUE_SIZE)) {
				// Something went wrong trying to get status, restart
				// restart DMA receive to idle
				HAL_ret = HAL_UART_Receive_DMA(gnss->gnss_uart_handle,
						ubx_DMA_message_buf, UBX_MESSAGE_SIZE);
				return;
			}

			CHAR* current_msg;
			// Find the right queue message pointer to assign to
			switch(num_msgs_enqueued){
			case 0:
				current_msg = queue_message_1;
				break;
			case 1:
				current_msg = queue_message_2;
				break;
			case 2:
				current_msg = queue_message_3;
				break;
			case 3:
				current_msg = queue_message_4;
				break;
			case 4:
				current_msg = queue_message_5;
				break;
			default:
				current_msg = queue_message_1;
				break;
			}

			memcpy(current_msg, (void*)ubx_DMA_message_buf, UBX_MESSAGE_SIZE);
			tx_queue_front_send(gnss->ubx_queue, current_msg, TX_NO_WAIT);

//			LL_DMA_ResetChannel(dma_handle, LL_DMA_CHANNEL_0);

			HAL_ret = HAL_UARTEx_ReceiveToIdle_DMA(gnss->gnss_uart_handle, ubx_DMA_message_buf, UBX_MESSAGE_SIZE);
			if (HAL_ret != HAL_OK) {
				HAL_ret = HAL_OK;
			}
		}
	} else {
		HAL_ret = HAL_UARTEx_ReceiveToIdle_DMA(gnss->gnss_uart_handle, ubx_DMA_message_buf, UBX_MESSAGE_SIZE);
		__HAL_DMA_DISABLE_IT(dma_handle, DMA_IT_HT);
		if (HAL_ret != HAL_OK) {
			HAL_ret = HAL_OK;
		}
	}
	_tx_thread_context_restore();
}

//void align32Bytes(CHAR* pointer){
//	if (pointer % 32 != 0) {
//		pointer += pointer % 32;
//	}
//}
/* USER CODE END 1 */
