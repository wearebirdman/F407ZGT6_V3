#include "led_task.h"
#include "led.h"

/* LED显示任务入口 */
void Led_Disp(void *argument)
{
    (void)argument;

    LedMsg_t led_msg;

    for (;;)
    {
        osMessageQueueGet(LedQueueHandle, &led_msg, NULL, osWaitForever);  /* 阻塞等待 LED 请求 */
        Led_Refresh(led_msg);
    }
}
