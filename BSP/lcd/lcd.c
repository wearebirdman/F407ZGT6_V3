#include "lcd.h"
#include "lcd_font.h"

/* 屏幕方向配置结构体 */
typedef struct {
    uint8_t mv;  /* 0=竖屏, 1=横屏（行列交换） */
    uint8_t mx;  /* 0=从上到下, 1=从下到上（行反转） */
    uint8_t my;  /* 0=从左到右, 1=从右到左（列反转） */
    uint8_t bgr; /* 0=RGB, 1=BGR（颜色顺序） */
} LcdDir_t;

/* LCD 重要参数集结构体 */
typedef struct {
    uint16_t id;       /* LCD ID */
    uint16_t wramcmd;  /* 开始写GRAM指令 */
    uint16_t rdcmd;    /* 开始读GRAM指令 */
    LcdDir_t dir;      /* 屏幕方向配置 */
    uint16_t width;    /* LCD 宽度 */
    uint16_t height;   /* LCD 高度 */
    uint16_t setxcmd;  /* 设置X坐标指令 */
    uint16_t setycmd;  /* 设置Y坐标指令 */
} LcdDev_t;

/* 内部变量（仅本文件使用） */
static LcdDev_t s_LcdDev;

/* ===================== 底层寄存器操作 ===================== */

/* 写入 LCD 命令（寄存器序号） */
static void Lcd_WrCmd(uint16_t cmd)
{
    /* cmd = cmd; */    /* 使用-O2优化时，必须插入的延时 */
    LCD->LCD_REG = cmd; /* 写入要写的寄存器序号 */
}

/* 写入 LCD 数据 */
static void Lcd_WrData(uint16_t data)
{
    /* data = data; */   /* 使用-O2优化时，必须插入的延时 */
    LCD->LCD_RAM = data; /* 写入数据 */
}

/* 读取 LCD 数据 */
static uint16_t Lcd_RdData(void)
{
    return LCD->LCD_RAM; /* FSMC 直接读数据寄存器（原实现把值当地址解引用，是错误的） */
}

/* 写入 LCD 寄存器（寄存器序号 + 寄存器值） */
static void Lcd_WriteReg(uint16_t reg, uint16_t reg_value)
{
    Lcd_WrCmd(reg);        /* 写入要写的寄存器序号 */
    Lcd_WrData(reg_value); /* 写入数据 */
}

/* ===================== 命令 + 数据连续写入宏 ===================== */

/* 写命令 + 1 个数据 */
#define LCD_WR_CMD1(cmd, d1) \
    do                       \
    {                        \
        Lcd_WrCmd(cmd);      \
        Lcd_WrData(d1);      \
    } while (0)

/* 写命令 + 2 个数据 */
#define LCD_WR_CMD2(cmd, d1, d2) \
    do                           \
    {                            \
        Lcd_WrCmd(cmd);          \
        Lcd_WrData(d1);          \
        Lcd_WrData(d2);          \
    } while (0)

/* 写命令 + 3 个数据 */
#define LCD_WR_CMD3(cmd, d1, d2, d3) \
    do                               \
    {                                \
        Lcd_WrCmd(cmd);              \
        Lcd_WrData(d1);              \
        Lcd_WrData(d2);              \
        Lcd_WrData(d3);              \
    } while (0)

/* 写命令 + 4 个数据 */
#define LCD_WR_CMD4(cmd, d1, d2, d3, d4) \
    do                                   \
    {                                    \
        Lcd_WrCmd(cmd);                  \
        Lcd_WrData(d1);                  \
        Lcd_WrData(d2);                  \
        Lcd_WrData(d3);                  \
        Lcd_WrData(d4);                  \
    } while (0)

/* 写命令 + 5 个数据 */
#define LCD_WR_CMD5(cmd, d1, d2, d3, d4, d5) \
    do                                       \
    {                                        \
        Lcd_WrCmd(cmd);                      \
        Lcd_WrData(d1);                      \
        Lcd_WrData(d2);                      \
        Lcd_WrData(d3);                      \
        Lcd_WrData(d4);                      \
        Lcd_WrData(d5);                      \
    } while (0)

