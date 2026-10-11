#include "lcd_task.h"
#include "lcd.h"
#include "lvgl.h"
#include "lvgl_port.h"

#if LV_USE_DEMO_WIDGETS
    #include "lv_demo_widgets.h"
#endif

#define LVGL_TASK_MIN_DELAY_MS 5  /* lv_timer_handler 返回等待下限（ms） */
#define LVGL_TASK_MAX_DELAY_MS 33 /* 等待上限（ms），与 LV_DEF_REFR_PERIOD 对齐 */

/* LCD 显示任务入口：LVGL 渲染心跳 */
void Lcd_Disp(void *argument)
{
    uint32_t wait_ms;

    (void)argument;

    Lcd_Init();        /* LCD 硬件初始化 */
    Lvgl_PortInit();   /* LVGL 移植初始化 */

#if LV_USE_DEMO_WIDGETS
    /* 简化版控件演示页。换自有 UI 步骤：lv_conf.h 关 LV_USE_DEMO_WIDGETS →
     * 删除本块与 demos 目录 → eide.yml 移除 demos include 路径（池 32KB 可按 UI 占用再压） */
    lv_demo_widgets();
#endif

    for (;;)
    {
        wait_ms = lv_timer_handler();
        if (wait_ms < LVGL_TASK_MIN_DELAY_MS)
            wait_ms = LVGL_TASK_MIN_DELAY_MS;
        if (wait_ms > LVGL_TASK_MAX_DELAY_MS)
            wait_ms = LVGL_TASK_MAX_DELAY_MS;

        osDelay(wait_ms);
    }
}
