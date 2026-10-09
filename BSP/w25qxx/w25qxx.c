#include "w25qxx.h"
#include "spi.h"

#include <stdio.h>
#include <string.h>

/* ========== 内部宏定义 ========== */
#define W25QXX_CS_LOW()  HAL_GPIO_WritePin(W25QXX_CS_PORT, W25QXX_CS_PIN, GPIO_PIN_RESET)
#define W25QXX_CS_HIGH() HAL_GPIO_WritePin(W25QXX_CS_PORT, W25QXX_CS_PIN, GPIO_PIN_SET)

#define W25QXX_EXPECTED_MF_ID   0xEF /* 制造商 ID（Winbond） */
#define W25QXX_EXPECTED_TYPE_ID 0x40 /* 存储类型 ID（W25Qxx 系列） */

/* ========== 超时时间配置（单位：ms） ========== */
#define W25QXX_TIMEOUT_GENERIC      100   /* 通用操作超时（读、写） */
#define W25QXX_TIMEOUT_SECTOR_ERASE 500   /* 扇区擦除超时（典型 40~400ms） */
#define W25QXX_TIMEOUT_BLOCK_ERASE  1000  /* 块擦除超时 */
#define W25QXX_TIMEOUT_CHIP_ERASE   90000 /* 整片擦除超时（典型 40~80 秒） */
#define W25QXX_SPI_TIMEOUT          100   /* SPI 单字节超时（ms） */

/* ========== 内部函数声明 ========== */
static HAL_StatusTypeDef W25QXX_SPI_RW(uint8_t tx, uint8_t *rx);
static uint8_t W25QXX_CheckAddr(uint32_t addr, uint32_t len);
static W25QXXErr_t W25QXX_ReadStatus1Checked(uint8_t *sr1);
static uint8_t W25QXX_CheckProtected(void);
static W25QXXErr_t W25QXX_WaitBusyTimeout(uint32_t timeout_ms);

/* ========== SPI 底层操作 ========== */

/* SPI 单字节收发（内部使用）
 * rx: 接收字节输出，不需要接收值时传 NULL
 * 返回: HAL 传输状态，失败由调用方映射为 W25QXX_ERR_TIMEOUT */
static HAL_StatusTypeDef W25QXX_SPI_RW(uint8_t tx, uint8_t *rx)
{
    uint8_t dummy;
    return HAL_SPI_TransmitReceive(&W25QXX_SPI, &tx, (rx != NULL) ? rx : &dummy, 1, W25QXX_SPI_TIMEOUT);
}

/* ========== 内部辅助函数 ========== */

/* 检查地址是否越界，0 = 越界, 1 = 合法 */
static uint8_t W25QXX_CheckAddr(uint32_t addr, uint32_t len)
{
    if (len == 0)
        return 1;
    if (addr >= W25QXX_CHIP_SIZE)
        return 0;
    if (addr + len > W25QXX_CHIP_SIZE)
        return 0;
    return 1;
}

/* 检查芯片是否处于写保护状态（BP 位非零），0 = 未保护, 1 = 已保护
 * SPI 失败时返回 0（未保护）：通信错误交由后续 WriteEnable 如实上报超时，
 * 避免把掉线芯片误判成写保护 */
static uint8_t W25QXX_CheckProtected(void)
{
    uint8_t sr1;
    if (W25QXX_ReadStatus1Checked(&sr1) != W25QXX_OK)
        return 0;
    /* 检查 BP0~BP2 是否全为 0 */
    if (sr1 & (W25QXX_SR1_BP0 | W25QXX_SR1_BP1 | W25QXX_SR1_BP2))
        return 1;
    return 0;
}

/* 读状态寄存器 1（带链路状态检查，供忙等待/写保护判断内部使用）
 * 返回: SPI 失败为 W25QXX_ERR_TIMEOUT，成功时读取值写入 *sr1 */
static W25QXXErr_t W25QXX_ReadStatus1Checked(uint8_t *sr1)
{
    W25QXX_CS_LOW();
    HAL_StatusTypeDef st = W25QXX_SPI_RW(W25QXX_READ_STATUS1, NULL);
    if (st == HAL_OK)
        st = W25QXX_SPI_RW(0xFF, sr1);
    W25QXX_CS_HIGH();
    return (st == HAL_OK) ? W25QXX_OK : W25QXX_ERR_TIMEOUT;
}