/* 写命令 + 15 个数据（用于 Gamma 校正） */
#define LCD_WR_CMD15(cmd, d1, d2, d3, d4, d5, d6, d7, d8, d9, d10, d11, d12, d13, d14, d15) \
    do                                                                                      \
    {                                                                                       \
        Lcd_WrCmd(cmd);                                                                     \
        Lcd_WrData(d1);                                                                     \
        Lcd_WrData(d2);                                                                     \
        Lcd_WrData(d3);                                                                     \
        Lcd_WrData(d4);                                                                     \
        Lcd_WrData(d5);                                                                     \
        Lcd_WrData(d6);                                                                     \
        Lcd_WrData(d7);                                                                     \
        Lcd_WrData(d8);                                                                     \
        Lcd_WrData(d9);                                                                     \
        Lcd_WrData(d10);                                                                    \
        Lcd_WrData(d11);                                                                    \
        Lcd_WrData(d12);                                                                    \
        Lcd_WrData(d13);                                                                    \
        Lcd_WrData(d14);                                                                    \
        Lcd_WrData(d15);                                                                    \
    } while (0)

/* 设置屏幕扫描方向（同步更新宽高与 X/Y 设置命令） */
static void Lcd_SetDirection(const LcdDir_t *cfg)
{
    s_LcdDev.dir = *cfg;

    uint8_t reg36 = 0x00;
    reg36 |= (cfg->my << 7);
    reg36 |= (cfg->mx << 6);
    reg36 |= (cfg->mv << 5);
    reg36 |= (cfg->bgr << 3);    /* BGR位在bit3 */

    Lcd_WriteReg(0x36, reg36);

    s_LcdDev.width  = (cfg->mv ? LCD_HEIGHT : LCD_WIDTH);
    s_LcdDev.height = (cfg->mv ? LCD_WIDTH : LCD_HEIGHT);

    /* 横屏或双翻转时需要交换 X/Y 设置命令 */
    uint8_t need_swap = cfg->mv || (cfg->my && cfg->mx);
    s_LcdDev.setxcmd = (need_swap ? 0x2B : 0x2A);
    s_LcdDev.setycmd = (need_swap ? 0x2A : 0x2B);
}

/* ILI9341 初始化序列 */
static void Lcd_InitILI9341(void)
{
    /* 1. 电源控制寄存器 */
    LCD_WR_CMD3(0xCF, 0x00, 0xC1, 0x30);              /* 电源控制 A */
    LCD_WR_CMD4(0xED, 0x64, 0x03, 0x12, 0x81);        /* 电源控制 B */
    LCD_WR_CMD3(0xE8, 0x85, 0x10, 0x7A);              /* 驱动时序控制 A */
    LCD_WR_CMD5(0xCB, 0x39, 0x2C, 0x00, 0x34, 0x02);  /* 电源控制 C */
    LCD_WR_CMD1(0xF7, 0x20);                          /* 泵比例控制 */
    LCD_WR_CMD2(0xEA, 0x00, 0x00);                    /* 驱动时序控制 B */

    /* 2. 电压/参考电压设置 */
    LCD_WR_CMD1(0xC0, 0x1B);        /* 电源控制 1（VGH/VGL 电压调节） */
    LCD_WR_CMD1(0xC1, 0x01);        /* 电源控制 2（电荷泵设置） */
    LCD_WR_CMD2(0xC5, 0x30, 0x30);  /* VCOM 电压控制 1 */
    LCD_WR_CMD1(0xC7, 0xB7);        /* VCOM 电压控制 2 */

    /* 3. 显示参数设置 */
    LCD_WR_CMD1(0x36, 0xFF);        /* 内存访问控制（BGR=1，扫描方向后续重设） */
    LCD_WR_CMD1(0x3A, 0x55);        /* 像素格式（RGB565，16位色） */
    LCD_WR_CMD2(0xB1, 0x00, 0x1A);  /* 帧率控制 */
    LCD_WR_CMD2(0xB6, 0x0A, 0xA2);  /* 显示功能控制 */
    LCD_WR_CMD1(0xF2, 0x00);        /* 禁用 3Gamma 功能 */

    /* 4. Gamma 校正 */
    LCD_WR_CMD1(0x26, 0x01);    /* 选择 Gamma 曲线 1 */
    /* 正 Gamma 校正 */
    LCD_WR_CMD15(0xE0, 0x0F, 0x2A, 0x28, 0x08, 0x0E, 0x08, 0x54, 0xA9, 0x43, 0x0A, 0x0F, 0x00, 0x00, 0x00, 0x00);
    /* 负 Gamma 校正 */
    LCD_WR_CMD15(0xE1, 0x00, 0x15, 0x17, 0x07, 0x11, 0x06, 0x2B, 0x56, 0x3C, 0x05, 0x10, 0x0F, 0x3F, 0x3F, 0x0F);

    /* 5. 设置显示窗口（全屏） */
    LCD_WR_CMD4(0x2B, 0x00, 0x00, 0x01, 0x3F);  /* 行地址（Y：0~319） */
    LCD_WR_CMD4(0x2A, 0x00, 0x00, 0x00, 0xEF);  /* 列地址（X：0~239） */

    /* 6. 唤醒并点亮屏幕 */
    Lcd_WrCmd(0x11);       /* 退出睡眠模式 */
    HAL_Delay(120);        /* 等待稳定（必须 ≥ 120ms） */
    Lcd_WrCmd(0x29);       /* 点亮屏幕 */
}

