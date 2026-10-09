#include "lcd_task.h"
#include "lcd.h"

#define LCD_DISP_PERIOD_MS 100

/* LCD 显示任务入口 */
void Lcd_Disp(void *argument)
{
    (void)argument;

    Lcd_Init();        /* LCD 硬件初始化 */

    for (;;)
    {
        osDelay(LCD_DISP_PERIOD_MS);
    }
}