/* 等待芯片空闲（带超时参数）
 * 返回: W25QXX_OK = 已空闲；W25QXX_ERR_TIMEOUT = 超时仍忙或 SPI 读取失败 */
static W25QXXErr_t W25QXX_WaitBusyTimeout(uint32_t timeout_ms)
{
    for (uint32_t elapsed = 0; elapsed < timeout_ms; elapsed++)
    {
        uint8_t sr1;
        W25QXXErr_t ret = W25QXX_ReadStatus1Checked(&sr1);
        if (ret != W25QXX_OK)
            return ret;
        if (!(sr1 & W25QXX_SR1_BUSY))
            return W25QXX_OK;
        HAL_Delay(1);
    }
    return W25QXX_ERR_TIMEOUT;
}

/* ========== 初始化与信息读取 ========== */

/* 初始化 W25QXX，验证制造商 ID / 存储类型 / 容量 ID 是否匹配 */
W25QXXErr_t W25QXX_Init(void)
{
    uint32_t id = W25QXX_ReadID();

    /* 检查制造商 ID 是否为 Winbond (0xEF) */
    uint8_t manufacturer = (id >> 16) & 0xFF;
    if (manufacturer != W25QXX_EXPECTED_MF_ID)
        return W25QXX_ERR_ID;

    /* 检查存储类型是否为 W25QXX 系列 (0x40) */
    uint8_t memory_type = (id >> 8) & 0xFF;
    if (memory_type != W25QXX_EXPECTED_TYPE_ID)
        return W25QXX_ERR_ID;

    /* 检查容量 ID 是否匹配当前定义的芯片型号（不匹配可能是型号选择错误） */
    uint8_t capacity = id & 0xFF;
    uint8_t expected_capacity = W25QXX_EXPECTED_ID & 0xFF;
    if (capacity != expected_capacity)
        return W25QXX_ERR_ID;

    /* 退出掉电模式（如果进入） */
    W25QXXErr_t ret = W25QXX_WakeUp();
    if (ret != W25QXX_OK)
        return ret;

    /* 清除写使能 */
    return W25QXX_WriteDisable();
}

/* 获取芯片信息 */
void W25QXX_GetInfo(W25QXXInfo_t *info)
{
    if (info == NULL)
        return;

    info->jedec_id = W25QXX_ReadID();
    info->manufacturer = (info->jedec_id >> 16) & 0xFF;
    info->memory_type = (info->jedec_id >> 8) & 0xFF;
    info->capacity = info->jedec_id & 0xFF;

    /* 使用宏定义的容量参数（编译时计算） */
    info->size_kb = W25QXX_CHIP_SIZE / 1024;
    info->size_mb = W25QXX_CHIP_SIZE / 1024 / 1024;
    info->sector_count = W25QXX_SECTOR_COUNT;
    info->page_count = W25QXX_PAGE_COUNT;
    info->block_count = W25QXX_BLOCK_COUNT;
}

/* 读取芯片 JEDEC ID，返回 24 位 ID（制造商 | 存储类型 | 容量），SPI 失败返回 0 */
uint32_t W25QXX_ReadID(void)
{
    uint8_t id[3];
    W25QXX_CS_LOW();
    HAL_StatusTypeDef st = W25QXX_SPI_RW(W25QXX_JEDEC_ID, NULL);
    for (uint8_t i = 0; i < 3 && st == HAL_OK; i++)
        st = W25QXX_SPI_RW(0xFF, &id[i]);
    W25QXX_CS_HIGH();
    if (st != HAL_OK)
        return 0;
    return ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
}

/* ========== 状态寄存器操作 ========== */

/* 读取状态寄存器 1（SPI 失败返回 0xFF：BUSY 位置 1，使 WaitBusy 如实报超时） */
uint8_t W25QXX_ReadStatus1(void)
{
    uint8_t sta;
    if (W25QXX_ReadStatus1Checked(&sta) != W25QXX_OK)
        return 0xFF;
    return sta;
}

/* 读取状态寄存器 2（SPI 失败返回 0xFF） */
uint8_t W25QXX_ReadStatus2(void)
{
    W25QXX_CS_LOW();
    HAL_StatusTypeDef st = W25QXX_SPI_RW(W25QXX_READ_STATUS2, NULL);
    uint8_t sta = 0xFF;
    if (st == HAL_OK)
        st = W25QXX_SPI_RW(0xFF, &sta);
    W25QXX_CS_HIGH();
    return (st == HAL_OK) ? sta : 0xFF;
}