/* LCD 初始化 */
void Lcd_Init(void)
{
    /* 1. 硬件复位 */
    LCD_RST_LOW();
    HAL_Delay(50);
    LCD_RST_HIGH();
    HAL_Delay(50);

    /* 2. 背光打开 */
    LCD_BL_ON();

    /* 3. 初始化 ILI9341 */
    Lcd_InitILI9341();

    /* 4. 设置默认参数 */
    s_LcdDev.id      = 0x9341;
    s_LcdDev.wramcmd = 0x2C;
    s_LcdDev.rdcmd   = 0x2E;

    /*
     * 5. 设置默认扫描方向
     * 从左到右，从上到下，BGR=1
     * 竖屏
     */
    LcdDir_t lcddir;
    lcddir.mv  = 0;     /* 竖屏 */
    lcddir.mx  = 0;     /* 从上到下 */
    lcddir.my  = 0;     /* 从左到右 */
    lcddir.bgr = 1;     /* BGR 色序 */
    Lcd_SetDirection(&lcddir);

    Lcd_Clear(BLACK);   /* 清屏为黑色 */
}

/* 开启屏幕显示 */
void Lcd_DisplayOn(void)
{
    Lcd_WrCmd(0x29);      /* 开启显示 */
}

/* 关闭屏幕显示 */
void Lcd_DisplayOff(void)
{
    Lcd_WrCmd(0x28);      /* 关闭显示 */
}

/* 设置显示窗口（起始坐标 + 宽高） */
static void Lcd_SetWindow(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    uint16_t twidth  = x + width - 1;
    uint16_t theight = y + height - 1;

    Lcd_WrCmd(s_LcdDev.setxcmd);
    Lcd_WrData(x >> 8);
    Lcd_WrData(x & 0xFF);
    Lcd_WrData(twidth >> 8);
    Lcd_WrData(twidth & 0xFF);
    Lcd_WrCmd(s_LcdDev.setycmd);
    Lcd_WrData(y >> 8);
    Lcd_WrData(y & 0xFF);
    Lcd_WrData(theight >> 8);
    Lcd_WrData(theight & 0xFF);
}

/* 坐标越界检查，1 = 坐标越界, 0 = 坐标正常 */
static uint8_t Lcd_CoordError(uint16_t x, uint16_t y)
{
    return (x >= s_LcdDev.width || y >= s_LcdDev.height);
}

/* 清屏（color: RGB565） */
void Lcd_Clear(uint16_t color)
{
    uint32_t total = (uint32_t)s_LcdDev.width * s_LcdDev.height;
    __IO uint16_t *ram_addr = &LCD->LCD_RAM;    /* 缓存 LCD_RAM 地址，减少指针解引用开销 */

    Lcd_SetWindow(0, 0, s_LcdDev.width, s_LcdDev.height);

    LCD->LCD_REG = s_LcdDev.wramcmd;
    for (uint32_t index = 0; index < total; index++)
        *ram_addr = color;
}

