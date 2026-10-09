#include "touch.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========== 底层引脚操作宏 ========== */
#define TOUCH_CS_LOW()  HAL_GPIO_WritePin(TOUCH_CS_PORT, TOUCH_CS_PIN, GPIO_PIN_RESET)
#define TOUCH_CS_HIGH() HAL_GPIO_WritePin(TOUCH_CS_PORT, TOUCH_CS_PIN, GPIO_PIN_SET)
#define TOUCH_PEN()     (HAL_GPIO_ReadPin(TOUCH_PEN_PORT, TOUCH_PEN_PIN) == TOUCH_PEN_ACTIVE_LEVEL)

#if TOUCH_SPI_MODE == 0
#define TOUCH_CLK_LOW()  HAL_GPIO_WritePin(TOUCH_CLK_PORT, TOUCH_CLK_PIN, GPIO_PIN_RESET)
#define TOUCH_CLK_HIGH() HAL_GPIO_WritePin(TOUCH_CLK_PORT, TOUCH_CLK_PIN, GPIO_PIN_SET)
#define TOUCH_MOSI_0()   HAL_GPIO_WritePin(TOUCH_MOSI_PORT, TOUCH_MOSI_PIN, GPIO_PIN_RESET)
#define TOUCH_MOSI_1()   HAL_GPIO_WritePin(TOUCH_MOSI_PORT, TOUCH_MOSI_PIN, GPIO_PIN_SET)
#define TOUCH_MOSI(x)    HAL_GPIO_WritePin(TOUCH_MOSI_PORT, TOUCH_MOSI_PIN, (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define TOUCH_MISO()     (HAL_GPIO_ReadPin(TOUCH_MISO_PORT, TOUCH_MISO_PIN) == GPIO_PIN_SET)
#endif

/*
 * 校准数据 EEPROM 存储布局（共 17 字节）：
 * magic(4) | xfac(4) | yfac(4) | xoff(2) | yoff(2) | swap(1)
 */
#define TOUCH_CAL_MAGIC 0x544F4348u /* "TOCH" */
#define TOUCH_CAL_SIZE  17

/* ========== 触摸状态定义 ========== */
typedef enum {
    TOUCH_STATE_IDLE = 0,
    TOUCH_STATE_PRESSED,
} TouchState_t;

/* ========== 触摸上下文结构体 ========== */
typedef struct {
    TouchState_t state;  /* 触摸状态 */
    uint16_t last_x;     /* 最后一次按下的屏幕坐标 X */
    uint16_t last_y;     /* 最后一次按下的屏幕坐标 Y */
    uint16_t last_raw_x; /* 最后一次按下的原始 AD 值 X */
    uint16_t last_raw_y; /* 最后一次按下的原始 AD 值 Y */
} TouchContext_t;

/* ========== 内部变量 ========== */
static TouchContext_t s_TouchCtx;
static uint8_t s_Initialized = 0;
static TouchCal_t s_Cal;                     /* 当前校准参数 */
static uint8_t s_CmdX = TOUCH_CMD_X;         /* X 轴读取命令（受 swap 影响） */
static uint8_t s_CmdY = TOUCH_CMD_Y;         /* Y 轴读取命令 */

/* ========== 内部函数声明 ========== */
#if TOUCH_SPI_MODE == 0
static void Touch_DelayUs(uint32_t us);
static void Touch_WriteByte(uint8_t num);
#endif
static uint16_t Touch_ReadAD(uint8_t cmd);
static uint16_t Touch_ReadXOY(uint8_t cmd);
static void Touch_UpdateCmdMap(uint8_t swap);

/* 使能指定 GPIO 端口的时钟（随引脚配置自动适配） */
static void Touch_EnablePortClk(GPIO_TypeDef *port)
{
    if (port == GPIOA)
        __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB)
        __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC)
        __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD)
        __HAL_RCC_GPIOD_CLK_ENABLE();
    else if (port == GPIOE)
        __HAL_RCC_GPIOE_CLK_ENABLE();
    else if (port == GPIOF)
        __HAL_RCC_GPIOF_CLK_ENABLE();
    else if (port == GPIOG)
        __HAL_RCC_GPIOG_CLK_ENABLE();
    else if (port == GPIOH)
        __HAL_RCC_GPIOH_CLK_ENABLE();
