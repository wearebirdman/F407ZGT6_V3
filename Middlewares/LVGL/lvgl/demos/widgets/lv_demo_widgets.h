#ifndef LV_DEMO_WIDGETS_H
#define LV_DEMO_WIDGETS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../lvgl.h"

#if LV_USE_DEMO_WIDGETS

/* 简化控件演示（模板工程维护版）：替换官方 widgets demo，验证显示/触摸/事件链路
 * 仅依赖默认字体 Montserrat 14，内置内存池 32KB 即可运行 */
void lv_demo_widgets(void);

#endif /* LV_USE_DEMO_WIDGETS */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LV_DEMO_WIDGETS_H */