/* 填充指定区域（起始坐标 + 宽高 + RGB565 颜色） */
void Lcd_Fill(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
    if (Lcd_CoordError(x, y) || Lcd_CoordError(x + width - 1, y + height - 1))
        return;

    /* 计算区域大小 */
    uint32_t total = (uint32_t)width * height;
    __IO uint16_t *ram_addr = &LCD->LCD_RAM;    /* 缓存 LCD_RAM 地址，减少指针解引用开销 */

    Lcd_SetWindow(x, y, width, height);

    LCD->LCD_REG = s_LcdDev.wramcmd;
    for (uint32_t i = 0; i < total; i++)
        *ram_addr = color;
}

/* 位图刷写：把 width*height 个 RGB565 像素写入指定窗口（LVGL flush 专用） */
void Lcd_FillBitmap(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint16_t *data)
{
    if (Lcd_CoordError(x, y) || Lcd_CoordError(x + width - 1, y + height - 1))
        return;

    uint32_t total = (uint32_t)width * height;
    __IO uint16_t *ram_addr = &LCD->LCD_RAM;    /* 缓存 LCD_RAM 地址，减少指针解引用开销 */

    Lcd_SetWindow(x, y, width, height);

    LCD->LCD_REG = s_LcdDev.wramcmd;
    for (uint32_t i = 0; i < total; i++)
        *ram_addr = data[i];
}

/*
 * 读取指定点的颜色值（RGB565），坐标越界返回 0
 * ILI9341 16 位并口时序：设 1x1 窗口 -> 发读 GRAM 命令（0x2E）->
 * 每个颜色通道前需一次哑读丢弃无效数据，通道有效位位于 [15:10]。
 * 部分面板读出顺序为 B 先，若上板验证发现红蓝互换，对调 r/b 即可。
 */
uint16_t Lcd_ReadPoint(uint16_t x, uint16_t y)
{
    uint16_t r, g, b;

    if (Lcd_CoordError(x, y))
        return 0;

    Lcd_SetWindow(x, y, 1, 1);     /* 1x1 窗口，定位到目标像素 */
    Lcd_WrCmd(s_LcdDev.rdcmd);     /* 开始读 GRAM 命令 */

    Lcd_RdData();                  /* 哑读（丢弃） */
    Lcd_RdData();
    r = Lcd_RdData();              /* R[5:0] @ [15:10] */
    Lcd_RdData();
    g = Lcd_RdData();              /* G[5:0] @ [15:10] */
    Lcd_RdData();
    b = Lcd_RdData();              /* B[5:0] @ [15:10] */

    /* 组装为 RGB565：R/B 截为 5 位，G 截为 6 位 */
    r = (r >> 10) & 0x1F;
    g = (g >> 10) & 0x3F;
    b = (b >> 10) & 0x1F;

    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* ===================== 绘图函数 ===================== */

/* 画点（坐标 + RGB565 颜色） */
void Lcd_DrawPoint(uint16_t x, uint16_t y, uint16_t color)
{
    if (Lcd_CoordError(x, y))
        return;

    /* 设置1x1窗口以确保精确的像素定位 */
    Lcd_SetWindow(x, y, 1, 1);

    LCD->LCD_REG = s_LcdDev.wramcmd;
    LCD->LCD_RAM = color;
}

/* 画大点（2x2像素） */
void Lcd_DrawBigPoint(uint16_t x, uint16_t y, uint16_t color)
{
    Lcd_Fill(x, y, 2, 2, color);    /* 绘制一个2x2的大点 */
}

/* 画线（Bresenham算法，起点 -> 终点） */
void Lcd_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    /* 计算增量 */
    int16_t dx    = x2 - x1;        /* X方向总增量 */
    int16_t dy    = y2 - y1;        /* Y方向总增量 */
    int16_t cur_x = x1;             /* 当前X坐标 */
    int16_t cur_y = y1;             /* 当前Y坐标 */

    /* 确定步进方向 */
    int8_t incx = (dx > 0) ? 1 : (dx == 0 ? 0 : -1);
    int8_t incy = (dy > 0) ? 1 : (dy == 0 ? 0 : -1);

    /* 取绝对值 */
    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;

    /* 确定总步数（取较大增量） */
    uint16_t distance = (dx > dy) ? dx : dy;

    /* 初始化误差 */
    int16_t xerr = 0;
    int16_t yerr = 0;

    /* 主循环：逐点绘制 */
    for (uint16_t t = 0; t <= distance; t++)
    {
        /* 绘制当前像素 */
        Lcd_DrawPoint((uint16_t)cur_x, (uint16_t)cur_y, color);

        /* 累加误差 */
        xerr += dx;
        yerr += dy;

        /* X方向步进判断 */
        if (xerr > distance)
        {
            xerr -= distance;
            cur_x += incx;
        }

        /* Y方向步进判断 */
        if (yerr > distance)
        {
            yerr -= distance;
            cur_y += incy;
        }
    }
}

