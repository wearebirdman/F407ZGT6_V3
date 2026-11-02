#include "touch_task.h"
#include "lcd.h"
#include "lcd_task.h"
#include "touch.h"

#define TEV_MOVE_MIN          2   /* 行笔位移阈值（px）：HOLD 事件抖动过滤 */
#define TOUCH_SCAN_PERIOD_MS  10  /* 触摸扫描周期（ms） */
#define CAL_MARGIN            20  /* 校准点四角内缩量（px） */
#define CAL_POINT_TIMEOUT_MS  30000U /* 单个校准点等待超时（ms） */
#define CAL_SETTLE_MS         30  /* 按下/抬起稳定延时（ms） */

/* 诊断心跳：扫描循环计数 + 已入队事件计数（net diag 命令读取） */
volatile uint32_t g_TouchBeat   = 0;
volatile uint32_t g_TouchEvtCnt = 0;

/* 触摸任务就绪标志（见 touch_task.h） */
volatile uint8_t g_TouchReady = 0;

/* 校准取点回调：屏幕画十字 → 等待按下 → 采样原始 AD → 等待抬起 */
static uint8_t Touch_CalGetPoint(uint16_t px, uint16_t py, uint16_t *raw_x, uint16_t *raw_y)
{
    uint32_t t0;

    /* 提示界面：十字中心即目标触摸点 */
    Lcd_Clear(WHITE);
    Lcd_ShowString(0, 8, LCD_WIDTH, 16, 16, "TOUCH CALIBRATION", RED, WHITE);
    Lcd_ShowString(0, LCD_HEIGHT - 24, LCD_WIDTH, 16, 16, "TOUCH THE CROSS", BLUE, WHITE);
    Lcd_DrawLine((uint16_t)(px - 10), py, (uint16_t)(px + 10), py, RED);
    Lcd_DrawLine(px, (uint16_t)(py - 10), px, (uint16_t)(py + 10), RED);
    Lcd_DrawCircle(px, py, 8, RED);

    /* 等待笔按下（超时视为放弃校准） */
    t0 = HAL_GetTick();
    while (!Touch_IsPressed())
    {
        if (HAL_GetTick() - t0 > CAL_POINT_TIMEOUT_MS)
            return 0;
        osDelay(5);
    }

    /* 笔压稳定后读取原始坐标 */
    osDelay(CAL_SETTLE_MS);
    if (Touch_ReadRawXY(raw_x, raw_y) != TOUCH_OK)
        return 0;

    /* 等待抬起，避免同一次触摸被下一个校准点复用 */
    t0 = HAL_GetTick();
    while (Touch_IsPressed())
    {
        if (HAL_GetTick() - t0 > CAL_POINT_TIMEOUT_MS)
            return 0;
        osDelay(5);
    }
    osDelay(CAL_SETTLE_MS);
    return 1;
}

/* 首次校准：无有效校准参数时引导用户点 4 个角点，成功后存 EEPROM（须在 LVGL 启动前调用） */
void Touch_TaskCalibrate(void)
{
    TouchCal_t cal;
    TouchErr_t ret;

    Touch_GetCalibration(&cal);
    if (cal.valid)
        return; /* 已有有效校准（EEPROM 中加载），跳过 */

    ret = Touch_Adjust(LCD_WIDTH, LCD_HEIGHT, CAL_MARGIN, Touch_CalGetPoint);

    Lcd_Clear(WHITE);
    if (ret == TOUCH_OK)
    {
        Lcd_ShowString(0, LCD_HEIGHT / 2 - 8, LCD_WIDTH, 16, 16, "CAL OK", GREEN, WHITE);
#if TOUCH_USE_EEPROM_CAL
        Touch_SaveCalibration(); /* 保存失败不影响本次运行，下次开机重新校准 */
#endif
        osDelay(500);
    }
    else
    {
        Lcd_ShowString(0, LCD_HEIGHT / 2 - 8, LCD_WIDTH, 16, 16, "CAL FAILED", RED, WHITE);
        osDelay(500);
    }
    Lcd_Clear(BLACK);
}

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

    /* 等 LCD 初始化完成：首次校准要独占屏幕与触摸，必须在 LVGL 启动前完成 */
    while (!g_LcdReady)
        osDelay(10);
    Touch_TaskCalibrate();
    g_TouchReady = 1; /* 通知 LCD 任务可以启动 LVGL 了 */

    for (;;)
    {
        touch_msg = Touch_Scan();

        if (touch_msg.event == TOUCH_EVENT_PRESS_DOWN)
        {
            /* 按下边沿：直接转发，记录位移基准 */
            lx = touch_msg.x;
            ly = touch_msg.y;
            osMessageQueuePut(TouchQueueHandle, &touch_msg, 0, 0); /* 满则丢 */
            g_TouchEvtCnt++;
        }
        else if (touch_msg.event == TOUCH_EVENT_PRESS_UP)
        {
            /* 抬起边沿：直接转发（驱动已保留最后按下坐标） */
            osMessageQueuePut(TouchQueueHandle, &touch_msg, 0, 0); /* 满则丢 */
            g_TouchEvtCnt++;
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
                g_TouchEvtCnt++;
            }
        }
        g_TouchBeat++;
        osDelay(TOUCH_SCAN_PERIOD_MS);
    }
}
