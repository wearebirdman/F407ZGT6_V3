/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.c
  * @brief   This file provides code for the configuration
  *          of the USART instances.
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
#include "usart.h"

/* USER CODE BEGIN 0 */
#include "app_objects.h"

#include <stdarg.h>
#include <stdio.h>

static uint8_t s_RxBufA[UART_RX_BUF_MAX]; /* 接收双缓冲 A */
static uint8_t s_RxBufB[UART_RX_BUF_MAX]; /* 接收双缓冲 B */
static volatile uint16_t s_RxLenA;        /* A 的有效字节数（ISR 写、任务读） */
static volatile uint16_t s_RxLenB;        /* B 的有效字节数（ISR 写、任务读） */
static volatile uint8_t s_RxReadyA;       /* 1 = A 已满待消费 */
static volatile uint8_t s_RxReadyB;       /* 1 = B 已满待消费 */
static volatile uint8_t s_RxActive;       /* 0 = DMA 正写 A，1 = DMA 正写 B */
static uint8_t s_Quiet;                   /* 1 = Uart_Printf 丢弃输出（独占传输期间防污染协议流） */
/* USER CODE END 0 */

UART_HandleTypeDef huart1;
DMA_HandleTypeDef hdma_usart1_rx;

/* USART1 init function */

void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */
  /* DMA 不定长接收（空闲中断），双缓冲先在 A 上启动 */
  s_RxActive = 0;
  HAL_UARTEx_ReceiveToIdle_DMA(&huart1, s_RxBufA, UART_RX_BUF_MAX);
  __HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);
  /* USER CODE END USART1_Init 2 */

}

void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspInit 0 */

  /* USER CODE END USART1_MspInit 0 */
    /* USART1 clock enable */
    __HAL_RCC_USART1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART1 GPIO Configuration
    PA9     ------> USART1_TX
    PA10     ------> USART1_RX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_9|GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* USART1 DMA Init */
    /* USART1_RX Init */
    hdma_usart1_rx.Instance = DMA2_Stream2;
    hdma_usart1_rx.Init.Channel = DMA_CHANNEL_4;
    hdma_usart1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart1_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart1_rx.Init.Mode = DMA_NORMAL;
    hdma_usart1_rx.Init.Priority = DMA_PRIORITY_LOW;
    hdma_usart1_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart1_rx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmarx,hdma_usart1_rx);

    /* USART1 interrupt Init */
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspInit 1 */

  /* USER CODE END USART1_MspInit 1 */
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{

  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspDeInit 0 */

  /* USER CODE END USART1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART1_CLK_DISABLE();

    /**USART1 GPIO Configuration
    PA9     ------> USART1_TX
    PA10     ------> USART1_RX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9|GPIO_PIN_10);

    /* USART1 DMA DeInit */
    HAL_DMA_DeInit(uartHandle->hdmarx);

    /* USART1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspDeInit 1 */

  /* USER CODE END USART1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
/* printf 重定向（GCC/newlib）：强定义覆盖 syscalls.c 的弱引用，被弱 _write 逐字符调用。
 * 符号名由工具链契约固定，不适用模块命名规范。
 * 任务级调试输出仍统一走 Uart_Printf（互斥锁整行原子）；
 * 本通道不加锁，供调度器未运行场景（启动早期、Error_Handler）兜底使用 */
/* int __io_putchar(int ch)
{
  uint8_t byte = (uint8_t)ch;
  HAL_UART_Transmit(&huart1, &byte, 1, HAL_MAX_DELAY);
  return ch;
} */

/* printf 风格调试发送：格式化后加互斥锁整行发送，防多任务输出交错 */
void Uart_Printf(const char *fmt, ...)
{
  char buf[UART_PRINTF_BUF_MAX];
  va_list args;
  int total;

  if (s_Quiet)
    return; /* 独占传输静默期：直接丢弃 */

  va_start(args, fmt);
  total = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (total <= 0)
    return;

  if (total > (int)(sizeof(buf) - 1))
    total = (int)(sizeof(buf) - 1); /* 超长截断，只发实际写入部分 */

  osMutexAcquire(Uart1TxMutexHandle, osWaitForever);
  HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)total, HAL_MAX_DELAY);
  osMutexRelease(Uart1TxMutexHandle);
}

/* DMA 空闲不定长接收回调：双缓冲切换，记录长度后 give 信号量通知 RX 任务 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
  if (huart == &huart1)
  {
    if (s_RxActive == 0)
    {
      s_RxLenA = size;
      s_RxReadyA = 1;
      s_RxActive = 1;
      HAL_UARTEx_ReceiveToIdle_DMA(&huart1, s_RxBufB, UART_RX_BUF_MAX);
    }
    else
    {
      s_RxLenB = size;
      s_RxReadyB = 1;
      s_RxActive = 0;
      HAL_UARTEx_ReceiveToIdle_DMA(&huart1, s_RxBufA, UART_RX_BUF_MAX);
    }
    __HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);
    osSemaphoreRelease(Uart1RxSemHandle);
  }
}

/* 取出一帧已就绪的接收数据并清除就绪标志；无就绪帧返回 NULL */
uint8_t *Uart_RxFetchFrame(uint16_t *len)
{
  if (s_RxReadyA)
  {
    s_RxReadyA = 0;
    *len = s_RxLenA;
    return s_RxBufA;
  }
  if (s_RxReadyB)
  {
    s_RxReadyB = 0;
    *len = s_RxLenB;
    return s_RxBufB;
  }
  *len = 0;
  return NULL;
}

/* 设置 Uart_Printf 静默开关（由独占传输的宿主任务调用）
 * quiet: 1 = 后续所有任务的 Uart_Printf 调用被丢弃，0 = 恢复输出 */
void Uart_SetQuiet(uint8_t quiet)
{
  s_Quiet = quiet;
}

/* 停止 DMA 不定长接收：接收通道交给 Ymodem 等独占协议
 * HAL_UART_DMAStop 内部已关闭 IDLE 中断并复位 RxState，不会再触发 RxEventCallback */
void Uart_RxStop(void)
{
  HAL_UART_DMAStop(&huart1);
}

/* 重新挂 DMA 空闲接收（A 缓冲），独占协议结束后恢复正常帧接收 */
void Uart_RxRestart(void)
{
  __HAL_UART_CLEAR_OREFLAG(&huart1); /* 清独占期可能残留的溢出/错误标志 */

  s_RxActive = 0;
  s_RxReadyA = 0;
  s_RxReadyB = 0;
  HAL_UARTEx_ReceiveToIdle_DMA(&huart1, s_RxBufA, UART_RX_BUF_MAX);
  __HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);
}
/* USER CODE END 1 */

