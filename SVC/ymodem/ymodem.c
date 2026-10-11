#include "ymodem.h"
#include "cmsis_os.h"

#include <string.h>

/* ========== 协议常量 ========== */
#define YMODEM_SOH            0x01U /* 128 字节数据字段包类型 */
#define YMODEM_STX            0x02U /* 1024 字节数据字段包类型 */
#define YMODEM_EOT            0x04U /* 传输结束（两段确认：EOT→NAK→EOT→ACK） */
#define YMODEM_CAN            0x18U /* 取消 */
#define YMODEM_ACK            0x06U /* 应答 */
#define YMODEM_NAK            0x15U /* 否定应答（请求重发） */
#define YMODEM_C              0x43U /* 'C'：请求 CRC 模式启动/下一文件 */

#define YMODEM_DATA_MAX       1024U /* 数据字段长度上限 */
#define YMODEM_HDR_LEN        128U  /* 头块数据字段固定 128 字节 */
#define YMODEM_CRC16_POLY     0x1021U /* CRC16/XMODEM 多项式（MSB 先行，初值 0） */

/* ========== 时序与重试参数 ========== */
#define YMODEM_C_INTERVAL_MS  1000U  /* 握手期 'C' 发送周期 */
#define YMODEM_DATA_WAIT_MS   3000U  /* 包间等待包首字节上限 */
#define YMODEM_BLOCK_MS       1000U  /* 整包体紧轮询接收超时（1024B ≈ 89ms @115200） */
#define YMODEM_TX_MS          100U   /* 单控制字节发送超时 */
#define YMODEM_START_C_MAX    30U    /* 握手期 'C' 发送上限（无发送方则放弃） */
#define YMODEM_RETRY_MAX      10U    /* 连续坏包/超时上限 */

/* 包接收状态 */
typedef enum {
    YMODEM_PKT_OK = 0,   /* 完整包且 CRC 有效 */
    YMODEM_PKT_TIMEOUT,  /* 超时未见有效包首字节 */
    YMODEM_PKT_CAN,      /* 收到发送方 CAN */
    YMODEM_PKT_EOT,      /* 收到发送方 EOT */
    YMODEM_PKT_BAD,      /* 意外字节 / seq 反码 / CRC 错误 */
} YmodemPktState_t;

/* ========== 内部状态 ========== */
/* 包缓冲：[0]=seq [1]=~seq [2..]=数据字段 [2+len]=CRC高 [3+len]=CRC低
 * 单任务（Uart_Proc）共享，放 .bss 省任务栈 */
static uint8_t s_Pkt[2U + YMODEM_DATA_MAX + 2U];

/* ========== 内部函数声明 ========== */
static int16_t Ymodem_WaitByte(uint32_t timeout_ms);
static uint8_t Ymodem_RecvBody(uint8_t *buf, uint32_t len);
static void Ymodem_SendByte(uint8_t b);
static void Ymodem_Abort(void);
static uint16_t Ymodem_CRC16(const uint8_t *data, uint32_t len);
static YmodemPktState_t Ymodem_GetPacket(uint32_t wait_ms, uint32_t *data_len);
static uint8_t Ymodem_ParseOctal(const char *s, uint32_t *out);

/* ========== 字节原语 ========== */

/* 等待单字节（osDelay 轮询，包间空档让出 CPU，不饿死低优先级任务）
 * 返回: 字节值 0~255；-1 = 超时 */
static int16_t Ymodem_WaitByte(uint32_t timeout_ms)
{
    for (uint32_t t = 0; t < timeout_ms; t++)
    {
        if (__HAL_UART_GET_FLAG(&YMODEM_UART, UART_FLAG_RXNE))
            return (int16_t)(READ_REG(YMODEM_UART.Instance->DR) & 0xFFU); /* 读 SR+DR 序列顺带清 ORE */
        osDelay(1);
    }
    return -1;
}

/* 紧轮询收满包体剩余字节（115200 下字节间隔约 87µs，此阶段不可睡眠）
 * len ≤ 1028，适配 HAL uint16 长度字段
 * 返回: 1 = 收满, 0 = 超时 */
static uint8_t Ymodem_RecvBody(uint8_t *buf, uint32_t len)
{
    return (HAL_UART_Receive(&YMODEM_UART, buf, (uint16_t)len, YMODEM_BLOCK_MS) == HAL_OK) ? 1U : 0U;
}

/* 发送单控制字节 */
static void Ymodem_SendByte(uint8_t b)
{
    HAL_UART_Transmit(&YMODEM_UART, &b, 1, YMODEM_TX_MS);
}

