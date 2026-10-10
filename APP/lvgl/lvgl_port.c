#include "lvgl_port.h"
#include "app_objects.h"
#include "cmsis_os.h"
#include "lcd.h"
#include "lvgl.h"
#include "touch.h"

/* ========== 配置宏 ========== */
#define LVGL_DRAW_BUF_LINES 80 /* 分区绘制缓冲高度（行），240x80x2 = 37.5KB */
#define LVGL_COLOR_BYTES    2  /* RGB565 每像素字节数 */

/* ========== 私有变量 ========== */
/* 绘制缓冲放 CCMRAM 段：FSMC 为 CPU 直写、无 DMA 依赖，可安全使用 CCM，不挤占主 SRAM */
static uint8_t s_LvglBuf[LCD_WIDTH * LVGL_DRAW_BUF_LINES * LVGL_COLOR_BYTES]
    __attribute__((section(".ccmram"), aligned(4)));

static uint16_t s_TouchX    = 0; /* 最近一次触摸点 X（屏幕坐标） */
static uint16_t s_TouchY    = 0; /* 最近一次触摸点 Y */
static uint8_t  s_TouchDown = 0; /* 当前是否处于按下状态 */

/* ========== 私有函数 ========== */

/* LVGL flush 回调：将脏区 RGB565 位图经 FSMC 刷写到 LCD */
static void Lvgl_FlushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint16_t x = (uint16_t)area->x1;
    uint16_t y = (uint16_t)area->y1;
    uint16_t width = (uint16_t)(area->x2 - area->x1 + 1);
    uint16_t height = (uint16_t)(area->y2 - area->y1 + 1);

    Lcd_FillBitmap(x, y, width, height, (const uint16_t *)px_map);
    lv_display_flush_ready(disp);
}

/* LVGL 触摸读回调：非阻塞排空触摸事件队列，保留最新坐标与按压状态 */
static void Lvgl_TouchReadCb(lv_indev_t *indev, lv_indev_data_t *data)
{
    TouchMsg_t msg;

    (void)indev;

    while (osMessageQueueGet(TouchQueueHandle, &msg, NULL, 0) == osOK)
    {
        if (msg.event == TOUCH_EVENT_PRESS_DOWN || msg.event == TOUCH_EVENT_PRESS_HOLD)
        {
            s_TouchX = msg.x;
            s_TouchY = msg.y;
            s_TouchDown = 1;
        }
        else if (msg.event == TOUCH_EVENT_PRESS_UP)
        {
            s_TouchDown = 0;
        }
    }

    data->point.x = (int32_t)s_TouchX;
    data->point.y = (int32_t)s_TouchY;
    data->state = s_TouchDown ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* ========== 公共函数 ========== */

/* LVGL 移植初始化：核心初始化 + 显示/触摸设备注册，需在 Lcd_Init 之后调用 */
void Lvgl_PortInit(void)
{
    lv_display_t *disp;
    lv_indev_t   *indev;

    lv_init();
    lv_tick_set_cb(HAL_GetTick); /* 时基走 HAL（TIM 驱动），SysTick 归 FreeRTOS */

    disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    lv_display_set_buffers(disp, s_LvglBuf, NULL, sizeof(s_LvglBuf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, Lvgl_FlushCb);

    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, Lvgl_TouchReadCb);
}
