#ifndef __TOUCH_TASK_H
#define __TOUCH_TASK_H

#include "main.h"
#include "app_objects.h"

/* 触摸任务就绪标志：Touch_Init 完成且（必要时）首次校准结束，LCD 任务等待此标志后再启动 LVGL */
extern volatile uint8_t g_TouchReady;

void Touch_Proc(void *argument); /* 触摸任务入口 */

#endif /* __TOUCH_TASK_H */
