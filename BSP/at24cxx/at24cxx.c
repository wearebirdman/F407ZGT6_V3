#include "at24cxx.h"
#include "i2c.h"

#include <stdio.h>
#include <string.h>

/* ========== 内部宏定义 ========== */
#define AT24CXX_WRITE_ADDR       ((AT24CXX_DEV_ADDR << 1) & 0xFE) /* 写地址（7位地址左移1位，最低位为0） */
#define AT24CXX_I2C_TIMEOUT      100                              /* I2C 超时时间（ms） */
#define AT24CXX_WRITE_TIMEOUT    10                               /* 写周期等待超时（典型最大 5ms，留余量） */
#define AT24CXX_DEV_CHECK_TRIALS 3                                /* 检测设备应答重试次数 */

/* ========== 内部函数声明 ========== */
static uint8_t AT24CXX_CheckDevice(void);
static AT24CXXErr_t AT24CXX_WaitWriteComplete(void);
static uint8_t AT24CXX_CheckAddr(uint16_t addr, uint16_t len);

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
static AT24CXXErr_t AT24CXX_WaitWriteComplete(void)
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
static uint8_t AT24CXX_CheckAddr(uint16_t addr, uint16_t len)
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
AT24CXXErr_t AT24CXX_Init(void)
{
    if (!AT24CXX_CheckDevice())
        return AT24CXX_ERR_DEVICE;
    return AT24CXX_OK;
}

/* 获取芯片信息 */
void AT24CXX_GetInfo(AT24CXXInfo_t *info)
{
    if (info == NULL)
        return;
    info->size_bytes = AT24CXX_CHIP_SIZE;
    info->page_size = AT24CXX_PAGE_SIZE;
    info->page_count = AT24CXX_PAGE_COUNT;
    info->dev_addr = AT24CXX_DEV_ADDR;
}

