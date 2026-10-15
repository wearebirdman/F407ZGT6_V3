#include "at24cxx.h"
#include "i2c.h"

#include <stdio.h>
#include <string.h>

/* ========== 内部宏定义 ========== */
#define AT24CXX_WRITE_ADDR       ((AT24CXX_DEV_ADDR << 1) & 0xFE) /* 写地址（7位地址左移1位，最低位为0） */
#define AT24CXX_I2C_TIMEOUT      100                              /* I2C 超时时间（ms） */
#define AT24CXX_WRITE_TIMEOUT    10                               /* 写周期等待超时（典型最大 5ms，留余量） */
#define AT24CXX_DEV_CHECK_TRIALS 3                                /* 检测设备应答重试次数 */
#define AT24CXX_REC_MAGIC        0x52454331U /* 记录头标识 "REC1" */
#define AT24CXX_CRC32_POLY       0xEDB88320U /* IEEE 802.3 反射式多项式 */
#define AT24CXX_CRC32_INIT       0xFFFFFFFFU /* 流式累积初值 */

/* ========== 内部函数声明 ========== */
static uint8_t AT24CXX_CheckDevice(void);
static AT24CxxErr_t AT24Cxx_WaitWriteComplete(void);
static uint8_t AT24Cxx_CheckAddr(uint16_t addr, uint16_t len);
static uint32_t AT24Cxx_Crc32Update(uint32_t crc, const uint8_t *data, uint16_t len);
static uint16_t AT24Cxx_SlotBase(uint8_t idx);
static uint8_t AT24Cxx_VerifySlot(uint8_t idx, uint32_t *seq, uint16_t *len);

/* ========== 内部变量 ========== */
static uint8_t s_RecBuf[AT24CXX_REC_DATA_MAX]; /* 记录校验分块读暂存 */

/* 检测设备是否存在（发送设备地址，检查ACK），1 = 存在, 0 = 不存在 */
static uint8_t AT24CXX_CheckDevice(void)
{
    /* 尝试发送写地址，检测是否应答 */
    HAL_StatusTypeDef status = HAL_I2C_IsDeviceReady(&AT24CXX_I2C,
                                                     AT24CXX_WRITE_ADDR,
                                                     AT24CXX_DEV_CHECK_TRIALS,
                                                     AT24CXX_I2C_TIMEOUT);
    return (status == HAL_OK) ? 1 : 0;
}

/* 等待内部写周期完成（通过轮询设备应答）
 * 每次写操作后，芯片内部会进入写周期（典型最大 5ms），期间不响应 I2C
 * 返回: AT24CXX_OK = 设备已就绪；AT24CXX_ERR_TIMEOUT = 超时仍未应答（掉线/总线卡死） */
static AT24CxxErr_t AT24Cxx_WaitWriteComplete(void)
{
    for (uint32_t elapsed = 0; elapsed < AT24CXX_WRITE_TIMEOUT; elapsed++)
    {
        if (AT24CXX_CheckDevice())
            return AT24CXX_OK;
        HAL_Delay(1);
    }
    return AT24CXX_ERR_TIMEOUT;
}

/* 检查地址是否越界，0 = 越界, 1 = 合法 */
static uint8_t AT24Cxx_CheckAddr(uint16_t addr, uint16_t len)
{
    if (len == 0)
        return 1;
    if (addr >= AT24CXX_CHIP_SIZE)
        return 0;
    if (addr + len > AT24CXX_CHIP_SIZE)
        return 0;
    return 1;
}

/* 初始化 AT24CXX，检测设备是否存在 */
AT24CxxErr_t AT24Cxx_Init(void)
{
    if (!AT24CXX_CheckDevice())
        return AT24CXX_ERR_DEVICE;
    return AT24CXX_OK;
}

/* 获取芯片信息 */
void AT24Cxx_GetInfo(AT24CxxInfo_t *info)
{
    if (info == NULL)
        return;
    info->size_bytes = AT24CXX_CHIP_SIZE;
    info->page_size = AT24CXX_PAGE_SIZE;
    info->page_count = AT24CXX_PAGE_COUNT;
    info->dev_addr = AT24CXX_DEV_ADDR;
}

