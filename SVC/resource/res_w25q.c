#include "res_w25q.h"
#include "usart.h"
#include "w25qxx.h"
#include "ymodem.h"

#include <string.h>

/* ========== 内部宏定义 ========== */
#define RES_W25Q_CRC32_POLY    0xEDB88320U /* IEEE 802.3 反射式多项式 */
#define RES_W25Q_CRC32_INIT    0xFFFFFFFFU /* 流式累积初值 */
#define RES_W25Q_VERIFY_CHUNK  256U        /* Verify 读回分块大小 */
#define RES_W25Q_ERASE_REPORT  16U         /* 擦除进度打印间隔（扇区数） */

/* CRC32 取终值 */
#define RES_W25Q_CRC32_FINAL(crc)  ((uint32_t)(crc) ^ 0xFFFFFFFFU)

/* 按扇区向上对齐 */
#define RES_W25Q_SECTOR_ALIGN_UP(x) \
    (((uint32_t)(x) + W25QXX_SECTOR_SIZE - 1U) & ~(uint32_t)(W25QXX_SECTOR_SIZE - 1U))

/* 分区表镜像（532 字节）：头部 + 条目数组 + 表 CRC，字段顺序即存储顺序 */
typedef struct {
    uint32_t magic;                            /* 表标识 RES_W25Q_MAGIC */
    uint32_t version;                          /* 布局版本 */
    uint32_t entry_count;                      /* 有效条目数 */
    uint32_t reserved;                         /* 对齐填充 */
    ResW25qEntry_t entries[RES_W25Q_MAX_FILES]; /* 条目数组 */
    uint32_t table_crc;                        /* 前部全部字节的 CRC32 终值 */
} ResW25qTable_t;

/* ========== 内部状态 ========== */
/* 以下缓冲均假定单任务（Uart_Proc）访问，未做重入保护 */
static ResW25qTable_t s_Table;                                    /* 表 RAM 镜像 */
static uint8_t s_Ready = 0;                                       /* 1 = 初始化已完成 */
static uint8_t s_VerifyBuf[RES_W25Q_VERIFY_CHUNK];                /* Verify 读回缓冲 */
static uint32_t s_RegionBegin[RES_W25Q_MAX_FILES + 1U];           /* Alloc 空洞扫描：占用区起点 */
static uint32_t s_RegionEnd[RES_W25Q_MAX_FILES + 1U];             /* Alloc 空洞扫描：占用区终点（不含） */

/* ========== 内部函数声明 ========== */
static uint32_t ResW25q_Crc32Update(uint32_t crc, const uint8_t *data, uint32_t len);
static uint32_t ResW25q_TableCrc(const ResW25qTable_t *table);
static ResW25qErr_t ResW25q_LoadTable(void);
static ResW25qErr_t ResW25q_StoreTable(void);
static int32_t ResW25q_FindIndex(const char *name);

/* ========== CRC32 ========== */

/* CRC32（IEEE 反射式）流式更新
 * crc: 上次累积值（首次传 RES_W25Q_CRC32_INIT）
 * data/len: 本次数据
 * 返回: 更新后的累积值（结束套 RES_W25Q_CRC32_FINAL 得终值） */
static uint32_t ResW25q_Crc32Update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8U; bit++)
        {
            if (crc & 1U)
                crc = (crc >> 1) ^ RES_W25Q_CRC32_POLY;
            else
                crc >>= 1;
        }
    }
    return crc;
}

/* 计算表镜像的 CRC 终值（覆盖除 table_crc 自身以外的全部字节） */
static uint32_t ResW25q_TableCrc(const ResW25qTable_t *table)
{
    uint32_t crc = ResW25q_Crc32Update(RES_W25Q_CRC32_INIT, (const uint8_t *)table,
                                       (uint32_t)sizeof(ResW25qTable_t) - (uint32_t)sizeof(uint32_t));
    return RES_W25Q_CRC32_FINAL(crc);
}

/* ========== 表读写 ========== */

