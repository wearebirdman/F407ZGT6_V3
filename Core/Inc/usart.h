/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.h
  * @brief   This file contains all the function prototypes for
  *          the usart.c file
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
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __USART_H__
#define __USART_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern UART_HandleTypeDef huart1;

/* USER CODE BEGIN Private defines */

/* 串口缓冲定义宏 */
#define UART_PRINTF_BUF_MAX 128 /* Uart_Printf 单次格式化缓冲上限 */
#define UART_RX_BUF_MAX     64  /* 接收双缓冲单块容量 */

/* USER CODE END Private defines */

void MX_USART1_UART_Init(void);

/* USER CODE BEGIN Prototypes */
void Uart_Printf(const char *fmt, ...);   /* printf 风格调试发送：互斥锁整行原子，防任务间交错 */
uint8_t *Uart_RxFetchFrame(uint16_t *len); /* 取出一帧就绪接收数据并清就绪标志，无则返回 NULL */
void Uart_SetQuiet(uint8_t quiet);        /* 设置 Uart_Printf 静默开关（1 = 丢弃输出） */
void Uart_RxStop(void);                   /* 停止 DMA 不定长接收（通道移交独占协议） */
void Uart_RxRestart(void);                /* 恢复 DMA 空闲帧接收 */
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __USART_H__ */