/* 从指定地址读取数据（addr: 0 ~ AT24CXX_CHIP_SIZE-1） */
AT24CxxErr_t AT24Cxx_Read(uint16_t addr, uint8_t *buf, uint16_t len)
{
    /* 参数检查 */
    if (buf == NULL || len == 0)
        return AT24CXX_ERR_PARAM;

    /* 地址越界检查 */
    if (!AT24Cxx_CheckAddr(addr, len))
        return AT24CXX_ERR_ADDR;

    /* 使用 HAL 库的 Mem_Read 函数（地址宽度由所选型号决定） */
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(&AT24CXX_I2C,
                                                 AT24CXX_WRITE_ADDR,
                                                 addr,
                                                 AT24CXX_MEMADD_SIZE,
                                                 buf,
                                                 len,
                                                 AT24CXX_I2C_TIMEOUT);
    if (status != HAL_OK)
        return AT24CXX_ERR_DEVICE;

    return AT24CXX_OK;
}

/* 页写入（单次最多 AT24CXX_PAGE_SIZE 字节）
 * 如果数据跨页，硬件会自动回绕到页首，可能导致数据覆盖，
 * 建议使用 AT24Cxx_Write() 处理跨页写入 */
AT24CxxErr_t AT24Cxx_PageWrite(uint16_t addr, const uint8_t *buf, uint16_t len)
{
    /* 参数检查 */
    if (buf == NULL || len == 0)
        return AT24CXX_ERR_PARAM;

    if (len > AT24CXX_PAGE_SIZE)
        return AT24CXX_ERR_PARAM;

    /* 地址越界检查 */
    if (!AT24Cxx_CheckAddr(addr, len))
        return AT24CXX_ERR_ADDR;

    /* 等待前一次写操作完成，超时说明设备无应答，中止本次写入 */
    AT24CxxErr_t ret = AT24Cxx_WaitWriteComplete();
    if (ret != AT24CXX_OK)
        return ret;

    /* 使用 HAL 库的 Mem_Write 函数 */
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(&AT24CXX_I2C,
                                                 AT24CXX_WRITE_ADDR,
                                                 addr,
                                                 AT24CXX_MEMADD_SIZE,
                                                 (uint8_t *)buf, /* HAL 形参非 const，驱动内唯一转换点 */
                                                 len,
                                                 AT24CXX_I2C_TIMEOUT);
    if (status != HAL_OK)
        return AT24CXX_ERR_DEVICE;

    /* 写操作完成后，芯片进入内部写周期，不需要立即等待，但后续操作前会等待 */
    return AT24CXX_OK;
}

/* 任意地址写入任意长度数据（自动处理跨页）
 * 自动将数据拆分为多次页写入，处理页边界，每次页写入后等待写周期完成 */
AT24CxxErr_t AT24Cxx_Write(uint16_t addr, const uint8_t *buf, uint16_t len)
{
    uint16_t remaining = len;
    uint16_t current_addr = addr;
    const uint8_t *current_buf = buf;

    /* 参数检查 */
    if (buf == NULL || len == 0)
        return AT24CXX_ERR_PARAM;

    /* 地址越界检查 */
    if (!AT24Cxx_CheckAddr(addr, len))
        return AT24CXX_ERR_ADDR;

    while (remaining > 0)
    {
        /* 计算当前页剩余空间 */
        uint16_t page_offset = current_addr & (AT24CXX_PAGE_SIZE - 1);
        uint16_t page_remain = AT24CXX_PAGE_SIZE - page_offset;

        /* 本次可写入的长度 */
        uint16_t chunk = (remaining > page_remain) ? page_remain : remaining;

        /* 执行页写入 */
        AT24CxxErr_t ret = AT24Cxx_PageWrite(current_addr, current_buf, chunk);
        if (ret != AT24CXX_OK)
            return ret;

        /* 等待本次写周期完成（PageWrite 内部等待发生在写入前，写后主动等待更可靠） */
        ret = AT24Cxx_WaitWriteComplete();
        if (ret != AT24CXX_OK)
            return ret;

        /* 更新指针和剩余长度 */
        current_addr += chunk;
        current_buf += chunk;
        remaining -= chunk;
    }

    return AT24CXX_OK;
}

/* ========== 记录层（A/B 双分区，掉电安全） ========== */

/* CRC32（IEEE 反射式）流式更新 */
static uint32_t AT24Cxx_Crc32Update(uint32_t crc, const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8U; bit++)
        {
            if (crc & 1U)
                crc = (crc >> 1) ^ AT24CXX_CRC32_POLY;
            else
                crc >>= 1;
        }
    }
    return crc;
}