/* 从扇区 0 加载表并校验
 * 返回: RES_W25Q_OK 有效；RES_W25Q_ERR_PARAM 表无效（magic/版本/CRC/计数异常）；
 *       RES_W25Q_ERR_FLASH 读失败 */
static ResW25qErr_t ResW25q_LoadTable(void)
{
    if (W25QXX_Read(0, (uint8_t *)&s_Table, (uint32_t)sizeof(s_Table)) != W25QXX_OK)
        return RES_W25Q_ERR_FLASH;

    if (s_Table.magic != RES_W25Q_MAGIC || s_Table.version != RES_W25Q_VERSION ||
        s_Table.entry_count > RES_W25Q_MAX_FILES ||
        s_Table.table_crc != ResW25q_TableCrc(&s_Table))
        return RES_W25Q_ERR_PARAM;

    return RES_W25Q_OK;
}

/* 重算表 CRC 后擦写扇区 0 持久化 */
static ResW25qErr_t ResW25q_StoreTable(void)
{
    s_Table.table_crc = ResW25q_TableCrc(&s_Table);

    if (W25QXX_SectorErase(0) != W25QXX_OK)
        return RES_W25Q_ERR_FLASH;
    if (W25QXX_Write(0, (uint8_t *)&s_Table, (uint32_t)sizeof(s_Table)) != W25QXX_OK)
        return RES_W25Q_ERR_FLASH;

    return RES_W25Q_OK;
}

/* 按名字查条目索引，未找到返回 -1 */
static int32_t ResW25q_FindIndex(const char *name)
{
    for (uint32_t i = 0; i < s_Table.entry_count; i++)
    {
        if (strncmp(s_Table.entries[i].name, name, RES_W25Q_NAME_MAX) == 0)
            return (int32_t)i;
    }
    return -1;
}

/* ========== 初始化与查询 ========== */

/* 初始化（幂等）：加载分区表；表无效（首次使用/损坏）则格式化重建空表 */
ResW25qErr_t ResW25q_Init(void)
{
    if (s_Ready)
        return RES_W25Q_OK;

    if (W25QXX_Init() != W25QXX_OK)
        return RES_W25Q_ERR_FLASH;

    ResW25qErr_t ret = ResW25q_LoadTable();
    if (ret == RES_W25Q_ERR_FLASH)
        return ret;

    if (ret != RES_W25Q_OK)
    {
        Uart_Printf("res_w25q: table invalid, formatting...\r\n");
        memset(&s_Table, 0, sizeof(s_Table));
        s_Table.magic = RES_W25Q_MAGIC;
        s_Table.version = RES_W25Q_VERSION;
        s_Table.entry_count = 0;

        ret = ResW25q_StoreTable();
        if (ret != RES_W25Q_OK)
            return ret;
        Uart_Printf("res_w25q: empty table created\r\n");
    }

    s_Ready = 1;
    return RES_W25Q_OK;
}

/* 按名字查条目信息（out: 输出条目副本） */
ResW25qErr_t ResW25q_Find(const char *name, ResW25qEntry_t *out)
{
    if (name == NULL || out == NULL)
        return RES_W25Q_ERR_PARAM;

    ResW25qErr_t ret = ResW25q_Init();
    if (ret != RES_W25Q_OK)
        return ret;

    int32_t idx = ResW25q_FindIndex(name);
    if (idx < 0)
        return RES_W25Q_ERR_NOT_FOUND;

    *out = s_Table.entries[idx];
    return RES_W25Q_OK;
}

/* 打印分区目录 */
void ResW25q_List(void)
{
    ResW25qErr_t ret = ResW25q_Init();
    if (ret != RES_W25Q_OK)
    {
        Uart_Printf("res_w25q: init failed (%d)\r\n", ret);
        return;
    }

    Uart_Printf("== resource partition: %lu entries ==\r\n", s_Table.entry_count);
    for (uint32_t i = 0; i < s_Table.entry_count; i++)
    {
        Uart_Printf("[%lu] %-15s off=0x%06lX len=%lu crc=0x%08lX\r\n",
                    i, s_Table.entries[i].name, s_Table.entries[i].offset,
                    s_Table.entries[i].length, s_Table.entries[i].crc32);
    }
}