/* 画矩形（左上角 + 右下角） */
void Lcd_DrawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    Lcd_DrawLine(x1, y1, x2, y1, color);
    Lcd_DrawLine(x1, y1, x1, y2, color);
    Lcd_DrawLine(x1, y2, x2, y2, color);
    Lcd_DrawLine(x2, y1, x2, y2, color);
}

/* 画圆（Bresenham算法，圆心 + 半径） */
void Lcd_DrawCircle(uint16_t x, uint16_t y, uint8_t r, uint16_t color)
{
    if (r == 0)
        return;

    int a  = 0;
    int b  = r;
    int di = 3 - (r << 1);    /* 判断下个点位置的标志 */

    while (a <= b)
    {
        Lcd_DrawPoint(x + a, y - b, color);    /* 5 */
        Lcd_DrawPoint(x + b, y - a, color);    /* 0 */
        Lcd_DrawPoint(x + b, y + a, color);    /* 4 */
        Lcd_DrawPoint(x + a, y + b, color);    /* 6 */
        Lcd_DrawPoint(x - a, y + b, color);    /* 1 */
        Lcd_DrawPoint(x - b, y + a, color);
        Lcd_DrawPoint(x - a, y - b, color);    /* 2 */
        Lcd_DrawPoint(x - b, y - a, color);    /* 7 */

        a++;
        /* 使用Bresenham算法画圆 */
        if (di < 0)
            di += 4 * a + 6;
        else
        {
            di += 10 + 4 * (a - b);
            b--;
        }
    }
}

/* ===================== 显示字符函数 ===================== */

/*
 * 在指定位置显示一个字符（适配 lcd_font.h，size: 12/16/24）
 * x/y 为左上角坐标，color 前景色，bg_color 背景色
 */
void Lcd_ShowChar(uint16_t x, uint16_t y, uint8_t ch, uint8_t size,
                  uint16_t color, uint16_t bg_color)
{
    /* 1. 参数检查 */
    if (size != 12 && size != 16 && size != 24)
        return;
    if ((ch < (uint8_t)' ') || (ch > (uint8_t)'~'))
        return;
    if (x >= s_LcdDev.width || y >= s_LcdDev.height)
        return;

    /* 2. 获取字库数据 */
    const uint8_t *font;
    uint8_t width;          /* 字符宽度（像素） */
    uint8_t height = size;  /* 字符高度（像素） */
    uint8_t bytes_per_row;  /* 每行占用的字节数 */

    /* 根据字体大小设置参数 */
    if (size == 12)
    {
        font          = ascii_1206[ch - ' '];
        width         = 6;
        bytes_per_row = 1;  /* 6像素 → 1字节（有2bit填充） */
    }
    else if (size == 16)
    {
        font          = ascii_1608[ch - ' '];
        width         = 8;
        bytes_per_row = 1;  /* 8像素 → 1字节 */
    }
    else    /* size == 24 */
    {
        font          = ascii_2412[ch - ' '];
        width         = 12;
        bytes_per_row = 2;  /* 12像素 → 2字节（有4bit填充） */
    }

    /* 3. 边界裁剪 */
    uint16_t draw_width  = width;
    uint16_t draw_height = height;
    if (x + draw_width > s_LcdDev.width)
        draw_width = s_LcdDev.width - x;
    if (y + draw_height > s_LcdDev.height)
        draw_height = s_LcdDev.height - y;
    if (draw_width == 0 || draw_height == 0)
        return;

    /* 4. 设置窗口，进入批量写入模式 */
    Lcd_SetWindow(x, y, draw_width, draw_height);
    LCD->LCD_REG = s_LcdDev.wramcmd;

    /* 5. 逐行绘制（适配 lcd_font.h：LSB = 左边像素） */
    for (uint8_t row = 0; row < draw_height; row++)
    {
        /* 当前行的起始字节索引 */
        uint16_t byte_idx     = row * bytes_per_row;
        uint8_t  bit_pos      = 0;    /* 当前 bit 位置（0~7） */
        uint8_t  current_byte = font[byte_idx];

        for (uint8_t col = 0; col < draw_width; col++)
        {
            /* 从 LSB 开始读取（bit0 = 左边像素） */
            uint8_t bit = (current_byte >> bit_pos) & 0x01;
            LCD->LCD_RAM = bit ? color : bg_color;

            /* 移动到下一个 bit */
            bit_pos++;
            if (bit_pos >= 8)
            {
                bit_pos = 0;
                byte_idx++;
                current_byte = font[byte_idx];
            }
        }
    }
}