#ifdef GPIOI
    else if (port == GPIOI)
        __HAL_RCC_GPIOI_CLK_ENABLE();
#endif /* GPIOI */
}

/* ========== SPI 底层操作 ========== */
#if TOUCH_SPI_MODE == 0
/* 微秒级延时（DWT 周期计数器实现，与系统主频无关） */
static void Touch_DelayUs(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000u);
    while ((DWT->CYCCNT - start) < cycles)
    {
        /* 等待 */
    }
}

/* 软件 SPI 向触摸 IC 写入 1 字节（MSB 优先，上升沿锁存） */
static void Touch_WriteByte(uint8_t num)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        TOUCH_MOSI(num & 0x80);
        num <<= 1;
        TOUCH_CLK_LOW();
        Touch_DelayUs(1);
        TOUCH_CLK_HIGH(); /* 上升沿有效 */
        Touch_DelayUs(1);
    }
}
#endif /* TOUCH_SPI_MODE == 0 */

/* 读取触摸 IC 某通道 12 位 AD 值，返回 0 ~ 4095 */
static uint16_t Touch_ReadAD(uint8_t cmd)
{
#if TOUCH_SPI_MODE == 0
    uint16_t val = 0;

    TOUCH_CLK_LOW();
    TOUCH_MOSI_0();
    TOUCH_CS_LOW();         /* 选中触摸 IC */

    Touch_WriteByte(cmd);   /* 发送读取命令 */
    Touch_DelayUs(6);       /* 等待 AD 转换（ADS7846 最长 6us） */

    TOUCH_CLK_LOW();
    Touch_DelayUs(1);
    TOUCH_CLK_HIGH();       /* 一个时钟丢弃 BUSY 位 */
    Touch_DelayUs(1);
    TOUCH_CLK_LOW();

    for (uint8_t i = 0; i < 16; i++) /* 读取 16 位，仅高 12 位有效 */
    {
        val <<= 1;
        TOUCH_CLK_LOW();
        Touch_DelayUs(1);
        TOUCH_CLK_HIGH();
        if (TOUCH_MISO())
            val++;
    }
    val >>= 4;

    TOUCH_CS_HIGH();        /* 释放片选 */
    return val;

#else /* 硬件 SPI */
    uint8_t tx[3] = {cmd, 0xFF, 0xFF};
    uint8_t rx[3];

    TOUCH_CS_LOW();
    HAL_SPI_TransmitReceive(&TOUCH_SPI, tx, rx, 3, TOUCH_SPI_TIMEOUT);
    TOUCH_CS_HIGH();

    /* rx[1] 含 BUSY 位，rx[1..2] 共 16 位，高 12 位有效 */
    return (uint16_t)((((uint16_t)rx[1] << 8) | rx[2]) >> 4);
#endif
}

/* 读取单轴 AD 值（去极值均值滤波：采样后排序，去掉两端极值取平均） */
static uint16_t Touch_ReadXOY(uint8_t cmd)
{
    uint16_t buf[TOUCH_READ_TIMES];
    uint32_t sum = 0;

    for (uint8_t i = 0; i < TOUCH_READ_TIMES; i++)
        buf[i] = Touch_ReadAD(cmd);

    /* 冒泡排序（升序） */
    for (uint8_t i = 0; i < TOUCH_READ_TIMES - 1; i++)
        for (uint8_t j = i + 1; j < TOUCH_READ_TIMES; j++)
            if (buf[i] > buf[j])
            {
                uint16_t temp = buf[i];
                buf[i] = buf[j];
                buf[j] = temp;
            }

    /* 去掉两端极值后求平均 */
    for (uint8_t i = TOUCH_LOST_VAL; i < TOUCH_READ_TIMES - TOUCH_LOST_VAL; i++)
        sum += buf[i];

    return (uint16_t)(sum / (TOUCH_READ_TIMES - 2 * TOUCH_LOST_VAL));
}