/* ========== 空间分配与提交 ========== */

/* 为资源分配数据区并擦除覆盖扇区（不写表，数据落盘后由 ResW25q_Commit 登记）
 * name: 资源名（同名条目视为待更新：分配新区域，旧区域在 Commit 后自动成为可复用空洞）
 * size: 字节长度
 * offset: 输出分配到的起始地址
 * 返回: RES_W25Q_OK / RES_W25Q_ERR_NO_SPACE / RES_W25Q_ERR_FLASH 等 */
ResW25qErr_t ResW25q_Alloc(const char *name, uint32_t size, uint32_t *offset)
{
    if (name == NULL || offset == NULL || size == 0 || strlen(name) >= RES_W25Q_NAME_MAX)
        return RES_W25Q_ERR_PARAM;

    ResW25qErr_t ret = ResW25q_Init();
    if (ret != RES_W25Q_OK)
        return ret;

    uint32_t need = RES_W25Q_SECTOR_ALIGN_UP(size);

    /* 收集全部占用区间（含同名旧条目），按起点升序插入排序（≤16 条，代价可忽略） */
    uint32_t count = 0;
    for (uint32_t i = 0; i < s_Table.entry_count; i++)
    {
        uint32_t j = count++;
        s_RegionBegin[j] = s_Table.entries[i].offset;
        s_RegionEnd[j] = RES_W25Q_SECTOR_ALIGN_UP(s_Table.entries[i].offset + s_Table.entries[i].length);

        while (j > 0 && s_RegionBegin[j - 1U] > s_RegionBegin[j])
        {
            uint32_t tb = s_RegionBegin[j - 1U];
            uint32_t te = s_RegionEnd[j - 1U];
            s_RegionBegin[j - 1U] = s_RegionBegin[j];
            s_RegionEnd[j - 1U] = s_RegionEnd[j];
            s_RegionBegin[j] = tb;
            s_RegionEnd[j] = te;
            j--;
        }
    }

    /* first-fit：从数据区起点找第一个可容纳 need 的空洞 */
    uint32_t cand = RES_W25Q_DATA_BASE;
    for (uint32_t i = 0; i < count; i++)
    {
        if (s_RegionBegin[i] >= cand + need)
            break; /* 当前空洞已足够，后面区间无需再看 */
        if (s_RegionEnd[i] > cand)
            cand = RES_W25Q_SECTOR_ALIGN_UP(s_RegionEnd[i]);
    }
    if (cand + need > W25QXX_CHIP_SIZE)
        return RES_W25Q_ERR_NO_SPACE;

    /* 擦除覆盖扇区，按间隔报进度 */
    uint32_t sectors = need / W25QXX_SECTOR_SIZE;
    for (uint32_t s = 0; s < sectors; s++)
    {
        if (W25QXX_SectorErase(cand + s * W25QXX_SECTOR_SIZE) != W25QXX_OK)
            return RES_W25Q_ERR_FLASH;
        if ((s % RES_W25Q_ERASE_REPORT) == (RES_W25Q_ERASE_REPORT - 1U))
            Uart_Printf("erase %lu/%lu sectors...\r\n", s + 1U, sectors);
    }

    *offset = cand;
    return RES_W25Q_OK;
}

/* 登记资源：写入/更新条目并持久化表（数据须已落盘且与 crc32 一致）
 * name/offset/length/crc32: 资源名、ResW25q_Alloc 返回的地址、实际字节数、数据 CRC32 终值 */