/* 读取状态寄存器 3（SPI 失败返回 0xFF） */
uint8_t W25QXX_ReadStatus3(void)
{
    W25QXX_CS_LOW();
    HAL_StatusTypeDef st = W25QXX_SPI_RW(W25QXX_READ_STATUS3, NULL);
    uint8_t sta = 0xFF;
    if (st == HAL_OK)
        st = W25QXX_SPI_RW(0xFF, &sta);
    W25QXX_CS_HIGH();
    return (st == HAL_OK) ? sta : 0xFF;
}

/* 检查芯片是否忙碌，0 = 空闲, 1 = 忙碌 */
uint8_t W25QXX_IsBusy(void)
{
    return (W25QXX_ReadStatus1() & W25QXX_SR1_BUSY) ? 1 : 0;
}

/* 检查写使能状态，0 = 未使能, 1 = 已使能 */
uint8_t W25QXX_IsWriteEnabled(void)
{
    return (W25QXX_ReadStatus1() & W25QXX_SR1_WEL) ? 1 : 0;
}

/* ========== 写使能与等待 ========== */

/* 写使能（任何写入/擦除操作前必须调用） */
W25QXXErr_t W25QXX_WriteEnable(void)
{
    W25QXX_CS_LOW();
    HAL_StatusTypeDef st = W25QXX_SPI_RW(W25QXX_WRITE_EN, NULL);
    W25QXX_CS_HIGH();
    return (st == HAL_OK) ? W25QXX_OK : W25QXX_ERR_TIMEOUT;
}

/* 写禁止 */
W25QXXErr_t W25QXX_WriteDisable(void)
{
    W25QXX_CS_LOW();
    HAL_StatusTypeDef st = W25QXX_SPI_RW(W25QXX_WRITE_DIS, NULL);
    W25QXX_CS_HIGH();
    return (st == HAL_OK) ? W25QXX_OK : W25QXX_ERR_TIMEOUT;
}

/* 等待芯片空闲（使用默认超时时间 W25QXX_TIMEOUT_GENERIC） */
W25QXXErr_t W25QXX_WaitBusy(void)
{
    return W25QXX_WaitBusyTimeout(W25QXX_TIMEOUT_GENERIC);
}

/* ========== 数据读取 ========== */

/* 从指定地址读取数据（addr: 0 ~ W25QXX_CHIP_SIZE-1） */
W25QXXErr_t W25QXX_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    /* 参数检查 */
    if (buf == NULL || len == 0)
        return W25QXX_ERR_PARAM;

    /* 地址越界检查 */
    if (!W25QXX_CheckAddr(addr, len))
        return W25QXX_ERR_ADDR;

    W25QXX_CS_LOW();
    HAL_StatusTypeDef st = W25QXX_SPI_RW(W25QXX_READ, NULL);
    if (st == HAL_OK)
        st = W25QXX_SPI_RW((uint8_t)((addr >> 16) & 0xFF), NULL);
    if (st == HAL_OK)
        st = W25QXX_SPI_RW((uint8_t)((addr >> 8) & 0xFF), NULL);
    if (st == HAL_OK)
        st = W25QXX_SPI_RW((uint8_t)(addr & 0xFF), NULL);

    for (uint32_t i = 0; i < len && st == HAL_OK; i++)
        st = W25QXX_SPI_RW(0xFF, &buf[i]);

    W25QXX_CS_HIGH();
    return (st == HAL_OK) ? W25QXX_OK : W25QXX_ERR_TIMEOUT;
}

/* ========== 页写入 ========== */

/* 页写入（单次最多 256 字节）
 * 如果数据跨页，硬件会自动回绕到页首，可能导致数据覆盖，
 * 调用者应使用 W25QXX_Write() 处理跨页 */
