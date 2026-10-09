#ifndef __APP_OBJECTS_H__
#define __APP_OBJECTS_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "cmsis_os.h"

/* ========== FreeRTOS 内核对象句柄（freertos.c 中由 CubeMX 创建） ========== */

/* 消息队列 */
extern osMessageQueueId_t KeyQueueHandle;   /* 按键事件队列（key_task 生产） */
extern osMessageQueueId_t LedQueueHandle;   /* LED 请求队列（led_task 消费） */
extern osMessageQueueId_t TouchQueueHandle; /* 触摸事件队列（touch_task 生产） */

/* 互斥锁 */
extern osMutexId_t Uart1TxMutexHandle;      /* USART1 发送互斥锁 */

/* 信号量 */
extern osSemaphoreId_t Uart1RxSemHandle;    /* USART1 接收帧计数信号量 */

#ifdef __cplusplus
}
#endif

#endif /* __APP_OBJECTS_H__ */