ResW25qErr_t ResW25q_Commit(const char *name, uint32_t offset, uint32_t length, uint32_t crc32)
{
    if (name == NULL || length == 0 || strlen(name) >= RES_W25Q_NAME_MAX)
        return RES_W25Q_ERR_PARAM;
    if (offset < RES_W25Q_DATA_BASE || (offset % W25QXX_SECTOR_SIZE) != 0 ||
        offset + RES_W25Q_SECTOR_ALIGN_UP(length) > W25QXX_CHIP_SIZE)
        return RES_W25Q_ERR_PARAM;

    ResW25qErr_t ret = ResW25q_Init();
    if (ret != RES_W25Q_OK)
        return ret;

    int32_t idx = ResW25q_FindIndex(name);
    if (idx < 0)
    {
        if (s_Table.entry_count >= RES_W25Q_MAX_FILES)
            return RES_W25Q_ERR_FULL;
        idx = (int32_t)s_Table.entry_count;
        s_Table.entry_count++;
    }

    ResW25qEntry_t *entry = &s_Table.entries[idx];
    memset(entry, 0, sizeof(*entry));
    strncpy(entry->name, name, RES_W25Q_NAME_MAX - 1U);
    entry->offset = offset;
    entry->length = length;
    entry->crc32 = crc32;

    return ResW25q_StoreTable();
}

/* ========== 读回校验 ========== */

/* 读回资源数据计算 CRC32 与表比对，结果经 Uart_Printf 打印 */
ResW25qErr_t ResW25q_Verify(const char *name)
{
    ResW25qEntry_t entry;
    ResW25qErr_t ret = ResW25q_Find(name, &entry);
    if (ret != RES_W25Q_OK)
        return ret;

    uint32_t crc = RES_W25Q_CRC32_INIT;
    uint32_t remaining = entry.length;
    uint32_t addr = entry.offset;
    while (remaining > 0)
    {
        uint32_t chunk = (remaining > RES_W25Q_VERIFY_CHUNK) ? RES_W25Q_VERIFY_CHUNK : remaining;
        if (W25QXX_Read(addr, s_VerifyBuf, chunk) != W25QXX_OK)
            return RES_W25Q_ERR_FLASH;
        crc = ResW25q_Crc32Update(crc, s_VerifyBuf, chunk);
        addr += chunk;
        remaining -= chunk;
    }

    uint32_t calc = RES_W25Q_CRC32_FINAL(crc);
    if (calc != entry.crc32)
    {
        Uart_Printf("res_w25q: %s CRC mismatch (calc 0x%08lX, table 0x%08lX)\r\n", entry.name, calc, entry.crc32);
        return RES_W25Q_ERR_MISMATCH;
    }

    Uart_Printf("res_w25q: %s verify OK (%lu bytes)\r\n", entry.name, entry.length);
    return RES_W25Q_OK;
}

/* ========== 图案自测 ========== */

/* 按绝对地址图案生成数据写入并登记（绕过 Ymodem 通道的分区闭环自测）
 * name: 资源名; size: 字节长度
 * 返回: RES_W25Q_OK / 对应环节错误码；失败信息经 Uart_Printf 打印 */
ResW25qErr_t ResW25q_GenPattern(const char *name, uint32_t size)
{
    static uint8_t s_GenBuf[256]; /* 图案块缓冲（静态，省任务栈） */

    uint32_t offset;
    ResW25qErr_t ret = ResW25q_Alloc(name, size, &offset);
    if (ret != RES_W25Q_OK)
    {
        Uart_Printf("gen: alloc failed (%d)\r\n", ret);
        return ret;
    }

    uint32_t crc = RES_W25Q_CRC32_INIT;
    uint32_t written = 0;
    while (written < size)
    {
        uint32_t chunk = (size - written > sizeof(s_GenBuf)) ? sizeof(s_GenBuf) : (size - written);
        for (uint32_t i = 0; i < chunk; i++)
            s_GenBuf[i] = (uint8_t)((offset + written + i) & 0xFF);

        if (W25QXX_Write(offset + written, s_GenBuf, chunk) != W25QXX_OK)
        {
            Uart_Printf("gen: write failed @0x%06lX\r\n", offset + written);
            return RES_W25Q_ERR_FLASH;
        }
        crc = ResW25q_Crc32Update(crc, s_GenBuf, chunk);
        written += chunk;
    }

    ret = ResW25q_Commit(name, offset, size, RES_W25Q_CRC32_FINAL(crc));
    if (ret != RES_W25Q_OK)
        Uart_Printf("gen: commit failed (%d)\r\n", ret);
    else
        Uart_Printf("gen: %s committed (off=0x%06lX len=%lu)\r\n", name, offset, size);
    return ret;
}