W25QXXErr_t W25QXX_PageWrite(uint32_t addr, uint8_t *buf, uint16_t len)
{
    /* 参数检查 */
    if (buf == NULL || len == 0)
        return W25QXX_ERR_PARAM;

    if (len > W25QXX_PAGE_SIZE)
        return W25QXX_ERR_PARAM;

    /* 地址越界检查 */
    if (!W25QXX_CheckAddr(addr, len))
        return W25QXX_ERR_ADDR;

    /* 检查写保护 */
    if (W25QXX_CheckProtected())
        return W25QXX_ERR_PROTECTED;

    W25QXXErr_t ret = W25QXX_WriteEnable();
    if (ret != W25QXX_OK)
        return ret;

    W25QXX_CS_LOW();
    HAL_StatusTypeDef st = W25QXX_SPI_RW(W25QXX_PAGE_PROGRAM, NULL);
    if (st == HAL_OK)
        st = W25QXX_SPI_RW((uint8_t)((addr >> 16) & 0xFF), NULL);
    if (st == HAL_OK)
        st = W25QXX_SPI_RW((uint8_t)((addr >> 8) & 0xFF), NULL);
    if (st == HAL_OK)
        st = W25QXX_SPI_RW((uint8_t)(addr & 0xFF), NULL);

    for (uint16_t i = 0; i < len && st == HAL_OK; i++)
        st = W25QXX_SPI_RW(buf[i], NULL);

    W25QXX_CS_HIGH();
    if (st != HAL_OK)
        return W25QXX_ERR_TIMEOUT;

    return W25QXX_WaitBusy();
}

/* 任意地址写入任意长度数据（自动处理跨页）
 * 自动将数据拆分为多次页写入，处理页边界。
 * 注意：写入前需要确保目标地址所在的扇区已被擦除 */
W25QXXErr_t W25QXX_Write(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint32_t remaining = len;
    uint32_t current_addr = addr;
    uint8_t *current_buf = buf;

    /* 参数检查 */
    if (buf == NULL || len == 0)
        return W25QXX_ERR_PARAM;

    /* 地址越界检查 */
    if (!W25QXX_CheckAddr(addr, len))
        return W25QXX_ERR_ADDR;

    /* 检查写保护 */
    if (W25QXX_CheckProtected())
        return W25QXX_ERR_PROTECTED;

    while (remaining > 0)
    {
        /* 计算当前页剩余空间 */
        uint16_t page_offset = current_addr & (W25QXX_PAGE_SIZE - 1);
        uint16_t page_remain = W25QXX_PAGE_SIZE - page_offset;

        /* 本次可写入的长度 */
        uint16_t chunk = (remaining > page_remain) ? page_remain : remaining;

        /* 执行页写入 */
        W25QXXErr_t ret = W25QXX_PageWrite(current_addr, current_buf, chunk);
        if (ret != W25QXX_OK)
            return ret;

        /* 更新指针和剩余长度 */
        current_addr += chunk;
        current_buf += chunk;
        remaining -= chunk;
    }

    return W25QXX_OK;
}

/* ========== 擦除操作 ========== */

/* 扇区擦除（4KB），addr 为扇区内任意地址（自动对齐到扇区首地址） */
W25QXXErr_t W25QXX_SectorErase(uint32_t addr)
{
    /* 地址越界检查（先查原始地址，避免对齐回绕漏检） */
    if (addr >= W25QXX_CHIP_SIZE)
        return W25QXX_ERR_ADDR;

    /* 自动对齐到扇区首地址 */
    uint32_t sector_addr = W25QXX_SECTOR_ALIGN(addr);

    /* 检查写保护 */
    if (W25QXX_CheckProtected())
        return W25QXX_ERR_PROTECTED;

    W25QXXErr_t ret = W25QXX_WriteEnable();
    if (ret != W25QXX_OK)
        return ret;

    W25QXX_CS_LOW();
    HAL_StatusTypeDef st = W25QXX_SPI_RW(W25QXX_SECTOR_ERASE, NULL);
    if (st == HAL_OK)
        st = W25QXX_SPI_RW((uint8_t)((sector_addr >> 16) & 0xFF), NULL);
    if (st == HAL_OK)
        st = W25QXX_SPI_RW((uint8_t)((sector_addr >> 8) & 0xFF), NULL);
    if (st == HAL_OK)
        st = W25QXX_SPI_RW((uint8_t)(sector_addr & 0xFF), NULL);
    W25QXX_CS_HIGH();
    if (st != HAL_OK)
        return W25QXX_ERR_TIMEOUT;

    /* 扇区擦除典型时间：40~400ms，使用 500ms 超时 */
    return W25QXX_WaitBusyTimeout(W25QXX_TIMEOUT_SECTOR_ERASE);
}

