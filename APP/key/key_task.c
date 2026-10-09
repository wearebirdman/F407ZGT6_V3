#include "key_task.h"
#include "key.h"
#include "led.h"

#define KEY_SCAN_PERIOD_MS  10  /* 按键扫描周期（ms） */

/* 按键任务入口 */
void Key_Proc(void *argument)
{
    (void)argument;

    KeyMsg_t key_msg;

    Key_Init();

    for (;;)
    {
        key_msg = Key_Scan();

        /* 按键事件入队：KeyQueue 生产端示例（消费端按需 osMessageQueueGet 取出） */
        if (key_msg.event != KEY_EVENT_NONE)
            osMessageQueuePut(KeyQueueHandle, &key_msg, 0, 0); /* 满则丢 */

        switch (key_msg.key_id)
        {
        case KEY_1_ID:
            if (key_msg.event == KEY_EVENT_SHORT_PRESS)
            {
                /* 按键1短按：向 LedQueue 发送翻转 LED1 请求，由 Led_Disp 任务消费执行 */
                LedMsg_t led_msg = {LED_1_ID, LED_TOGGLE};
                osMessageQueuePut(LedQueueHandle, &led_msg, 0, 0); /* 满则丢 */
            }
            break;
        case KEY_2_ID:
            if (key_msg.event == KEY_EVENT_SHORT_PRESS)
            {
                /* 按键2短按：向 LedQueue 发送翻转 LED2 请求，由 Led_Disp 任务消费执行 */
                LedMsg_t led_msg = {LED_2_ID, LED_TOGGLE};
                osMessageQueuePut(LedQueueHandle, &led_msg, 0, 0); /* 满则丢 */
            }
            break;
        case KEY_3_ID:
            if (key_msg.event == KEY_EVENT_SHORT_PRESS)
            {
                /* 按键3短按：向 LedQueue 发送翻转 LED3 请求，由 Led_Disp 任务消费执行 */
                LedMsg_t led_msg = {LED_3_ID, LED_TOGGLE};
                osMessageQueuePut(LedQueueHandle, &led_msg, 0, 0); /* 满则丢 */
            }
            break;
        case KEY_NONE_ID:
            break;
        default:
            break;
        }
        osDelay(KEY_SCAN_PERIOD_MS);  /* 按扫描周期轮询按键 */
    }
}