/* 双 CAN 通知发送方中止 */
static void Ymodem_Abort(void)
{
    Ymodem_SendByte(YMODEM_CAN);
    Ymodem_SendByte(YMODEM_CAN);
}

/* ========== 校验与解析 ========== */

/* CRC16/XMODEM（poly 0x1021，初值 0，MSB 先行，无反射无终异或） */
static uint16_t Ymodem_CRC16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0;

    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (uint8_t bit = 0; bit < 8U; bit++)
        {
            if (crc & 0x8000U)
                crc = (uint16_t)((uint16_t)(crc << 1) ^ YMODEM_CRC16_POLY);
            else
                crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* 接收一个完整包：等首字节定类型，收 seq+数据+CRC 并校验
 * wait_ms: 等首字节上限
 * data_len: 输出数据字段长度（128 或 1024） */
static YmodemPktState_t Ymodem_GetPacket(uint32_t wait_ms, uint32_t *data_len)
{
    int16_t b = Ymodem_WaitByte(wait_ms);
    if (b < 0)
        return YMODEM_PKT_TIMEOUT;
    if ((uint8_t)b == YMODEM_CAN)
        return YMODEM_PKT_CAN;
    if ((uint8_t)b == YMODEM_EOT)
        return YMODEM_PKT_EOT;

    uint32_t len;
    if ((uint8_t)b == YMODEM_SOH)
        len = YMODEM_HDR_LEN;
    else if ((uint8_t)b == YMODEM_STX)
        len = YMODEM_DATA_MAX;
    else
        return YMODEM_PKT_BAD; /* 意外首字节 */

    if (!Ymodem_RecvBody(s_Pkt, 2U + len + 2U))
        return YMODEM_PKT_BAD;

    if ((uint8_t)(s_Pkt[0] + s_Pkt[1]) != 0xFFU)
        return YMODEM_PKT_BAD;

    uint16_t crc_calc = Ymodem_CRC16(&s_Pkt[2], len);
    uint16_t crc_recv = (uint16_t)(((uint16_t)s_Pkt[2 + len] << 8) | (uint16_t)s_Pkt[3 + len]);
    if (crc_calc != crc_recv)
        return YMODEM_PKT_BAD;

    *data_len = len;
    return YMODEM_PKT_OK;
}

/* 八进制字符串解析（Ymodem 头块长度字段格式）；含非法字符返回 0 */
static uint8_t Ymodem_ParseOctal(const char *s, uint32_t *out)
{
    if (*s < '0' || *s > '7')
        return 0;

    uint32_t v = 0;
    while (*s >= '0' && *s <= '7')
    {
        v = (v << 3) + (uint32_t)(*s - '0');
        s++;
    }
    *out = v;
    return 1;
}

/* ========== 主流程 ========== */

/* Ymodem 接收单文件（CRC 模式，1K/128 包混用，单文件批次）
 * 协议层只负责字节流与状态机，存储由 sink 回调完成
 * 前置：调用方已 Uart_RxStop() 停止 DMA 接收并 Uart_SetQuiet(1) 静默输出
 * 约束：静默期所有 Uart_Printf（含上层回调）被丢弃，故无实时进度；
 *       on_header 内的擦除耗时与文件大小成正比（约 2.5s/100KB），过大文件可能触发发送方等待超时
 * 返回: YmodemErr_t */
YmodemErr_t Ymodem_Receive(const YmodemSink_t *sink)
{
    uint32_t len;
    YmodemPktState_t st;
    uint32_t retries = 0;

    /* ---- 1. 握手：周期发 'C' 等发送方抬头块 ---- */
    for (;;)
    {
        Ymodem_SendByte(YMODEM_C);
        st = Ymodem_GetPacket(YMODEM_C_INTERVAL_MS, &len);
        if (st == YMODEM_PKT_OK)
            break;
        if (st == YMODEM_PKT_CAN)
            return YMODEM_ERR_ABORTED;
        retries++;
        if (retries >= YMODEM_START_C_MAX)
            return YMODEM_ERR_TIMEOUT;
    }

    if (len != YMODEM_HDR_LEN)
        return YMODEM_ERR_PARAM; /* 头块必须为 128 字节包 */

    const char *fname = (const char *)&s_Pkt[2];
    uint32_t name_len = strlen(fname);
    const char *size_str = fname + name_len + 1U;
    uint32_t fsize;
    if (name_len == 0U || name_len >= YMODEM_NAME_MAX ||
        !Ymodem_ParseOctal(size_str, &fsize) || fsize == 0U)
        return YMODEM_ERR_PARAM;

    uint8_t expected = (uint8_t)(s_Pkt[0] + 1U); /* 数据块序号接续头块 */

    /* ---- 2. 通知上层准备存储（回调负责分配/擦除，必须在收数据前完成）---- */
    if (sink->on_header != NULL && sink->on_header(fname, fsize, sink->ctx) != 0)
    {
        Ymodem_Abort();
        return YMODEM_ERR_REJECT;
    }

    Ymodem_SendByte(YMODEM_ACK);
    Ymodem_SendByte(YMODEM_C);

    /* ---- 3. 数据块循环 ---- */
    uint32_t written = 0;
    retries = 0;
    for (;;)
    {
        st = Ymodem_GetPacket(YMODEM_DATA_WAIT_MS, &len);
        if (st == YMODEM_PKT_CAN)
            return YMODEM_ERR_ABORTED;
        if (st == YMODEM_PKT_EOT) /* 未收满声明长度即 EOT：发送方提前结束，视为协议错误 */
        {
            Ymodem_Abort();
            return YMODEM_ERR_PROTOCOL;
        }
        if (st == YMODEM_PKT_TIMEOUT)
        {
            if (++retries > YMODEM_RETRY_MAX)
            {
                Ymodem_Abort();
                return YMODEM_ERR_TIMEOUT;
            }
            Ymodem_SendByte(YMODEM_C);
            continue;
        }
        if (st == YMODEM_PKT_BAD)
        {
            if (++retries > YMODEM_RETRY_MAX)
            {
                Ymodem_Abort();
                return YMODEM_ERR_PROTOCOL;
            }
            Ymodem_SendByte(YMODEM_NAK);
            continue;
        }

        uint8_t seq = s_Pkt[0];
        if (seq == (uint8_t)(expected - 1U)) /* 重复包（ACK 丢失）：只应答不重写 */
        {
            Ymodem_SendByte(YMODEM_ACK);
            continue;
        }
        if (seq != expected) /* 乱序：请求重发 */
        {
            Ymodem_SendByte(YMODEM_NAK);
            continue;
        }

        uint32_t take = (fsize - written > len) ? (fsize - written) : len; /* 末包去填充 */
        if (sink->on_data != NULL &&
            sink->on_data(&s_Pkt[2], take, written, sink->ctx) != 0)
        {
            Ymodem_Abort();
            return YMODEM_ERR_REJECT;
        }
        written += take;
        expected++;
        retries = 0;
        Ymodem_SendByte(YMODEM_ACK);

        if (written >= fsize)
            break;
    }

    /* ---- 4. EOT 两段确认：首个 EOT 回 NAK 请求重发，再次 EOT 回 ACK+'C' ---- */
    for (;;) /* 末块 ACK 丢失时发送方会重发末块：持续应答重复块直至 EOT/超时/CAN */
    {
        st = Ymodem_GetPacket(YMODEM_DATA_WAIT_MS, &len);
        if (st == YMODEM_PKT_OK && s_Pkt[0] == (uint8_t)(expected - 1U))
        {
            Ymodem_SendByte(YMODEM_ACK);
            continue;
        }
        break;
    }
    if (st == YMODEM_PKT_EOT)
    {
        Ymodem_SendByte(YMODEM_NAK);
        st = Ymodem_GetPacket(YMODEM_DATA_WAIT_MS, &len);
        if (st == YMODEM_PKT_EOT)
        {
            Ymodem_SendByte(YMODEM_ACK);
            Ymodem_SendByte(YMODEM_C); /* 请求结束块（或下一文件头块） */
            st = Ymodem_GetPacket(YMODEM_DATA_WAIT_MS, &len);
        }
    }
    if (st == YMODEM_PKT_CAN)
        return YMODEM_ERR_ABORTED;
    /* 发送方跳过 EOT 直发空头块（非标准但兼容）：st 为 PKT_OK 时直接落入第 5 步 */

    /* ---- 5. 结束块（空文件名头块）---- */
    if (st == YMODEM_PKT_OK && len == YMODEM_HDR_LEN && s_Pkt[2] == 0U)
    {
        Ymodem_SendByte(YMODEM_ACK);
        Ymodem_SendByte(YMODEM_C); /* 等下一文件头：单文件模式超时即正常收尾 */
        st = Ymodem_GetPacket(YMODEM_C_INTERVAL_MS, &len);
        if (st == YMODEM_PKT_OK && s_Pkt[2] != 0U)
            Ymodem_Abort(); /* 发送方还有文件：批次多文件属 M4，中止剩余 */
    }
    /* 结束块缺失不影响收尾：数据已收满且协议层 CRC 自洽 */

    /* ---- 6. 通知上层传输完成（回调负责校验/登记）---- */
    if (sink->on_end != NULL && sink->on_end(written, sink->ctx) != 0)
        return YMODEM_ERR_REJECT;

    return YMODEM_OK;
}
