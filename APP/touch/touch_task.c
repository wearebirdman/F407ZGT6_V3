#include "touch_task.h"
#include "touch.h"

#define TEV_MOVE_MIN         2  /* 行笔位移阈值（px）：HOLD 事件抖动过滤 */
#define TOUCH_SCAN_PERIOD_MS 10 /* 触摸扫描周期（ms） */

/* 触摸任务：周期扫描触摸，将事件转发到触摸消息队列 */
void Touch_Proc(void *argument)
{
    (void)argument;

    TouchMsg_t touch_msg;
    uint16_t lx = 0, ly = 0; /* 最后上报的屏幕坐标（HOLD 位移判定基准） */

#if TOUCH_USE_EEPROM_CAL
    AT24Cxx_Init(); /* 检测 EEPROM 在位（I2C 句柄由 at24cxx.h 的 AT24CXX_I2C 宏指定） */
#endif
    Touch_Init(); /* 内部加载 EEPROM 校准参数（未校准过则输出原始 AD 值） */

    for (;;)
    {
        touch_msg = Touch_Scan();

        if (touch_msg.event == TOUCH_EVENT_PRESS_DOWN)
        {
            /* 按下边沿：直接转发，记录位移基准 */
            lx = touch_msg.x;
            ly = touch_msg.y;
            osMessageQueuePut(TouchQueueHandle, &touch_msg, 0, 0); /* 满则丢 */
        }
        else if (touch_msg.event == TOUCH_EVENT_PRESS_UP)
        {
            /* 抬起边沿：直接转发（驱动已保留最后按下坐标） */
            osMessageQueuePut(TouchQueueHandle, &touch_msg, 0, 0); /* 满则丢 */
        }
        else if (touch_msg.event == TOUCH_EVENT_PRESS_HOLD)
        {
            /* 按住：位移达标才转发（防抖 + 降消息流量） */
            uint16_t dx = (touch_msg.x > lx) ? (touch_msg.x - lx) : (lx - touch_msg.x);
            uint16_t dy = (touch_msg.y > ly) ? (touch_msg.y - ly) : (ly - touch_msg.y);
            if (dx >= TEV_MOVE_MIN || dy >= TEV_MOVE_MIN)
            {
                lx = touch_msg.x;
                ly = touch_msg.y;
                osMessageQueuePut(TouchQueueHandle, &touch_msg, 0, 0); /* 满则丢 */
            }
        }
        osDelay(TOUCH_SCAN_PERIOD_MS);
    }
}