/* 读取原始 X/Y 坐标（双重滤波 + 两次读取一致性校验），偏差过大返回 TOUCH_ERR_READ */
TouchErr_t Touch_ReadRawXY(uint16_t *raw_x, uint16_t *raw_y)
{
    uint16_t x1, y1, x2, y2;

    if (raw_x == NULL || raw_y == NULL)
        return TOUCH_ERR_PARAM;

    x1 = Touch_ReadXOY(s_CmdX);
    y1 = Touch_ReadXOY(s_CmdY);
    x2 = Touch_ReadXOY(s_CmdX);
    y2 = Touch_ReadXOY(s_CmdY);

    /* 两次读取偏差均需在允许范围内 */
    if (abs((int16_t)(x1 - x2)) > TOUCH_ERR_RANGE ||
        abs((int16_t)(y1 - y2)) > TOUCH_ERR_RANGE)
        return TOUCH_ERR_READ;

    *raw_x = (x1 + x2) / 2;
    *raw_y = (y1 + y2) / 2;
    return TOUCH_OK;
}

/* 根据 X/Y 轴交换标志更新读取命令映射（0 = 同向；1 = 反向） */
static void Touch_UpdateCmdMap(uint8_t swap)
{
    if (swap)
    {
        s_CmdX = TOUCH_CMD_Y;
        s_CmdY = TOUCH_CMD_X;
    }
    else
    {
        s_CmdX = TOUCH_CMD_X;
        s_CmdY = TOUCH_CMD_Y;
    }
}

/* 手动设置校准参数（立即生效） */
void Touch_SetCalibration(const TouchCal_t *cal)
{
    if (cal == NULL)
        return;
    s_Cal = *cal;
    s_Cal.valid = 1;
    Touch_UpdateCmdMap(s_Cal.swap);
}

/* 获取当前校准参数 */
void Touch_GetCalibration(TouchCal_t *cal)
{
    if (cal == NULL)
        return;
    *cal = s_Cal;
}

/*
 * 由 4 点原始坐标计算校准参数（纯计算，不依赖屏幕与存储）
 * pos 顺序固定：pos[0] 左上、pos[1] 右上、pos[2] 左下、pos[3] 右下
 * tgt 为对应的 4 个目标点屏幕坐标（顺序同 pos，允许非全屏对称布局）
 * 采样质量不合格（边长/对角线比例超差）返回 TOUCH_ERR_CAL
 */
TouchErr_t Touch_CalcCalibrationEx(const uint16_t pos[4][2], const uint16_t tgt[4][2], TouchCal_t *cal)
{
    uint32_t tem1, tem2;
    double d1, d2, fac;

    if (pos == NULL || tgt == NULL || cal == NULL)
        return TOUCH_ERR_PARAM;

    /* 校验 1: 上边长 / 下边长 */
    tem1 = (uint32_t)abs((int16_t)(pos[0][0] - pos[1][0]));
    tem2 = (uint32_t)abs((int16_t)(pos[0][1] - pos[1][1]));
    d1 = sqrt((double)(tem1 * tem1 + tem2 * tem2));
    tem1 = (uint32_t)abs((int16_t)(pos[2][0] - pos[3][0]));
    tem2 = (uint32_t)abs((int16_t)(pos[2][1] - pos[3][1]));
    d2 = sqrt((double)(tem1 * tem1 + tem2 * tem2));
    fac = (d2 == 0) ? 0 : (d1 / d2);
    if (fac < 0.95 || fac > 1.05 || d1 == 0)
        return TOUCH_ERR_CAL;

    /* 校验 2: 左边长 / 右边长 */
    tem1 = (uint32_t)abs((int16_t)(pos[0][0] - pos[2][0]));
    tem2 = (uint32_t)abs((int16_t)(pos[0][1] - pos[2][1]));
    d1 = sqrt((double)(tem1 * tem1 + tem2 * tem2));
    tem1 = (uint32_t)abs((int16_t)(pos[1][0] - pos[3][0]));
    tem2 = (uint32_t)abs((int16_t)(pos[1][1] - pos[3][1]));
    d2 = sqrt((double)(tem1 * tem1 + tem2 * tem2));
    fac = (d2 == 0) ? 0 : (d1 / d2);
    if (fac < 0.95 || fac > 1.05 || d1 == 0)
        return TOUCH_ERR_CAL;

    /* 校验 3: 对角线 1-4 / 对角线 2-3 */
    tem1 = (uint32_t)abs((int16_t)(pos[1][0] - pos[2][0]));
    tem2 = (uint32_t)abs((int16_t)(pos[1][1] - pos[2][1]));
    d1 = sqrt((double)(tem1 * tem1 + tem2 * tem2));
    tem1 = (uint32_t)abs((int16_t)(pos[0][0] - pos[3][0]));
    tem2 = (uint32_t)abs((int16_t)(pos[0][1] - pos[3][1]));
    d2 = sqrt((double)(tem1 * tem1 + tem2 * tem2));
    fac = (d2 == 0) ? 0 : (d1 / d2);
    if (fac < 0.95 || fac > 1.05 || d1 == 0)
        return TOUCH_ERR_CAL;

    /* 计算线性变换参数：系数 = 目标点跨度 / 原始 AD 跨度，偏移 = 目标中点 - 系数 × AD 中点 */
    if (pos[1][0] == pos[0][0] || pos[2][1] == pos[0][1])
        return TOUCH_ERR_CAL;

    cal->xfac = (float)(tgt[1][0] - tgt[0][0]) / (int16_t)(pos[1][0] - pos[0][0]);
    cal->xoff = (int16_t)(((int32_t)tgt[0][0] + tgt[1][0]) / 2 - cal->xfac * (int32_t)(pos[0][0] + pos[1][0]) / 2);
    cal->yfac = (float)(tgt[2][1] - tgt[0][1]) / (int16_t)(pos[2][1] - pos[0][1]);
    cal->yoff = (int16_t)(((int32_t)tgt[0][1] + tgt[2][1]) / 2 - cal->yfac * (int32_t)(pos[0][1] + pos[2][1]) / 2);
    cal->valid = 1;
    /* swap 保持调用者传入值（此处不修改） */
    return TOUCH_OK;
}