/* 从指定地址读取数据（addr: 0 ~ AT24CXX_CHIP_SIZE-1） */
AT24CXXErr_t AT24CXX_Read(uint16_t addr, uint8_t *buf, uint16_t len)
{
    /* 参数检查 */
    if (buf == NULL || len == 0)
        return AT24CXX_ERR_PARAM;

    /* 地址越界检查 */
    if (!AT24CXX_CheckAddr(addr, len))
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
 * 建议使用 AT24CXX_Write() 处理跨页写入 */
AT24CXXErr_t AT24CXX_PageWrite(uint16_t addr, uint8_t *buf, uint16_t len)
{
    /* 参数检查 */
    if (buf == NULL || len == 0)
        return AT24CXX_ERR_PARAM;

    if (len > AT24CXX_PAGE_SIZE)
        return AT24CXX_ERR_PARAM;

    /* 地址越界检查 */
    if (!AT24CXX_CheckAddr(addr, len))
        return AT24CXX_ERR_ADDR;

    /* 等待前一次写操作完成，超时说明设备无应答，中止本次写入 */
    AT24CXXErr_t ret = AT24CXX_WaitWriteComplete();
    if (ret != AT24CXX_OK)
        return ret;

    /* 使用 HAL 库的 Mem_Write 函数 */
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(&AT24CXX_I2C,
                                                 AT24CXX_WRITE_ADDR,
                                                 addr,
                                                 AT24CXX_MEMADD_SIZE,
                                                 buf,
                                                 len,
                                                 AT24CXX_I2C_TIMEOUT);
    if (status != HAL_OK)
        return AT24CXX_ERR_DEVICE;

    /* 写操作完成后，芯片进入内部写周期，不需要立即等待，但后续操作前会等待 */
    return AT24CXX_OK;
}

/* 任意地址写入任意长度数据（自动处理跨页）
 * 自动将数据拆分为多次页写入，处理页边界，每次页写入后等待写周期完成 */
AT24CXXErr_t AT24CXX_Write(uint16_t addr, uint8_t *buf, uint16_t len)
{
    uint16_t remaining = len;
    uint16_t current_addr = addr;
    uint8_t *current_buf = buf;

    /* 参数检查 */
    if (buf == NULL || len == 0)
        return AT24CXX_ERR_PARAM;

    /* 地址越界检查 */
    if (!AT24CXX_CheckAddr(addr, len))
        return AT24CXX_ERR_ADDR;

    while (remaining > 0)
    {
        /* 计算当前页剩余空间 */
        uint16_t page_offset = current_addr & (AT24CXX_PAGE_SIZE - 1);
        uint16_t page_remain = AT24CXX_PAGE_SIZE - page_offset;

        /* 本次可写入的长度 */
        uint16_t chunk = (remaining > page_remain) ? page_remain : remaining;

        /* 执行页写入 */
        AT24CXXErr_t ret = AT24CXX_PageWrite(current_addr, current_buf, chunk);
        if (ret != AT24CXX_OK)
            return ret;

        /* 等待本次写周期完成（PageWrite 内部等待发生在写入前，写后主动等待更可靠） */
        ret = AT24CXX_WaitWriteComplete();
        if (ret != AT24CXX_OK)
            return ret;

        /* 更新指针和剩余长度 */
        current_addr += chunk;
        current_buf += chunk;
        remaining -= chunk;
    }

    return AT24CXX_OK;
}

/* ========== 使用示例 ========== */
#if AT24CXX_ENABLE_EXAMPLE

/* AT24CXX 使用示例，展示完整的读写流程：
 * 初始化芯片 -> 获取芯片信息 -> 写入数据（自动跨页）-> 读取数据 -> 验证数据 */
void AT24CXX_Example(void)
{
    /* 测试数据 */
    uint8_t write_buf[] = "Hello AT24CXX!";
    uint8_t read_buf[sizeof(write_buf)];
    AT24CXXInfo_t info;
    AT24CXXErr_t ret;

    /* 1. 初始化 */
    ret = AT24CXX_Init();
    if (ret != AT24CXX_OK)
    {
        printf("AT24CXX Init Failed! Error: %d\r\n", ret);
        return;
    }

    /* 2. 获取芯片信息 */
    AT24CXX_GetInfo(&info);
    printf("========== AT24CXX Info ==========\r\n");
    printf("Device Address: 0x%02X\r\n", info.dev_addr);
    printf("Capacity:       %lu bytes\r\n", info.size_bytes);
    printf("Page Size:      %lu bytes\r\n", info.page_size);
    printf("Page Count:     %lu\r\n", info.page_count);
    printf("==================================\r\n\r\n");

    /* 3. 写入数据（使用跨页写入函数） */
    printf("Writing %zu bytes to address 0x%04X...\r\n", sizeof(write_buf), AT24CXX_TEST_ADDR);
    ret = AT24CXX_Write(AT24CXX_TEST_ADDR, write_buf, sizeof(write_buf));
    if (ret != AT24CXX_OK)
    {
        printf("Write Failed! Error: %d\r\n", ret);
        return;
    }
    printf("Write OK!\r\n\r\n");

    /* 4. 读取数据 */
    memset(read_buf, 0, sizeof(read_buf));
    printf("Reading %zu bytes from address 0x%04X...\r\n", sizeof(read_buf), AT24CXX_TEST_ADDR);
    ret = AT24CXX_Read(AT24CXX_TEST_ADDR, read_buf, sizeof(read_buf));
    if (ret != AT24CXX_OK)
    {
        printf("Read Failed! Error: %d\r\n", ret);
        return;
    }
    printf("Read OK!\r\n\r\n");

    /* 5. 验证数据 */
    if (memcmp(write_buf, read_buf, sizeof(write_buf)) == 0)
    {
        printf("Data verified OK!\r\n");
        printf("Written: \"%s\"\r\n", write_buf);
        printf("Read:    \"%s\"\r\n", read_buf);
    }
    else
    {
        printf("Data verification FAILED!\r\n");
        printf("Written: \"%s\"\r\n", write_buf);
        printf("Read:    \"%s\"\r\n", read_buf);
    }
}

#endif /* AT24CXX_ENABLE_EXAMPLE */
