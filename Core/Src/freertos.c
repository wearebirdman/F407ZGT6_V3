/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "key.h"
#include "led.h"
#include "touch.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Key_Proc_Task */
osThreadId_t Key_Proc_TaskHandle;
const osThreadAttr_t Key_Proc_Task_attributes = {
  .name = "Key_Proc_Task",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for Led_Disp_Task */
osThreadId_t Led_Disp_TaskHandle;
const osThreadAttr_t Led_Disp_Task_attributes = {
  .name = "Led_Disp_Task",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for Lcd_Disp_Task */
osThreadId_t Lcd_Disp_TaskHandle;
const osThreadAttr_t Lcd_Disp_Task_attributes = {
  .name = "Lcd_Disp_Task",
  .stack_size = 1536 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for Touch_Proc_Task */
osThreadId_t Touch_Proc_TaskHandle;
const osThreadAttr_t Touch_Proc_Task_attributes = {
  .name = "Touch_Proc_Task",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for Uart_Proc_Task */
osThreadId_t Uart_Proc_TaskHandle;
const osThreadAttr_t Uart_Proc_Task_attributes = {
  .name = "Uart_Proc_Task",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for KeyQueue */
osMessageQueueId_t KeyQueueHandle;
const osMessageQueueAttr_t KeyQueue_attributes = {
  .name = "KeyQueue"
};
/* Definitions for LedQueue */
osMessageQueueId_t LedQueueHandle;
const osMessageQueueAttr_t LedQueue_attributes = {
  .name = "LedQueue"
};
/* Definitions for TouchQueue */
osMessageQueueId_t TouchQueueHandle;
const osMessageQueueAttr_t TouchQueue_attributes = {
  .name = "TouchQueue"
};
/* Definitions for Uart1TxMutex */
osMutexId_t Uart1TxMutexHandle;
const osMutexAttr_t Uart1TxMutex_attributes = {
  .name = "Uart1TxMutex"
};
/* Definitions for Uart1RxSem */
osSemaphoreId_t Uart1RxSemHandle;
const osSemaphoreAttr_t Uart1RxSem_attributes = {
  .name = "Uart1RxSem"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void Key_Proc(void *argument);
void Led_Disp(void *argument);
void Lcd_Disp(void *argument);
void Touch_Proc(void *argument);
void Uart_Proc(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* creation of Uart1TxMutex */
  Uart1TxMutexHandle = osMutexNew(&Uart1TxMutex_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* Create the semaphores(s) */
  /* creation of Uart1RxSem */
  Uart1RxSemHandle = osSemaphoreNew(2, 0, &Uart1RxSem_attributes);

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of KeyQueue */
  KeyQueueHandle = osMessageQueueNew (4, sizeof(KeyMsg_t), &KeyQueue_attributes);

  /* creation of LedQueue */
  LedQueueHandle = osMessageQueueNew (4, sizeof(LedMsg_t), &LedQueue_attributes);

  /* creation of TouchQueue */
  TouchQueueHandle = osMessageQueueNew (16, sizeof(TouchMsg_t), &TouchQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of Key_Proc_Task */
  Key_Proc_TaskHandle = osThreadNew(Key_Proc, NULL, &Key_Proc_Task_attributes);

  /* creation of Led_Disp_Task */
  Led_Disp_TaskHandle = osThreadNew(Led_Disp, NULL, &Led_Disp_Task_attributes);

  /* creation of Lcd_Disp_Task */
  Lcd_Disp_TaskHandle = osThreadNew(Lcd_Disp, NULL, &Lcd_Disp_Task_attributes);

  /* creation of Touch_Proc_Task */
  Touch_Proc_TaskHandle = osThreadNew(Touch_Proc, NULL, &Touch_Proc_Task_attributes);

  /* creation of Uart_Proc_Task */
  Uart_Proc_TaskHandle = osThreadNew(Uart_Proc, NULL, &Uart_Proc_Task_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* defaultTask 仅作占位无业务：立即注销自身，释放其栈与 TCB 内存 */

  osThreadTerminate(osThreadGetId());
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_Key_Proc */
/**
* @brief Function implementing the t_Key_Proc thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Key_Proc */
__weak void Key_Proc(void *argument)
{
  /* USER CODE BEGIN Key_Proc */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END Key_Proc */
}

/* USER CODE BEGIN Header_Led_Disp */
/**
* @brief Function implementing the t_Led_Disp thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Led_Disp */
__weak void Led_Disp(void *argument)
{
  /* USER CODE BEGIN Led_Disp */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END Led_Disp */
}

/* USER CODE BEGIN Header_Lcd_Disp */
/**
* @brief Function implementing the t_Lcd_Disp thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Lcd_Disp */
__weak void Lcd_Disp(void *argument)
{
  /* USER CODE BEGIN Lcd_Disp */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END Lcd_Disp */
}

/* USER CODE BEGIN Header_Touch_Proc */
/**
* @brief Function implementing the t_Touch_Proc thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Touch_Proc */
__weak void Touch_Proc(void *argument)
{
  /* USER CODE BEGIN Touch_Proc */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END Touch_Proc */
}

/* USER CODE BEGIN Header_Uart_Proc */
/**
* @brief Function implementing the Uart_Proc_Task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Uart_Proc */
__weak void Uart_Proc(void *argument)
{
  /* USER CODE BEGIN Uart_Proc */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END Uart_Proc */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