/* 块擦除（32KB），addr 为块内任意地址（自动对齐到 32KB 块首地址） */
W25QXXErr_t W25QXX_BlockErase32K(uint32_t addr)
{
    /* 地址越界检查（先查原始地址，避免对齐回绕漏检） */
    if (addr >= W25QXX_CHIP_SIZE)
        return W25QXX_ERR_ADDR;

    /* 自动对齐到 32KB 块首地址 */
    uint32_t block_addr = W25QXX_BLOCK_32K_ALIGN(addr);

    /* 检查写保护 */
    if (W25QXX_CheckProtected())
        return W25QXX_ERR_PROTECTED;

    W25QXXErr_t ret = W25QXX_WriteEnable();
    if (ret != W25QXX_OK)
        return ret;

    W25QXX_CS_LOW();
    HAL_StatusTypeDef st = W25QXX_SPI_RW(W25QXX_BLOCK_ERASE_32K, NULL);
    if (st == HAL_OK)
        st = W25QXX_SPI_RW((uint8_t)((block_addr >> 16) & 0xFF), NULL);
    if (st == HAL_OK)
        st = W25QXX_SPI_RW((uint8_t)((block_addr >> 8) & 0xFF), NULL);
    if (st == HAL_OK)
        st = W25QXX_SPI_RW((uint8_t)(block_addr & 0xFF), NULL);
    W25QXX_CS_HIGH();
    if (st != HAL_OK)
        return W25QXX_ERR_TIMEOUT;

    /* 块擦除典型时间：200~2000ms，使用 1000ms 超时 */
    return W25QXX_WaitBusyTimeout(W25QXX_TIMEOUT_BLOCK_ERASE);
}

/* 块擦除（64KB），addr 为块内任意地址（自动对齐到 64KB 块首地址） */
W25QXXErr_t W25QXX_BlockErase64K(uint32_t addr)
{
    /* 地址越界检查（先查原始地址，避免对齐回绕漏检） */
    if (addr >= W25QXX_CHIP_SIZE)
        return W25QXX_ERR_ADDR;

    /* 自动对齐到 64KB 块首地址 */
    uint32_t block_addr = W25QXX_BLOCK_ALIGN(addr);

    /* 检查写保护 */
    if (W25QXX_CheckProtected())
        return W25QXX_ERR_PROTECTED;

    W25QXXErr_t ret = W25QXX_WriteEnable();
    if (ret != W25QXX_OK)
        return ret;

    W25QXX_CS_LOW();
    HAL_StatusTypeDef st = W25QXX_SPI_RW(W25QXX_BLOCK_ERASE_64K, NULL);
    if (st == HAL_OK)
        st = W25QXX_SPI_RW((uint8_t)((block_addr >> 16) & 0xFF), NULL);
    if (st == HAL_OK)
        st = W25QXX_SPI_RW((uint8_t)((block_addr >> 8) & 0xFF), NULL);
    if (st == HAL_OK)
        st = W25QXX_SPI_RW((uint8_t)(block_addr & 0xFF), NULL);
    W25QXX_CS_HIGH();
    if (st != HAL_OK)
        return W25QXX_ERR_TIMEOUT;

    /* 块擦除典型时间：200~2000ms，使用 1000ms 超时 */
    return W25QXX_WaitBusyTimeout(W25QXX_TIMEOUT_BLOCK_ERASE);
}

/* 整片擦除（需要较长时间，典型值 40~80 秒） */
W25QXXErr_t W25QXX_ChipErase(void)
{
    /* 检查写保护 */
    if (W25QXX_CheckProtected())
        return W25QXX_ERR_PROTECTED;

    W25QXXErr_t ret = W25QXX_WriteEnable();
    if (ret != W25QXX_OK)
        return ret;

    W25QXX_CS_LOW();
    HAL_StatusTypeDef st = W25QXX_SPI_RW(W25QXX_CHIP_ERASE, NULL);
    W25QXX_CS_HIGH();
    if (st != HAL_OK)
        return W25QXX_ERR_TIMEOUT;

    /* 整片擦除超时时间：90 秒 */
    return W25QXX_WaitBusyTimeout(W25QXX_TIMEOUT_CHIP_ERASE);
}

/* ========== 电源管理 ========== */

