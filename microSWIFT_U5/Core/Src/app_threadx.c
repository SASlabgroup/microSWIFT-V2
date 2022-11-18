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

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_STACK_SIZE 2000
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
TX_THREAD thread_0;
TX_THREAD thread_1;
TX_THREAD thread_2;
TX_THREAD thread_3;
//CHAR uArray[10000];
//CHAR vArray[10000];
//CHAR zArray[10000];

TX_QUEUE queue;
TX_EVENT_FLAGS_GROUP eventFlags;
TX_BYTE_POOL theHeap;
TX_BLOCK_POOL memory_block_pool_0;
TX_BLOCK_POOL memory_block_pool_1;
TX_BLOCK_POOL memory_block_pool_2;
TX_BLOCK_POOL memory_block_pool_3;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
void thread_0_entry(ULONG thread_input);
void thread_1_entry(ULONG thread_input);
void thread_2_entry(ULONG thread_input);
void thread_3_entry(ULONG thread_input);
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

  CHAR *pointer;
  if (tx_byte_allocate(byte_pool, (VOID **)&pointer, APP_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS) {
	  ret = TX_POOL_ERROR;
  }

  ret = tx_block_pool_create(&memory_block_pool_0, "arrays pool", 125000, pointer, 125004);
  if (ret != TX_SUCCESS) {
	  ret = TX_POOL_ERROR;
  }

  ret = tx_block_pool_create(&memory_block_pool_1, "arrays pool", 125000, pointer, 125004);
  if (ret != TX_SUCCESS) {
	  ret = TX_POOL_ERROR;
  }

  ret = tx_block_pool_create(&memory_block_pool_2, "arrays pool", 125000, pointer, 125004);
  if (ret != TX_SUCCESS) {
	  ret = TX_POOL_ERROR;
  }

  ret = tx_block_pool_create(&memory_block_pool_3, "arrays pool", 125000, pointer, 125004);
  if (ret != TX_SUCCESS) {
	  ret = TX_POOL_ERROR;
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
void MX_ThreadX_Init(void)
{
  /* USER CODE BEGIN  Before_Kernel_Start */

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

/* USER CODE END 1 */