/* 全屏四角内缩 margin 布局的便捷包装（供 Touch_Adjust 使用） */
TouchErr_t Touch_CalcCalibration(const uint16_t pos[4][2], uint16_t screen_w, uint16_t screen_h,
                                 uint16_t margin, TouchCal_t *cal)
{
    uint16_t tgt[4][2];

    if (margin * 2 >= screen_w || margin * 2 >= screen_h)
        return TOUCH_ERR_PARAM;

    tgt[0][0] = margin;
    tgt[0][1] = margin;                 /* 左上 */
    tgt[1][0] = screen_w - margin;
    tgt[1][1] = margin;                 /* 右上 */
    tgt[2][0] = margin;
    tgt[2][1] = screen_h - margin;      /* 左下 */
    tgt[3][0] = screen_w - margin;
    tgt[3][1] = screen_h - margin;      /* 右下 */
    return Touch_CalcCalibrationEx(pos, tgt, cal);
}

/*
 * 交互式 4 点校准（不依赖具体显示设备，由回调完成取点交互）
 * margin 建议 20；若检测到触摸屏 X/Y 轴与屏幕反向（校准系数异常），自动交换轴重试一次
 */
TouchErr_t Touch_Adjust(uint16_t screen_w, uint16_t screen_h, uint16_t margin, TouchGetPointFn get_point)
{
    uint16_t pos[4][2];
    uint16_t px[4], py[4];
    TouchCal_t cal;
    TouchErr_t ret;
    uint8_t retry;
    uint8_t swap = s_Cal.swap; /* 轴交换标志（异常时自动翻转重试） */

    if (get_point == NULL)
        return TOUCH_ERR_PARAM;

    px[0] = margin;
    py[0] = margin;                     /* 左上 */
    px[1] = screen_w - margin;
    py[1] = margin;                     /* 右上 */
    px[2] = margin;
    py[2] = screen_h - margin;          /* 左下 */
    px[3] = screen_w - margin;
    py[3] = screen_h - margin;          /* 右下 */

    for (retry = 0; retry < 2; retry++)
    {
        cal.swap = swap;

        /* 依次采集 4 个校准点 */
        for (uint8_t i = 0; i < 4; i++)
            if (get_point(px[i], py[i], &pos[i][0], &pos[i][1]) == 0)
                return TOUCH_ERR_NO_TOUCH; /* 用户取消/超时 */

        ret = Touch_CalcCalibration(pos, screen_w, screen_h, margin, &cal);
        if (ret != TOUCH_OK)
            continue; /* 采样质量不合格，重新采集 */

        /* 系数异常说明触摸屏 X/Y 方向与屏幕相反，交换轴后重试 */
        if (fabsf(cal.xfac) > 2 || fabsf(cal.yfac) > 2)
        {
            swap = !swap;
            Touch_UpdateCmdMap(swap);
            continue;
        }

        cal.swap = swap;
        Touch_SetCalibration(&cal);
        return TOUCH_OK;
    }

    return TOUCH_ERR_CAL;
}