/* 进入掉电模式（降低功耗） */
W25QXXErr_t W25QXX_PowerDown(void)
{
    W25QXX_CS_LOW();
    HAL_StatusTypeDef st = W25QXX_SPI_RW(W25QXX_POWER_DOWN, NULL);
    W25QXX_CS_HIGH();
    return (st == HAL_OK) ? W25QXX_OK : W25QXX_ERR_TIMEOUT;
}

/* 退出掉电模式（唤醒芯片），唤醒后需要等待至少 3μs 才能发送命令 */
W25QXXErr_t W25QXX_WakeUp(void)
{
    W25QXX_CS_LOW();
    HAL_StatusTypeDef st = W25QXX_SPI_RW(W25QXX_RELEASE_POWER_DOWN, NULL);
    W25QXX_CS_HIGH();
    if (st != HAL_OK)
        return W25QXX_ERR_TIMEOUT;

    /* 等待芯片唤醒（典型 3μs，给一个安全延时 1ms） */
    HAL_Delay(1);
    return W25QXX_OK;
}

/* ========== 使用示例 ========== */
#if W25QXX_ENABLE_EXAMPLE

/* W25QXX 使用示例，展示完整的读写流程：
 * 初始化芯片 -> 获取芯片信息 -> 擦除扇区 -> 写入数据 -> 读取数据 -> 验证数据 */
void W25QXX_Example(void)
{
    /* 测试数据 */
    uint8_t write_buf[] = "Hello W25QXX!";
    uint8_t read_buf[sizeof(write_buf)];
    W25QXXInfo_t info;
    W25QXXErr_t ret;

    /* 1. 初始化 */
    ret = W25QXX_Init();
    if (ret != W25QXX_OK)
    {
        printf("W25QXX Init Failed! Error: %d\r\n", ret);
        return;
    }

    /* 2. 获取芯片信息 */
    W25QXX_GetInfo(&info);
    printf("========== W25QXX Info ==========\r\n");
    printf("JEDEC ID:     0x%06X\r\n", info.jedec_id);
    printf("Manufacturer: 0x%02X\r\n", info.manufacturer);
    printf("Memory Type:  0x%02X\r\n", info.memory_type);
    printf("Capacity ID:  0x%02X\r\n", info.capacity);
    printf("Capacity:     %u MB (%lu bytes)\r\n", info.size_mb, W25QXX_CHIP_SIZE);
    printf("Sector Size:  %u KB\r\n", W25QXX_SECTOR_SIZE / 1024);
    printf("Sector Count: %u\r\n", info.sector_count);
    printf("Block Size:   %u KB\r\n", W25QXX_BLOCK_SIZE / 1024);
    printf("Block Count:  %u\r\n", info.block_count);
    printf("Page Size:    %u B\r\n", W25QXX_PAGE_SIZE);
    printf("Page Count:   %u\r\n", info.page_count);
    printf("==================================\r\n\r\n");

    /* 3. 擦除扇区（使用第一个扇区） */
    printf("Erasing sector at address 0x%06X...\r\n", W25QXX_TEST_ADDR);
    ret = W25QXX_SectorErase(W25QXX_TEST_ADDR);
    if (ret != W25QXX_OK)
    {
        printf("Erase Failed! Error: %d\r\n", ret);
        return;
    }
    printf("Sector erased OK!\r\n\r\n");

    /* 4. 写入数据（使用跨页写入函数） */
    printf("Writing %zu bytes to address 0x%06X...\r\n", sizeof(write_buf), W25QXX_TEST_ADDR);
    ret = W25QXX_Write(W25QXX_TEST_ADDR, write_buf, sizeof(write_buf));
    if (ret != W25QXX_OK)
    {
        printf("Write Failed! Error: %d\r\n", ret);
        return;
    }
    printf("Write OK!\r\n\r\n");

    /* 5. 读取数据 */
    memset(read_buf, 0, sizeof(read_buf));
    printf("Reading %zu bytes from address 0x%06X...\r\n", sizeof(read_buf), W25QXX_TEST_ADDR);
    ret = W25QXX_Read(W25QXX_TEST_ADDR, read_buf, sizeof(read_buf));
    if (ret != W25QXX_OK)
    {
        printf("Read Failed! Error: %d\r\n", ret);
        return;
    }
    printf("Read OK!\r\n\r\n");

    /* 6. 验证数据 */
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

#endif /* W25QXX_ENABLE_EXAMPLE */
