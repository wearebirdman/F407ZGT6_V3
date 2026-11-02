#include "lcd_task.h"
#include "lcd.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "touch_task.h"

#if LV_USE_DEMO_WIDGETS
    #include "lv_demo_widgets.h"
#endif

#define LVGL_TASK_MIN_DELAY_MS 5  /* lv_timer_handler 返回等待下限（ms） */
#define LVGL_TASK_MAX_DELAY_MS 33 /* 等待上限（ms），与 LV_DEF_REFR_PERIOD 对齐 */

/* 诊断心跳：LVGL 渲染循环计数（net diag 命令读取） */
volatile uint32_t g_LvglBeat = 0;

/* LCD 初始化完成标志（见 lcd_task.h） */
volatile uint8_t g_LcdReady = 0;

/* LCD 显示任务入口：LVGL 渲染心跳 */
void Lcd_Disp(void *argument)
{
    uint32_t wait_ms;

    (void)argument;

    Lcd_Init();      /* LCD 硬件初始化 */
    g_LcdReady = 1;  /* 通知触摸任务：屏幕可用，可做首次触摸校准 */

    /* 等触摸任务完成校准再启动 LVGL，避免校准画面与 LVGL 渲染互相踩屏幕 */
    while (!g_TouchReady)
        osDelay(10);

    Lvgl_PortInit(); /* LVGL 移植初始化 */

#if LV_USE_DEMO_WIDGETS
    /* 简化版控件演示页。换自有 UI 步骤：lv_conf.h 关 LV_USE_DEMO_WIDGETS →
     * 删除本块与 demos 目录 → eide.yml 移除 demos include 路径（池 32KB 可按 UI 占用再压） */
    lv_demo_widgets();
#endif

    for (;;)
    {
        wait_ms = lv_timer_handler();
        g_LvglBeat++;
        if (wait_ms < LVGL_TASK_MIN_DELAY_MS)
            wait_ms = LVGL_TASK_MIN_DELAY_MS;
        if (wait_ms > LVGL_TASK_MAX_DELAY_MS)
            wait_ms = LVGL_TASK_MAX_DELAY_MS;

        osDelay(wait_ms);
    }
}