/*
 * 显示字符串（高效批量写入版）
 * 一次性设置窗口，所有像素连续写入，无多余命令开销；自动换行，超出区域裁剪。
 * width/height 为显示区域大小，size: 12/16/24
 */
void Lcd_ShowString(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                    uint8_t size, const char *str, uint16_t color, uint16_t bg_color)
{
    /* 1. 参数检查 */
    if (str == NULL || size == 0)
        return;
    if (x >= s_LcdDev.width || y >= s_LcdDev.height)
        return;
    if (width == 0 || height == 0)
        return;

    /* 2. 字体参数 */
    uint8_t char_width    = size / 2;
    uint8_t char_height   = size;
    uint8_t bytes_per_row = (size == 24) ? 2 : 1;    /* 24号字体每行2字节，其他1字节 */

    /* 3. 计算字符串显示所需行数 */
    uint16_t    str_len = 0;
    const char *p       = str;
    while (*p != '\0')
    {
        if (*p >= ' ' && *p <= '~')
            str_len++;
        p++;
    }
    if (str_len == 0)
        return;

    uint8_t chars_per_line = width / char_width;
    if (chars_per_line == 0)
        return;

    uint16_t total_lines  = (str_len + chars_per_line - 1) / chars_per_line;
    uint16_t total_height = total_lines * char_height;

    /* 裁剪高度 */
    if (total_height > height)
    {
        total_height = height;
        total_lines = total_height / char_height;
    }
    if (total_lines == 0 || total_height == 0)
        return;

    /* 计算实际使用的宽度（最后一行可能不满） */
    uint16_t last_line_chars = str_len % chars_per_line;
    if (last_line_chars == 0)
        last_line_chars = chars_per_line;
    uint16_t last_line_width = last_line_chars * char_width;
    uint16_t effective_width = (total_lines == 1) ? last_line_width : width;

    /* 裁剪到屏幕范围 */
    if (x + effective_width > s_LcdDev.width)
        effective_width = s_LcdDev.width - x;
    if (y + total_height > s_LcdDev.height)
    {
        total_height = s_LcdDev.height - y;
        total_lines = total_height / char_height;
    }
    if (effective_width == 0 || total_height == 0)
        return;

    /* 4. 一次性设置窗口 */
    Lcd_SetWindow(x, y, effective_width, total_height);
    LCD->LCD_REG = s_LcdDev.wramcmd;

    /* 5. 逐行逐像素填充 */
    for (uint16_t line = 0; line < total_lines; line++)
    {
        /* 当前行的起始字符索引和长度 */
        uint16_t start_idx = line * chars_per_line;
        uint16_t end_idx   = start_idx + chars_per_line;
        if (end_idx > str_len)
            end_idx = str_len;
        uint16_t line_chars = end_idx - start_idx;

        /* 遍历行内所有像素行 */
        for (uint8_t row = 0; row < char_height; row++)
        {
            /* 重新定位字符串指针到行首 */
            p = str;
            uint16_t char_pos = 0;
            while (char_pos < start_idx && *p != '\0')
            {
                if (*p >= ' ' && *p <= '~')
                    char_pos++;
                p++;
            }

            /* 遍历该行的每一列（像素列） */
            for (uint16_t col = 0; col < effective_width; col++)
            {
                /* 计算当前列属于哪个字符 */
                uint16_t char_index = col / char_width;
                if (char_index >= line_chars)
                {
                    /* 超出该行字符范围，填充背景色（用于不满行） */
                    LCD->LCD_RAM = bg_color;
                    continue;
                }

                /* 获取当前字符 */
                uint8_t ch = *p;
                while (*p != '\0' && (ch < ' ' || ch > '~'))
                {
                    p++;
                    ch = *p;
                }
                if (ch < ' ' || ch > '~')
                {
                    LCD->LCD_RAM = bg_color;
                    continue;
                }

                /* 获取字模指针 */
                const uint8_t *font;
                if (size == 12)
                    font = ascii_1206[ch - ' '];
                else if (size == 16)
                    font = ascii_1608[ch - ' '];
                else    /* size == 24 */
                    font = ascii_2412[ch - ' '];

                /* 计算当前像素在字模中的位置 */
                uint16_t byte_idx = row * bytes_per_row + (col % char_width) / 8;
                uint8_t  bit_pos  = (col % char_width) % 8;
                uint8_t  bit      = (font[byte_idx] >> bit_pos) & 0x01;

                /* 写入像素 */
                LCD->LCD_RAM = bit ? color : bg_color;

                /* 如果到达该字符末尾，移动到下一个字符 */
                if ((col % char_width) == char_width - 1)
                {
                    while (*p != '\0' && (*p < ' ' || *p > '~'))
                        p++;
                    if (*p != '\0')
                        p++;
                }
            }
        }
    }
}

