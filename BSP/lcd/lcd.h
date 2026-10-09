#ifndef __LCD_H
#define __LCD_H

#include "main.h"

/* LCD 引脚定义宏 */
#define LCD_RST_PORT   GPIOD       /* 硬件复位引脚（普通 GPIO） */
#define LCD_RST_PIN    GPIO_PIN_11
#define LCD_RST_HIGH() HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET)
#define LCD_RST_LOW()  HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET)

#define LCD_BL_PORT  GPIOG         /* 背光控制引脚 */
#define LCD_BL_PIN   GPIO_PIN_1
#define LCD_BL_ON()  HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_SET)
#define LCD_BL_OFF() HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_RESET)

/* LCD 地址结构体 */
typedef struct {
    volatile uint16_t LCD_REG;
    volatile uint16_t LCD_RAM;
} LCD_TypeDef;

/*
 * 使用NOR/SRAM的 Bank1.sector4，地址位HADDR[27,26]=11，A6作为数据命令区分线
 * 注意：设置时STM32内部会右移一位对齐! 111 1110=0X7E
 */
#define LCD_BASE ((uint32_t)(0x6C000000 | 0x0000007E))
#define LCD      ((LCD_TypeDef *)LCD_BASE)

/* LCD 屏幕尺寸定义 */
#define LCD_WIDTH  240
#define LCD_HEIGHT 320

/* 颜色定义（RGB565） */
#define WHITE   0xFFFF /* 白色 */
#define BLACK   0x0000 /* 黑色 */
#define BLUE    0x001F /* 蓝色 */
#define RED     0xF800 /* 红色 */
#define GREEN   0x07E0 /* 绿色 */
#define YELLOW  0xFFE0 /* 黄色 */
#define CYAN    0x7FFF /* 青色 */
#define MAGENTA 0xF81F /* 品红/紫红 */
#define ORANGE  0xFD20 /* 橙色 */

/* 灰度色定义 */
#define GRAY  0x8430 /* 灰色 */
#define LGRAY 0xC618 /* 浅灰色 */
#define DGRAY 0x4208 /* 深灰色 */

/* 常用混合色定义 */
#define BROWN  0xBC40 /* 棕色 */
#define PINK   0xFE19 /* 粉色 */
#define VIOLET 0x801F /* 紫罗兰 */

/* GUI 配色定义 */
#define DARKBLUE   0x01CF /* 深蓝色 */
#define LIGHTBLUE  0x7D7C /* 浅蓝色 */
#define GRAYBLUE   0x5458 /* 灰蓝色 */
#define LIGHTGREEN 0x841F /* 浅绿色 */
#define LGRAYBLUE  0xA651 /* 浅灰蓝色 */
#define LBBLUE     0x2B12 /* 浅棕蓝色 */

/* 函数接口 */
void Lcd_Init(void);                                                                         /* 初始化 */
void Lcd_DisplayOn(void);                                                                    /* 开显示 */
void Lcd_DisplayOff(void);                                                                   /* 关显示 */
void Lcd_Clear(uint16_t color);                                                              /* 清屏 */
void Lcd_Fill(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);      /* 填充单色 */
void Lcd_FillBitmap(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                    const uint16_t *data);                                                   /* 位图刷写（LVGL flush） */
uint16_t Lcd_ReadPoint(uint16_t x, uint16_t y);                                              /* 读取某点的颜色值 */
void Lcd_DrawPoint(uint16_t x, uint16_t y, uint16_t color);                                  /* 画点 */
void Lcd_DrawBigPoint(uint16_t x, uint16_t y, uint16_t color);                               /* 画大点 */
void Lcd_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);       /* 画线 */
void Lcd_DrawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);  /* 画矩形 */
void Lcd_DrawCircle(uint16_t x, uint16_t y, uint8_t r, uint16_t color);                      /* 画圆 */
void Lcd_ShowChar(uint16_t x, uint16_t y, uint8_t ch, uint8_t size,
                  uint16_t color, uint16_t bg_color);                                        /* 显示字符 */
void Lcd_ShowString(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                    uint8_t size, const char *str, uint16_t color, uint16_t bg_color);       /* 显示字符串 */
void Lcd_ShowNum(uint16_t x, uint16_t y, uint32_t num, uint8_t len,
                 uint8_t size, uint16_t color, uint16_t bg_color);                           /* 显示数字（高位不显示0） */
void Lcd_ShowxNum(uint16_t x, uint16_t y, uint32_t num, uint8_t len,
                  uint8_t size, uint8_t mode, uint16_t color, uint16_t bg_color);            /* 显示数字（支持补零/叠加） */

#endif /* __LCD_H */
