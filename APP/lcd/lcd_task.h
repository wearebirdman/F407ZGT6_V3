#ifndef __LCD_TASK_H
#define __LCD_TASK_H

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

void Lcd_Disp(void *argument);    /* LCD 显示任务入口 */

#endif /* __LCD_TASK_H */