/* 取记录槽起始地址（idx = 0/1） */
static uint16_t AT24Cxx_SlotBase(uint8_t idx)
{
    return (uint16_t)((uint32_t)idx * AT24CXX_SLOT_SIZE);
}

/* 读槽头并校验 magic/长度/CRC，1 = 有效
 * idx: 槽号; seq/len: 输出头字段 */
static uint8_t AT24Cxx_VerifySlot(uint8_t idx, uint32_t *seq, uint16_t *len)
{
    uint8_t hdr[AT24CXX_REC_HDR_SIZE];
    uint16_t base = AT24Cxx_SlotBase(idx);
    uint32_t magic = 0;
    uint32_t stored_crc = 0;

    if (AT24Cxx_Read(base, hdr, sizeof(hdr)) != AT24CXX_OK)
        return 0;

    memcpy(&magic, &hdr[0], 4);
    memcpy(seq, &hdr[4], 4);
    memcpy(len, &hdr[8], 2);
    memcpy(&stored_crc, &hdr[10], 4);

    if (magic != AT24CXX_REC_MAGIC || *len == 0 || *len > AT24CXX_REC_DATA_MAX)
        return 0;

    /* CRC 覆盖头前 10 字节与全部数据，分块流式读取 */
    uint32_t crc = AT24CXX_CRC32_INIT;
    crc = AT24Cxx_Crc32Update(crc, hdr, 10);
    uint16_t remaining = *len;
    uint16_t off = 0;
    while (remaining > 0)
    {
        uint16_t chunk = (remaining > sizeof(s_RecBuf)) ? sizeof(s_RecBuf) : remaining;
        if (AT24Cxx_Read(base + AT24CXX_REC_HDR_SIZE + off, s_RecBuf, chunk) != AT24CXX_OK)
            return 0;
        crc = AT24Cxx_Crc32Update(crc, s_RecBuf, chunk);
        off += chunk;
        remaining -= chunk;
    }
    crc ^= 0xFFFFFFFFU;

    return (crc == stored_crc) ? 1 : 0;
}

/* 保存记录（A/B 双分区提交）：数据先写非活动槽、头部最后写作为提交点
 * 任一步掉电旧活动槽都完整；旧记录所在槽成为下一次写入的目标（自动轮转）
 * data/len: 记录内容（len ≤ AT24CXX_REC_DATA_MAX） */
AT24CxxErr_t AT24Cxx_SaveRecord(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0 || len > AT24CXX_REC_DATA_MAX)
        return AT24CXX_ERR_PARAM;

    uint32_t seq_a = 0;
    uint32_t seq_b = 0;
    uint16_t tmp_len = 0;
    uint8_t valid_a = AT24Cxx_VerifySlot(0, &seq_a, &tmp_len);
    uint8_t valid_b = AT24Cxx_VerifySlot(1, &seq_b, &tmp_len);

    /* 目标槽 = 活动槽对面；序号 = 历史最大 + 1；双槽无效从槽 0 起新 */
    uint8_t next;
    uint32_t seq;
    if (valid_a && (!valid_b || seq_a >= seq_b))
    {
        next = 1;
        seq = seq_a + 1U;
    }
    else if (valid_b)
    {
        next = 0;
        seq = seq_b + 1U;
    }
    else
    {
        next = 0;
        seq = 1U;
    }

    uint16_t base = AT24Cxx_SlotBase(next);

    /* 1. 数据先落非活动槽（此刻掉电 → 旧活动槽仍有效） */
    AT24CxxErr_t ret = AT24Cxx_Write(base + AT24CXX_REC_HDR_SIZE, data, len);
    if (ret != AT24CXX_OK)
        return ret;

    /* 2. 头部最后写 = 提交点（CRC 覆盖头前 10 字节与数据） */
    uint8_t hdr[AT24CXX_REC_HDR_SIZE];
    uint32_t magic = AT24CXX_REC_MAGIC;
    memcpy(&hdr[0], &magic, 4);
    memcpy(&hdr[4], &seq, 4);
    memcpy(&hdr[8], &len, 2);

    uint32_t crc = AT24CXX_CRC32_INIT;
    crc = AT24Cxx_Crc32Update(crc, hdr, 10);
    crc = AT24Cxx_Crc32Update(crc, data, len);
    crc ^= 0xFFFFFFFFU;
    memcpy(&hdr[10], &crc, 4);

    return AT24Cxx_Write(base, hdr, sizeof(hdr));
}