/* ========== 校准参数 EEPROM 存储 ========== */
#if TOUCH_USE_EEPROM_CAL

/* 将当前校准参数保存到 AT24Cxx */
TouchErr_t Touch_SaveCalibration(void)
{
    uint8_t buf[TOUCH_CAL_SIZE];
    uint8_t idx = 0;
    uint32_t magic = TOUCH_CAL_MAGIC;

    if (!s_Cal.valid)
        return TOUCH_ERR_CAL;

    memcpy(&buf[idx], &magic, 4);
    idx += 4;
    memcpy(&buf[idx], &s_Cal.xfac, 4);
    idx += 4;
    memcpy(&buf[idx], &s_Cal.yfac, 4);
    idx += 4;
    memcpy(&buf[idx], &s_Cal.xoff, 2);
    idx += 2;
    memcpy(&buf[idx], &s_Cal.yoff, 2);
    idx += 2;
    buf[idx] = s_Cal.swap;

    if (TOUCH_EEPROM_WRITE(TOUCH_CAL_EEPROM_ADDR, buf, TOUCH_CAL_SIZE) != TOUCH_EEPROM_OK)
        return TOUCH_ERR_EEPROM;

    return TOUCH_OK;
}

/* 从 AT24Cxx 加载校准参数，TOUCH_ERR_CAL 表示尚未校准 */
TouchErr_t Touch_LoadCalibration(void)
{
    uint8_t buf[TOUCH_CAL_SIZE];
    uint8_t idx = 0;
    uint32_t magic = 0;
    TouchCal_t cal;

    if (TOUCH_EEPROM_READ(TOUCH_CAL_EEPROM_ADDR, buf, TOUCH_CAL_SIZE) != TOUCH_EEPROM_OK)
        return TOUCH_ERR_EEPROM;

    memcpy(&magic, &buf[idx], 4);
    idx += 4;
    if (magic != TOUCH_CAL_MAGIC)
        return TOUCH_ERR_CAL; /* 未校准过 */

    memcpy(&cal.xfac, &buf[idx], 4);
    idx += 4;
    memcpy(&cal.yfac, &buf[idx], 4);
    idx += 4;
    memcpy(&cal.xoff, &buf[idx], 2);
    idx += 2;
    memcpy(&cal.yoff, &buf[idx], 2);
    idx += 2;
    cal.swap = buf[idx];
    cal.valid = 1;

    Touch_SetCalibration(&cal);
    return TOUCH_OK;
}
#endif /* TOUCH_USE_EEPROM_CAL */

/* 查询触摸当前是否按下，0 = 未按下, 1 = 按下 */
uint8_t Touch_IsPressed(void)
{
    return TOUCH_PEN() ? 1 : 0;
}

/*
 * 扫描触摸（参照 Key_Scan 风格：调用者在循环中每隔固定时间扫描一次，直接返回触摸数据结构体）
 * 校准有效时 x/y 为屏幕坐标，否则为原始 AD 值；释放后 x/y 保留最后一次按下的坐标，便于处理点击
 */
