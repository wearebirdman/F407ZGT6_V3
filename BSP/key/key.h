#ifndef __KEY_H
#define __KEY_H

#include "main.h"

/* 按键定义宏 */
#define KEY_NUM             4     /* 始终为最大按键ID+1 */
#define KEY_WK_UNUSED       1     /* 未使用WakeUp按键为1,按键扫描中从1开始扫描 */
#define KEY_WK_PORT         GPIOA
#define KEY_WK_PIN          GPIO_PIN_0
#define KEY_WK_ACTIVE_LEVEL GPIO_PIN_SET
#define KEY_1_PORT          GPIOE
#define KEY_1_PIN           GPIO_PIN_4
#define KEY_1_ACTIVE_LEVEL  GPIO_PIN_RESET
#define KEY_2_PORT          GPIOE
#define KEY_2_PIN           GPIO_PIN_3
#define KEY_2_ACTIVE_LEVEL  GPIO_PIN_RESET
#define KEY_3_PORT          GPIOE
#define KEY_3_PIN           GPIO_PIN_2
#define KEY_3_ACTIVE_LEVEL  GPIO_PIN_RESET

/* 按键事件时间定义 */
#define KEY_DEBOUNCE_MS     10     /* 消抖时间 */
#define KEY_LONG_PRESS_MS   2000   /* 长按时间 */
#define KEY_DOUBLE_CLICK_MS 200    /* 双击间隔时间 */

/* 按键ID定义 */
typedef enum {
    KEY_NONE_ID = 255,
    KEY_WK_ID = 0,
    KEY_1_ID = 1,
    KEY_2_ID = 2,
    KEY_3_ID = 3,
} KeyID_t;

/* 按键事件类型定义 */
typedef enum {
    KEY_EVENT_NONE = 0,
    KEY_EVENT_SHORT_PRESS = 1,
    KEY_EVENT_LONG_PRESS = 2,
    KEY_EVENT_DOUBLE_CLICK = 3,
} KeyEvt_t;

/* 按键事件结构体 */
typedef struct {
    uint8_t  key_id;
    uint8_t  event;
} KeyMsg_t;

/* 函数接口 */
void Key_Init(void);      /* 初始化按键上下文 */
KeyMsg_t Key_Scan(void);  /* 扫描按键状态机，返回本次事件 */

#endif /* __KEY_H */