/* 读出最新有效记录：双槽各验 CRC 取 seq 大者
 * data/buf_max: 输出缓冲及容量; out_len: 实际记录长度
 * 返回: OK / NOT_FOUND 从未保存或双槽皆无效 / PARAM 记录长于缓冲（布局不符）
 *       / DEVICE 读失败 */
AT24CxxErr_t AT24Cxx_LoadRecord(uint8_t *data, uint16_t buf_max, uint16_t *out_len)
{
    if (data == NULL || out_len == NULL)
        return AT24CXX_ERR_PARAM;

    uint32_t seq_a = 0;
    uint32_t seq_b = 0;
    uint16_t len_a = 0;
    uint16_t len_b = 0;
    uint8_t valid_a = AT24Cxx_VerifySlot(0, &seq_a, &len_a);
    uint8_t valid_b = AT24Cxx_VerifySlot(1, &seq_b, &len_b);

    if (!valid_a && !valid_b)
        return AT24CXX_ERR_NOT_FOUND;

    uint8_t winner;
    uint16_t len;
    if (valid_a && (!valid_b || seq_a >= seq_b))
    {
        winner = 0;
        len = len_a;
    }
    else
    {
        winner = 1;
        len = len_b;
    }

    if (len > buf_max)
        return AT24CXX_ERR_PARAM;

    if (AT24Cxx_Read(AT24Cxx_SlotBase(winner) + AT24CXX_REC_HDR_SIZE, data, len) != AT24CXX_OK)
        return AT24CXX_ERR_DEVICE;

    *out_len = len;
    return AT24CXX_OK;
}

/* ========== 使用示例 ========== */
#if AT24CXX_ENABLE_EXAMPLE

/* AT24CXX 使用示例，展示记录层完整流程：
 * 初始化芯片 -> 获取芯片信息 -> 保存记录（A/B 双分区）-> 读出记录 -> 验证数据 */
void AT24Cxx_Example(void)
{
    /* 测试数据 */
    uint8_t write_buf[] = "Hello AT24CXX!";
    uint8_t read_buf[sizeof(write_buf)];
    uint16_t out_len = 0;
    AT24CxxInfo_t info;
    AT24CxxErr_t ret;

    /* 1. 初始化 */
    ret = AT24Cxx_Init();
    if (ret != AT24CXX_OK)
    {
        printf("AT24CXX Init Failed! Error: %d\r\n", ret);
        return;
    }

    /* 2. 获取芯片信息 */
    AT24Cxx_GetInfo(&info);
    printf("========== AT24CXX Info ==========\r\n");
    printf("Device Address: 0x%02X\r\n", info.dev_addr);
    printf("Capacity:       %lu bytes\r\n", info.size_bytes);
    printf("Page Size:      %lu bytes\r\n", info.page_size);
    printf("Page Count:     %lu\r\n", info.page_count);
    printf("==================================\r\n\r\n");

    /* 3. 保存记录（内部自动 A/B 轮转，掉电安全） */
    printf("Saving record %zu bytes...\r\n", sizeof(write_buf));
    ret = AT24Cxx_SaveRecord(write_buf, sizeof(write_buf));
    if (ret != AT24CXX_OK)
    {
        printf("SaveRecord Failed! Error: %d\r\n", ret);
        return;
    }
    printf("SaveRecord OK!\r\n\r\n");

    /* 4. 读出记录 */
    memset(read_buf, 0, sizeof(read_buf));
    printf("Loading record...\r\n");
    ret = AT24Cxx_LoadRecord(read_buf, sizeof(read_buf), &out_len);
    if (ret != AT24CXX_OK)
    {
        printf("LoadRecord Failed! Error: %d\r\n", ret);
        return;
    }
    printf("LoadRecord OK! len=%u\r\n\r\n", out_len);

    /* 5. 验证数据 */
    if (out_len == sizeof(write_buf) && memcmp(write_buf, read_buf, out_len) == 0)
    {
        printf("Data verified OK!\r\n");
        printf("Written: \"%s\"\r\n", write_buf);
        printf("Read:    \"%s\"\r\n", read_buf);
    }
    else
    {
        printf("Data verification FAILED!\r\n");
    }
}

#endif /* AT24CXX_ENABLE_EXAMPLE */
