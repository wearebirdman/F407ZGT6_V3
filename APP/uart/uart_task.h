#ifndef __UART_TASK_H__
#define __UART_TASK_H__

#include "main.h"
#include "app_objects.h"

/* 接收行缓冲上限（单条无 \n 行超过此长度则截断丢弃） */
#define UART_RX_LINE_MAX 128

/* 函数接口 */
void Uart_Proc(void *argument); /* RX 任务：取帧按 \n 组行并镜像 [Rx] 上屏 */

#endif /* __UART_TASK_H__ */
