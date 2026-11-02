#ifndef __LCD_TASK_H
#define __LCD_TASK_H

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* LCD 初始化完成标志：置位后触摸任务才可独占屏幕做首次校准 */
extern volatile uint8_t g_LcdReady;

void Lcd_Disp(void *argument);    /* LCD 显示任务入口 */

#endif /* __LCD_TASK_H */
