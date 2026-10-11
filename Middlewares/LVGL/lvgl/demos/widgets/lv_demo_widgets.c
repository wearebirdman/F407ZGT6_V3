/* 简化控件演示：替换官方 widgets demo（官方版含 4 张位图资源约 900KB，且硬性要求内存池 ≥38KB）
 * 本 demo 纵向排布 5 个条目，覆盖模板工程全部移植链路：
 *   标题标签        —— 验证渲染与默认字体
 *   触摸坐标标签    —— 按下时显示坐标（验证触摸校准结果）
 *   计数按钮        —— 验证点击事件
 *   滑块 + 进度条   —— 验证拖动事件与控件联动
 *   开关 + LED      —— 验证状态切换事件
 * 自有 UI 落地后：lv_conf.h 关 LV_USE_DEMO_WIDGETS、删本目录、eide.yml 移除 demos include 路径 */

#include "lv_demo_widgets.h"

#if LV_USE_DEMO_WIDGETS

/* ========== 演示参数 ========== */
#define DEMO_PAD          8U   /* 根容器内边距与行间距（px） */
#define DEMO_SLIDER_RANGE 100U /* 滑块/进度条满量程 */
#define DEMO_LED_SIZE     16U  /* LED 直径（px） */
#define DEMO_SWITCH_W     44U  /* 开关宽（px） */
#define DEMO_SWITCH_H     22U  /* 开关高（px） */
#define DEMO_BAR_H        10U  /* 进度条高（px） */

/* ========== 私有变量 ========== */
static lv_obj_t * s_TouchLabel;   /* 触摸坐标显示标签 */
static lv_obj_t * s_ClickLabel;   /* 按钮内文本标签 */
static lv_obj_t * s_Bar;          /* 与滑块联动的进度条 */
static lv_obj_t * s_Led;          /* 与开关联动的 LED */
static uint32_t   s_ClickCount;   /* 按钮累计点击次数 */

/* ========== 私有函数 ========== */

/* 计数按钮事件：按下/松开刷新触摸坐标显示，点击累加计数 */
static void LvDemo_BtnEventCb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED)
    {
        lv_point_t point;
        lv_indev_get_point(lv_event_get_indev(e), &point);
        lv_label_set_text_fmt(s_TouchLabel, "Touch: (%d, %d)", (int)point.x, (int)point.y);
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
        lv_label_set_text(s_TouchLabel, "Touch: released");
    }
    else if (code == LV_EVENT_CLICKED)
    {
        s_ClickCount++;
        lv_label_set_text_fmt(s_ClickLabel, "Click: %lu", s_ClickCount);
    }
}

/* 滑块事件：值变化同步到进度条 */
static void LvDemo_SliderEventCb(lv_event_t * e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        lv_bar_set_value(s_Bar, lv_slider_get_value((lv_obj_t *)lv_event_get_target(e)), LV_ANIM_ON);
    }
}

/* 开关事件：选中状态驱动 LED 亮灭 */
static void LvDemo_SwitchEventCb(lv_event_t * e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        lv_obj_t * sw = (lv_obj_t *)lv_event_get_target(e);

        if (lv_obj_has_state(sw, LV_STATE_CHECKED))
            lv_led_on(s_Led);
        else
            lv_led_off(s_Led);
    }
}

/* ========== 公共接口 ========== */

/* 创建简化控件演示页（挂载到当前活动屏幕） */
void lv_demo_widgets(void)
{
    lv_obj_t * root = lv_screen_active();
    lv_obj_t * title;
    lv_obj_t * btn;
    lv_obj_t * slider;
    lv_obj_t * row;
    lv_obj_t * sw;

    /* 根容器：整屏、纵向弹性布局、禁滚动、深色底 */
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_scrollable(root, false);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, DEMO_PAD, 0);
    lv_obj_set_style_pad_row(root, DEMO_PAD, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x101828), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(root, lv_color_white(), 0);

    /* 标题 */
    title = lv_label_create(root);
    lv_label_set_text(title, "LVGL Demo (Simple)");
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    /* 触摸坐标标签（按下计数按钮时更新） */
    s_TouchLabel = lv_label_create(root);
    lv_label_set_text(s_TouchLabel, "Touch: press btn");
    lv_obj_set_width(s_TouchLabel, LV_PCT(100));
    lv_obj_set_style_text_align(s_TouchLabel, LV_TEXT_ALIGN_CENTER, 0);

    /* 计数按钮 */
    s_ClickCount = 0;
    btn = lv_button_create(root);
    lv_obj_set_size(btn, LV_PCT(100), LV_SIZE_CONTENT);
    s_ClickLabel = lv_label_create(btn);
    lv_label_set_text(s_ClickLabel, "Click: 0");
    lv_obj_center(s_ClickLabel);
    lv_obj_add_event_cb(btn, LvDemo_BtnEventCb, LV_EVENT_ALL, NULL);

    /* 滑块 + 进度条联动 */
    slider = lv_slider_create(root);
    lv_obj_set_width(slider, LV_PCT(100));
    lv_slider_set_range(slider, 0, DEMO_SLIDER_RANGE);
    lv_slider_set_value(slider, DEMO_SLIDER_RANGE / 2U, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, LvDemo_SliderEventCb, LV_EVENT_VALUE_CHANGED, NULL);

    s_Bar = lv_bar_create(root);
    lv_obj_set_size(s_Bar, LV_PCT(100), DEMO_BAR_H);
    lv_bar_set_value(s_Bar, DEMO_SLIDER_RANGE / 2U, LV_ANIM_ON);

    /* 开关 + LED 同行 */
    row = lv_obj_create(root);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    sw = lv_switch_create(row);
    lv_obj_set_size(sw, DEMO_SWITCH_W, DEMO_SWITCH_H);
    lv_obj_add_event_cb(sw, LvDemo_SwitchEventCb, LV_EVENT_VALUE_CHANGED, NULL);

    s_Led = lv_led_create(row);
    lv_obj_set_size(s_Led, DEMO_LED_SIZE, DEMO_LED_SIZE);
    lv_led_set_color(s_Led, lv_palette_main(LV_PALETTE_GREEN));
    lv_led_off(s_Led);
}

#endif /* LV_USE_DEMO_WIDGETS */