/* 显示数字（高位不显示0），len 为总位数，size: 12/16/24 */
void Lcd_ShowNum(uint16_t x, uint16_t y, uint32_t num, uint8_t len,
                 uint8_t size, uint16_t color, uint16_t bg_color)
{
    if (len == 0 || size == 0)
        return;
    if (x >= s_LcdDev.width || y >= s_LcdDev.height)
        return;

    /* 预计算 10 的幂（最大支持10位） */
    static const uint32_t pow10[] = {
        1, 10, 100, 1000, 10000,
        100000, 1000000, 10000000, 100000000, 1000000000};

    if (len > 10)
        len = 10;    /* 限制最大位数 */

    uint8_t char_width = size / 2;
    uint8_t enshow     = 0;    /* 是否已经开始显示有效数字 */

    for (uint8_t i = 0; i < len; i++)
    {
        uint8_t digit = (num / pow10[len - i - 1]) % 10;

        if (!enshow && i < (len - 1))
        {
            if (digit == 0)
            {
                /* 高位0不显示，显示空格占位 */
                Lcd_ShowChar(x + char_width * i, y, ' ', size, color, bg_color);
                continue;
            }
            else
                enshow = 1;    /* 遇到非零数字，开始显示 */
        }
        Lcd_ShowChar(x + char_width * i, y, '0' + digit, size, color, bg_color);
    }
}

/*
 * 显示数字（增强版），mode: bit7=1 高位补零，bit0=1 叠加模式
 * len 为总位数，size: 12/16/24
 */
void Lcd_ShowxNum(uint16_t x, uint16_t y, uint32_t num, uint8_t len,
                  uint8_t size, uint8_t mode, uint16_t color, uint16_t bg_color)
{
    if (len == 0 || size == 0)
        return;
    if (x >= s_LcdDev.width || y >= s_LcdDev.height)
        return;

    static const uint32_t pow10[] = {
        1, 10, 100, 1000, 10000,
        100000, 1000000, 10000000, 100000000, 1000000000};

    if (len > 10)
        len = 10;

    uint8_t char_width = size / 2;
    uint8_t fill_zero  = (mode & 0x80) ? 1 : 0;    /* bit7: 是否补零 */
    uint8_t enshow     = 0;

    for (uint8_t i = 0; i < len; i++)
    {
        uint8_t digit = (num / pow10[len - i - 1]) % 10;

        if (!enshow && i < (len - 1))
        {
            if (digit == 0)
            {
                if (fill_zero)
                    Lcd_ShowChar(x + char_width * i, y, '0', size, color, bg_color);
                else
                    Lcd_ShowChar(x + char_width * i, y, ' ', size, color, bg_color);
                continue;
            }
            else
                enshow = 1;
        }
        Lcd_ShowChar(x + char_width * i, y, '0' + digit, size, color, bg_color);
    }
}