TouchMsg_t Touch_Scan(void)
{
    TouchMsg_t msg;

    msg.x = s_TouchCtx.last_x;
    msg.y = s_TouchCtx.last_y;
    msg.raw_x = s_TouchCtx.last_raw_x;
    msg.raw_y = s_TouchCtx.last_raw_y;
    msg.event = TOUCH_EVENT_NONE;

    if (!s_Initialized)
        return msg;

    if (Touch_IsPressed())
    {
        uint16_t raw_x, raw_y;

        if (Touch_ReadRawXY(&raw_x, &raw_y) != TOUCH_OK)
            return msg; /* 读取失败，保持上一次状态 */

        msg.raw_x = raw_x;
        msg.raw_y = raw_y;

        if (s_Cal.valid)
        {
            int32_t sx = (int32_t)(s_Cal.xfac * raw_x + s_Cal.xoff);
            int32_t sy = (int32_t)(s_Cal.yfac * raw_y + s_Cal.yoff);
            if (sx < 0)
                sx = 0;
            if (sy < 0)
                sy = 0;
            msg.x = (uint16_t)sx;
            msg.y = (uint16_t)sy;
        }
        else
        {
            msg.x = raw_x;
            msg.y = raw_y;
        }

        /* 按下边沿判定 */
        if (s_TouchCtx.state == TOUCH_STATE_PRESSED)
            msg.event = TOUCH_EVENT_PRESS_HOLD;
        else
        {
            msg.event = TOUCH_EVENT_PRESS_DOWN;
            s_TouchCtx.state = TOUCH_STATE_PRESSED;
        }

        s_TouchCtx.last_x = msg.x;
        s_TouchCtx.last_y = msg.y;
        s_TouchCtx.last_raw_x = raw_x;
        s_TouchCtx.last_raw_y = raw_y;
    }
    else
    {
        /* 释放边沿判定 */
        if (s_TouchCtx.state == TOUCH_STATE_PRESSED)
        {
            msg.event = TOUCH_EVENT_PRESS_UP;
            s_TouchCtx.state = TOUCH_STATE_IDLE;
        }
    }

    return msg;
}

/* 初始化触摸（软件 SPI 模式配置 5 个 GPIO；硬件 SPI 模式仅配置 CS 与 PEN，SPI 由 CubeMX 初始化）
 * 电阻屏无器件 ID 可读，初始化本身无法检测失败，故无返回值；校准是否有效用 Touch_GetCalibration() 查询 cal.valid */
void Touch_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* 使能所用引脚的端口时钟（随引脚配置自动适配） */
    Touch_EnablePortClk(TOUCH_CS_PORT);
    Touch_EnablePortClk(TOUCH_PEN_PORT);
#if TOUCH_SPI_MODE == 0
    Touch_EnablePortClk(TOUCH_CLK_PORT);
    Touch_EnablePortClk(TOUCH_MOSI_PORT);
    Touch_EnablePortClk(TOUCH_MISO_PORT);
#endif

    /* 输出引脚：CS（两种模式都需要）+ CLK/MOSI（仅软件 SPI） */
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = TOUCH_CS_PIN;
    HAL_GPIO_Init(TOUCH_CS_PORT, &gpio);
#if TOUCH_SPI_MODE == 0
    gpio.Pin = TOUCH_CLK_PIN;
    HAL_GPIO_Init(TOUCH_CLK_PORT, &gpio);
    gpio.Pin = TOUCH_MOSI_PIN;
    HAL_GPIO_Init(TOUCH_MOSI_PORT, &gpio);
#endif

    /* 输入引脚：PEN + MISO（仅软件 SPI），上拉 */
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin = TOUCH_PEN_PIN;
    HAL_GPIO_Init(TOUCH_PEN_PORT, &gpio);
#if TOUCH_SPI_MODE == 0
    gpio.Pin = TOUCH_MISO_PIN;
    HAL_GPIO_Init(TOUCH_MISO_PORT, &gpio);
#endif

    TOUCH_CS_HIGH();
#if TOUCH_SPI_MODE == 0
    TOUCH_CLK_HIGH();
    TOUCH_MOSI_1();

    /* 初始化 DWT 周期计数器（用于 us 延时） */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
#endif

    /* 第一次读取，唤醒触摸 IC 并稳定 AD 通道 */
    {
        uint16_t temp_x, temp_y;
        Touch_ReadRawXY(&temp_x, &temp_y);
    }

#if TOUCH_USE_EEPROM_CAL
    Touch_LoadCalibration(); /* 加载校准参数（失败表示未校准过） */
#endif

    s_TouchCtx.state = TOUCH_STATE_IDLE;
    s_TouchCtx.last_x = 0;
    s_TouchCtx.last_y = 0;
    s_TouchCtx.last_raw_x = 0;
    s_TouchCtx.last_raw_y = 0;
    s_Initialized = 1;
}