/* ========== Ymodem 入库服务 ========== */

/* Ymodem 入库上下文：由协议层透传，持有分区分配与 CRC 累积状态 */
typedef struct {
    char name[YMODEM_NAME_MAX];
    uint32_t offset;
    uint32_t crc;
} ResW25qYmCtx_t;

/* 头块回调：按文件名与大小分配分区并擦除覆盖扇区，初始化 CRC32 累积 */
static int ResW25q_YmOnHeader(const char *name, uint32_t size, void *ctx)
{
    ResW25qYmCtx_t *c = (ResW25qYmCtx_t *)ctx;

    strncpy(c->name, name, YMODEM_NAME_MAX - 1U);
    c->name[YMODEM_NAME_MAX - 1U] = '\0';
    if (ResW25q_Alloc(c->name, size, &c->offset) != RES_W25Q_OK)
        return -1;
    c->crc = RES_W25Q_CRC32_INIT;
    return 0;
}

/* 数据包回调：逐包写入 W25Qxx 并累积 CRC32（offset 为已接收累计偏移） */
static int ResW25q_YmOnData(const uint8_t *data, uint32_t len, uint32_t offset, void *ctx)
{
    ResW25qYmCtx_t *c = (ResW25qYmCtx_t *)ctx;

    if (W25QXX_Write(c->offset + offset, data, len) != W25QXX_OK)
        return -1;
    c->crc = ResW25q_Crc32Update(c->crc, data, len);
    return 0;
}

/* 结束回调：将文件信息登记到分区表 */
static int ResW25q_YmOnEnd(uint32_t total_size, void *ctx)
{
    ResW25qYmCtx_t *c = (ResW25qYmCtx_t *)ctx;

    return (ResW25q_Commit(c->name, c->offset, total_size,
                           RES_W25Q_CRC32_FINAL(c->crc)) == RES_W25Q_OK) ? 0 : -1;
}

/* Ymodem 接收入库：接管串口→协议接收→回调落盘→恢复帧接收→打印结果摘要
 * 前置：Uart_Proc 单任务上下文调用；传输期间静默所有任务输出，防污染协议流
 * 返回: YmodemErr_t 协议层接收结果 */
YmodemErr_t ResW25q_YmodemStore(void)
{
    static ResW25qYmCtx_t s_YmCtx; /* 静态省任务栈；单任务调用无重入风险 */
    const YmodemSink_t sink = {
        ResW25q_YmOnHeader,
        ResW25q_YmOnData,
        ResW25q_YmOnEnd,
        &s_YmCtx,
    };

    Uart_SetQuiet(1);
    Uart_RxStop();
    YmodemErr_t ret = Ymodem_Receive(&sink);
    Uart_RxRestart();
    Uart_SetQuiet(0);

    switch (ret)
    {
        case YMODEM_OK:
            Uart_Printf("ymodem: done OK, use 'part' to list\r\n");
            break;
        case YMODEM_ERR_TIMEOUT:
            Uart_Printf("ymodem: timeout (no sender?)\r\n");
            break;
        case YMODEM_ERR_ABORTED:
            Uart_Printf("ymodem: aborted by sender\r\n");
            break;
        case YMODEM_ERR_PROTOCOL:
            Uart_Printf("ymodem: protocol error\r\n");
            break;
        case YMODEM_ERR_PARAM:
            Uart_Printf("ymodem: bad header (name/size invalid)\r\n");
            break;
        case YMODEM_ERR_REJECT:
            Uart_Printf("ymodem: storage error (alloc/write/commit failed)\r\n");
            break;
        default:
            break;
    }
    return ret;
}
