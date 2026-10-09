#ifndef __LED_H
#define __LED_H

#include "main.h"

/* LED定义宏 */
#define LED_1_PORT             GPIOF
#define LED_1_PIN              GPIO_PIN_8
#define LED_1_OFF_LEVEL        GPIO_PIN_SET
#define LED_1_ON_LEVEL         GPIO_PIN_RESET
#define LED_2_PORT             GPIOF
#define LED_2_PIN              GPIO_PIN_9
#define LED_2_OFF_LEVEL        GPIO_PIN_SET
#define LED_2_ON_LEVEL         GPIO_PIN_RESET
#define LED_3_PORT             GPIOF
#define LED_3_PIN              GPIO_PIN_10
#define LED_3_OFF_LEVEL        GPIO_PIN_SET
#define LED_3_ON_LEVEL         GPIO_PIN_RESET

/* LED ID枚举类型 */
typedef enum {
    LED_1_ID = 1,
    LED_2_ID = 2,
    LED_3_ID = 3,
} LedID_t;

/* LED状态枚举类型 */
typedef enum {
    LED_OFF = 0,
    LED_ON = 1,
    LED_TOGGLE = 2,
} LedState_t;

/* LED请求结构体 */
typedef struct {
    uint8_t  led_id;
    uint8_t  state;
} LedReq_t;

/* LED函数声明 */
void Led_On(LedID_t led_id);         /* 点亮指定LED */
void Led_Off(LedID_t led_id);        /* 熄灭指定LED */
void Led_Toggle(LedID_t led_id);     /* 反转指定LED */
void Led_Refresh(LedReq_t led_req);  /* 按请求刷新LED */

#endif /* __LED_H */
