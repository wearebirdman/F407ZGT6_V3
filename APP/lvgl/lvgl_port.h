#ifndef __LVGL_PORT_H
#define __LVGL_PORT_H

#include "main.h"

void Lvgl_PortInit(void); /* LVGL 移植初始化：核心初始化 + 显示/触摸设备注册 */

/* 诊断：读取 LVGL 侧最近一次触摸状态（NULL 参数忽略对应输出） */
void Lvgl_GetTouchState(uint16_t *x, uint16_t *y, uint8_t *down);

#endif /* __LVGL_PORT_H */
