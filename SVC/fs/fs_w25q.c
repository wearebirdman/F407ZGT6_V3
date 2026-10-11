#include "fs_w25q.h"
#include "w25qxx.h"

#include <string.h>

/* ========== 内部宏定义 ========== */
#define FS_W25Q_CRC32_POLY   0xEDB88320U /* IEEE 802.3 反射式多项式 */
#define FS_W25Q_CRC32_INIT   0xFFFFFFFFU /* 流式累积初值 */
#define FS_W25Q_CHUNK        256U        /* Verify 读回分块大小 */

/* CRC32 取终值 */
#define FS_W25Q_CRC32_FINAL(crc) ((uint32_t)(crc) ^ 0xFFFFFFFFU)

/* 按扇区向上对齐 */
#define FS_W25Q_SECTOR_ALIGN_UP(x) \
    (((uint32_t)(x) + W25QXX_SECTOR_SIZE - 1U) & ~(uint32_t)(W25QXX_SECTOR_SIZE - 1U))

/* 目录表镜像（532 字节）：头部 + 条目数组 + 表 CRC，字段顺序即存储顺序 */
typedef struct {
    uint32_t magic;                            /* 表标识 FS_W25Q_MAGIC */
    uint32_t version;                          /* 布局版本 */
    uint32_t seq;                              /* 提交序号：双表仲裁用，单调递增 */
    uint32_t entry_count;                      /* 有效条目数 */
    FsW25qEntry_t entries[FS_W25Q_MAX_FILES];  /* 条目数组 */
    uint32_t table_crc;                        /* 前部全部字节的 CRC32 终值 */
} FsW25qTable_t;

/* ========== 内部状态 ========== */
/* 以下缓冲均假定单任务（Uart_Proc）访问，未做重入保护 */
static FsW25qTable_t s_Table;                        /* 表 RAM 镜像 */
static uint8_t s_Mounted = 0;                        /* 1 = 已挂载 */
static uint8_t s_ActiveIdx = 0;                      /* 当前活动表所在扇区副本号（0/1） */
static uint8_t s_WriteActive = 0;                    /* 1 = 存在未 Close 的写句柄（全局唯一） */
static uint8_t s_ChunkBuf[FS_W25Q_CHUNK];            /* Verify 读回缓冲 */
static uint32_t s_RegionBegin[FS_W25Q_MAX_FILES + 1U]; /* 空洞扫描：占用区起点（升序） */
static uint32_t s_RegionEnd[FS_W25Q_MAX_FILES + 1U];   /* 空洞扫描：占用区终点（不含，扇区对齐） */

/* ========== 内部函数声明 ========== */
static uint32_t FsW25q_Crc32Update(uint32_t crc, const uint8_t *data, uint32_t len);
static uint32_t FsW25q_TableCrc(const FsW25qTable_t *table);
static uint8_t FsW25q_LoadCopy(uint32_t idx, uint32_t *seq);
static FsW25qErr_t FsW25q_WriteTable(uint32_t idx);
static FsW25qErr_t FsW25q_StoreTable(void);
static FsW25qErr_t FsW25q_CheckGeometry(void);
static int32_t FsW25q_FindIndex(const char *name);
static uint32_t FsW25q_CollectRegions(void);
static FsW25qErr_t FsW25q_AllocRegion(uint32_t size, uint32_t *offset);
static FsW25qErr_t FsW25q_CommitEntry(const char *name, uint32_t offset, uint32_t length, uint32_t crc32);

/* ========== CRC32 ========== */

/* CRC32（IEEE 反射式）流式更新
 * crc: 上次累积值（首次传 FS_W25Q_CRC32_INIT）
 * data/len: 本次数据
 * 返回: 更新后的累积值（结束套 FS_W25Q_CRC32_FINAL 得终值） */
static uint32_t FsW25q_Crc32Update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8U; bit++)
        {
            if (crc & 1U)
                crc = (crc >> 1) ^ FS_W25Q_CRC32_POLY;
            else
                crc >>= 1;
        }
    }
    return crc;
}

/* 计算表镜像的 CRC 终值（覆盖除 table_crc 自身以外的全部字节） */
static uint32_t FsW25q_TableCrc(const FsW25qTable_t *table)
{
    uint32_t crc = FsW25q_Crc32Update(FS_W25Q_CRC32_INIT, (const uint8_t *)table,
                                      (uint32_t)sizeof(FsW25qTable_t) - (uint32_t)sizeof(uint32_t));
    return FS_W25Q_CRC32_FINAL(crc);
}

/* ========== 表读写与几何校验 ========== */

/* 读取并校验指定扇区副本（idx = 0/1）到 RAM 镜像
 * 返回: 1 = 副本有效（magic/版本/计数/CRC 全部通过），*seq 输出提交序号；0 = 无效 */
static uint8_t FsW25q_LoadCopy(uint32_t idx, uint32_t *seq)
{
    if (W25QXX_Read(idx * W25QXX_SECTOR_SIZE, (uint8_t *)&s_Table,
                    (uint32_t)sizeof(s_Table)) != W25QXX_OK)
        return 0;

    if (s_Table.magic != FS_W25Q_MAGIC || s_Table.version != FS_W25Q_VERSION ||
        s_Table.entry_count > FS_W25Q_MAX_FILES ||
        s_Table.table_crc != FsW25q_TableCrc(&s_Table))
        return 0;

    *seq = s_Table.seq;
    return 1;
}

/* 将 RAM 镜像写入指定扇区副本：重算 CRC → 擦 → 编程 → 读回流式校验
 * 读回校验通过才允许调用方切换活动副本，防止半写表被误认有效 */
static FsW25qErr_t FsW25q_WriteTable(uint32_t idx)
{
    uint32_t addr = idx * W25QXX_SECTOR_SIZE;

    s_Table.table_crc = FsW25q_TableCrc(&s_Table);

    if (W25QXX_SectorErase(addr) != W25QXX_OK)
        return FS_W25Q_ERR_FLASH;
    if (W25QXX_Write(addr, (uint8_t *)&s_Table, (uint32_t)sizeof(s_Table)) != W25QXX_OK)
        return FS_W25Q_ERR_FLASH;

    uint32_t crc = FS_W25Q_CRC32_INIT;
    uint32_t remaining = (uint32_t)sizeof(s_Table) - (uint32_t)sizeof(uint32_t);
    uint32_t off = 0;
    while (remaining > 0)
    {
        uint32_t chunk = (remaining > FS_W25Q_CHUNK) ? FS_W25Q_CHUNK : remaining;
        if (W25QXX_Read(addr + off, s_ChunkBuf, chunk) != W25QXX_OK)
            return FS_W25Q_ERR_FLASH;
        crc = FsW25q_Crc32Update(crc, s_ChunkBuf, chunk);
        off += chunk;
        remaining -= chunk;
    }
    if (FS_W25Q_CRC32_FINAL(crc) != s_Table.table_crc)
        return FS_W25Q_ERR_FLASH;

    return FS_W25Q_OK;
}

/* 提交目录表：seq+1 写入非活动副本，成功后切换活动副本
 * 掉电安全：写新副本期间旧活动副本始终完整，最坏情况回滚到上一次提交 */
static FsW25qErr_t FsW25q_StoreTable(void)
{
    uint32_t next = 1U - s_ActiveIdx;

    s_Table.seq++;
    if (FsW25q_WriteTable(next) != FS_W25Q_OK)
        return FS_W25Q_ERR_FLASH;

    s_ActiveIdx = (uint8_t)next;
    return FS_W25Q_OK;
}

/* 校验表条目与当前芯片容量匹配（换芯片/表损坏防护，越界条目拒绝挂载） */
static FsW25qErr_t FsW25q_CheckGeometry(void)
{
    for (uint32_t i = 0; i < s_Table.entry_count; i++)
    {
        const FsW25qEntry_t *e = &s_Table.entries[i];

        if (e->length == 0 || e->offset < FS_W25Q_DATA_BASE ||
            (e->offset % W25QXX_SECTOR_SIZE) != 0 || e->offset >= W25QXX_CHIP_SIZE ||
            e->offset + FS_W25Q_SECTOR_ALIGN_UP(e->length) > W25QXX_CHIP_SIZE)
            return FS_W25Q_ERR_GEOMETRY;
    }
    return FS_W25Q_OK;
}

/* 按名字查条目索引，未找到返回 -1 */
static int32_t FsW25q_FindIndex(const char *name)
{
    for (uint32_t i = 0; i < s_Table.entry_count; i++)
    {
        if (strncmp(s_Table.entries[i].name, name, FS_W25Q_NAME_MAX) == 0)
            return (int32_t)i;
    }
    return -1;
}

/* 收集全部占用区间（含同名旧条目），按起点升序插入排序（≤16 条，代价可忽略）
 * 返回: 区间数；区间终点已按扇区对齐 */
static uint32_t FsW25q_CollectRegions(void)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < s_Table.entry_count; i++)
    {
        uint32_t j = count++;
        s_RegionBegin[j] = s_Table.entries[i].offset;
        s_RegionEnd[j] = FS_W25Q_SECTOR_ALIGN_UP(s_Table.entries[i].offset + s_Table.entries[i].length);

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
    return count;
}

/* first-fit 分配数据区并擦除覆盖扇区（不写表；同名旧条目区域照常计入占用，
 * 新数据落盘后由 CommitEntry 原子切换，旧区域自动成为可复用空洞）
 * 前置：已挂载（公开入口负责检查） */
static FsW25qErr_t FsW25q_AllocRegion(uint32_t size, uint32_t *offset)
{
    uint32_t need = FS_W25Q_SECTOR_ALIGN_UP(size);
    uint32_t count = FsW25q_CollectRegions();

    uint32_t cand = FS_W25Q_DATA_BASE;
    for (uint32_t i = 0; i < count; i++)
    {
        if (s_RegionBegin[i] >= cand + need)
            break; /* 当前空洞已足够，后面区间无需再看 */
        if (s_RegionEnd[i] > cand)
            cand = FS_W25Q_SECTOR_ALIGN_UP(s_RegionEnd[i]);
    }
    if (cand + need > W25QXX_CHIP_SIZE)
        return FS_W25Q_ERR_NO_SPACE;

    uint32_t sectors = need / W25QXX_SECTOR_SIZE;
    for (uint32_t s = 0; s < sectors; s++)
    {
        if (W25QXX_SectorErase(cand + s * W25QXX_SECTOR_SIZE) != W25QXX_OK)
            return FS_W25Q_ERR_FLASH;
    }

    *offset = cand;
    return FS_W25Q_OK;
}

/* 写入/更新条目并持久化表（数据须已落盘且与 crc32 一致）
 * 前置：已挂载、参数已由公开入口校验 */
static FsW25qErr_t FsW25q_CommitEntry(const char *name, uint32_t offset, uint32_t length, uint32_t crc32)
{
    int32_t idx = FsW25q_FindIndex(name);
    if (idx < 0)
    {
        if (s_Table.entry_count >= FS_W25Q_MAX_FILES)
            return FS_W25Q_ERR_FULL;
        idx = (int32_t)s_Table.entry_count;
        s_Table.entry_count++;
    }

    FsW25qEntry_t *entry = &s_Table.entries[idx];
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->name, name, strlen(name) + 1U); /* 入口已限 strlen ≤ 15，含终止符恰填 16 字节 */
    entry->offset = offset;
    entry->length = length;
    entry->crc32 = crc32;

    return FsW25q_StoreTable();
}

/* ========== 挂载 ========== */

/* 挂载（幂等）：验片+唤醒 → 双副本仲裁加载表（均无效则格式化重建）→ 几何校验 */
FsW25qErr_t FsW25q_Mount(void)
{
    if (s_Mounted)
        return FS_W25Q_OK;

    if (W25QXX_Init() != W25QXX_OK)
        return FS_W25Q_ERR_FLASH;

    uint32_t seq_a = 0;
    uint32_t seq_b = 0;
    uint8_t valid_a = FsW25q_LoadCopy(0, &seq_a);
    uint8_t valid_b = FsW25q_LoadCopy(1, &seq_b); /* 成功时 s_Table 镜像此刻为副本 B */

    if (!valid_a && !valid_b)
    {
        /* 首次使用/旧版本格式/双副本皆损：重建空表并写满两份 */
        memset(&s_Table, 0, sizeof(s_Table));
        s_Table.magic = FS_W25Q_MAGIC;
        s_Table.version = FS_W25Q_VERSION;
        s_Table.seq = 1U;
        if (FsW25q_WriteTable(0) != FS_W25Q_OK || FsW25q_WriteTable(1) != FS_W25Q_OK)
            return FS_W25Q_ERR_FLASH;
        s_ActiveIdx = 0;
    }
    else if (valid_b && (!valid_a || seq_b >= seq_a))
    {
        s_ActiveIdx = 1;
    }
    else
    {
        FsW25q_LoadCopy(0, &seq_a); /* 副本 A 胜出：装回镜像（副本 B 读取失败也已覆盖镜像） */
        s_ActiveIdx = 0;
    }

    FsW25qErr_t ret = FsW25q_CheckGeometry();
    if (ret != FS_W25Q_OK)
        return ret;

    s_Mounted = 1;
    return FS_W25Q_OK;
}

/* ========== 文件操作 ========== */

/* 打开文件（f_open）
 * 读模式：按已有文件名打开，游标归零
 * 写模式：创建或覆盖，size 为文件总大小（NOR 先擦后写，打开即分配+擦除；
 *         Sync 前旧同名条目保持完整）；size=0 或超过芯片容量非法
 * fil: 调用方分配的文件对象；读写互斥，单句柄单模式 */
FsW25qErr_t FsW25q_Open(FsW25qFil_t *fil, const char *name, uint16_t mode, uint32_t size)
{
    if (!s_Mounted)
        return FS_W25Q_ERR_NOT_READY;
    if (fil == NULL || name == NULL || strlen(name) >= FS_W25Q_NAME_MAX)
        return FS_W25Q_ERR_PARAM;
    if (mode != FS_W25Q_FA_READ && mode != FS_W25Q_FA_WRITE)
        return FS_W25Q_ERR_PARAM;

    memset(fil, 0, sizeof(*fil));
    strncpy(fil->name, name, FS_W25Q_NAME_MAX - 1U);

    if (mode == FS_W25Q_FA_WRITE)
    {
        /* size 上界防对齐宏溢出（>芯片容量即非法） */
        if (size == 0 || size > W25QXX_CHIP_SIZE)
            return FS_W25Q_ERR_PARAM;
        if (s_WriteActive)
            return FS_W25Q_ERR_BUSY;
        /* 新文件预检表位：避免白付擦除成本后 Close 才报 FULL（同名覆盖不新增条目） */
        if (FsW25q_FindIndex(name) < 0 && s_Table.entry_count >= FS_W25Q_MAX_FILES)
            return FS_W25Q_ERR_FULL;

        FsW25qErr_t ret = FsW25q_AllocRegion(size, &fil->offset);
        if (ret != FS_W25Q_OK)
            return ret;

        s_WriteActive = 1;
        fil->mode = mode;
        fil->size = size;
        fil->crc = FS_W25Q_CRC32_INIT;
        return FS_W25Q_OK;
    }

    int32_t idx = FsW25q_FindIndex(name);
    if (idx < 0)
        return FS_W25Q_ERR_NOT_FOUND;

    fil->mode = mode;
    fil->offset = s_Table.entries[idx].offset;
    fil->size = s_Table.entries[idx].length;
    return FS_W25Q_OK;
}

/* 顺序追加写入（f_write）：超过 Open 声明容量返回 FS_W25Q_ERR_NO_SPACE */
FsW25qErr_t FsW25q_Write(FsW25qFil_t *fil, const uint8_t *buf, uint32_t btw, uint32_t *bw)
{
    if (!s_Mounted)
        return FS_W25Q_ERR_NOT_READY;
    if (fil == NULL || buf == NULL || bw == NULL)
        return FS_W25Q_ERR_PARAM;
    if (fil->mode != FS_W25Q_FA_WRITE)
        return FS_W25Q_ERR_PARAM;
    if (btw > fil->size - fil->pos) /* 减法比较避免 pos+btw 回绕绕过容量检查 */
        return FS_W25Q_ERR_NO_SPACE;

    if (btw == 0)
    {
        *bw = 0;
        return FS_W25Q_OK;
    }

    if (W25QXX_Write(fil->offset + fil->pos, buf, btw) != W25QXX_OK)
        return FS_W25Q_ERR_FLASH;

    fil->crc = FsW25q_Crc32Update(fil->crc, buf, btw);
    fil->pos += btw;
    fil->dirty = 1;
    *bw = btw;
    return FS_W25Q_OK;
}

/* 读取（f_read）：到文件尾短读，*br 为实际读出字节数（0 = EOF） */
FsW25qErr_t FsW25q_Read(FsW25qFil_t *fil, uint8_t *buf, uint32_t btr, uint32_t *br)
{
    if (!s_Mounted)
        return FS_W25Q_ERR_NOT_READY;
    if (fil == NULL || buf == NULL || br == NULL)
        return FS_W25Q_ERR_PARAM;
    if (fil->mode != FS_W25Q_FA_READ)
        return FS_W25Q_ERR_PARAM;

    uint32_t avail = fil->size - fil->pos;
    uint32_t n = (btr > avail) ? avail : btr;
    if (n == 0)
    {
        *br = 0;
        return FS_W25Q_OK;
    }

    if (W25QXX_Read(fil->offset + fil->pos, buf, n) != W25QXX_OK)
        return FS_W25Q_ERR_FLASH;

    fil->pos += n;
    *br = n;
    return FS_W25Q_OK;
}

/* 移动读游标（f_lseek）：仅读模式允许（写模式顺序追加，随机写会破坏 CRC 流式累积）
 * pos 可到 size（指向 EOF） */
FsW25qErr_t FsW25q_Lseek(FsW25qFil_t *fil, uint32_t pos)
{
    if (!s_Mounted)
        return FS_W25Q_ERR_NOT_READY;
    if (fil == NULL)
        return FS_W25Q_ERR_PARAM;
    if (fil->mode != FS_W25Q_FA_READ || pos > fil->size)
        return FS_W25Q_ERR_PARAM;

    fil->pos = pos;
    return FS_W25Q_OK;
}

/* 中途落表（f_sync）：把已写数据登记/更新进目录表并持久化，大文件分段保护；
 * 无脏数据直接返回 OK；未写入任何数据（pos=0）不允许落表 */
FsW25qErr_t FsW25q_Sync(FsW25qFil_t *fil)
{
    if (!s_Mounted)
        return FS_W25Q_ERR_NOT_READY;
    if (fil == NULL)
        return FS_W25Q_ERR_PARAM;
    if (fil->mode != FS_W25Q_FA_WRITE)
        return FS_W25Q_ERR_PARAM;
    if (!fil->dirty)
        return FS_W25Q_OK;
    if (fil->pos == 0)
        return FS_W25Q_ERR_PARAM;

    FsW25qErr_t ret = FsW25q_CommitEntry(fil->name, fil->offset, fil->pos,
                                         FS_W25Q_CRC32_FINAL(fil->crc));
    if (ret == FS_W25Q_OK)
        fil->dirty = 0;
    return ret;
}

/* 关闭文件（f_close）：写模式有脏数据先自动 Sync；释放写句柄标志；句柄复位为无效 */
FsW25qErr_t FsW25q_Close(FsW25qFil_t *fil)
{
    if (fil == NULL)
        return FS_W25Q_ERR_PARAM;

    FsW25qErr_t ret = FS_W25Q_OK;
    if (fil->mode == FS_W25Q_FA_WRITE)
    {
        if (fil->dirty)
            ret = FsW25q_Sync(fil);
        s_WriteActive = 0;
    }

    memset(fil, 0, sizeof(*fil));
    return ret;
}

/* ========== 目录与维护 ========== */

/* 查文件信息（f_stat） */
FsW25qErr_t FsW25q_Stat(const char *name, FsW25qFilInfo_t *info)
{
    if (!s_Mounted)
        return FS_W25Q_ERR_NOT_READY;
    if (name == NULL || info == NULL)
        return FS_W25Q_ERR_PARAM;

    int32_t idx = FsW25q_FindIndex(name);
    if (idx < 0)
        return FS_W25Q_ERR_NOT_FOUND;

    const FsW25qEntry_t *e = &s_Table.entries[idx];
    strncpy(info->fname, e->name, FS_W25Q_NAME_MAX - 1U);
    info->fname[FS_W25Q_NAME_MAX - 1U] = '\0';
    info->fsize = e->length;
    info->crc32 = e->crc32;
    info->offset = e->offset;
    return FS_W25Q_OK;
}

/* 删除条目（f_unlink）：仅从表移除并持久化，数据区不擦除（first-fit 自动复用空洞） */
FsW25qErr_t FsW25q_Remove(const char *name)
{
    if (!s_Mounted)
        return FS_W25Q_ERR_NOT_READY;
    if (name == NULL)
        return FS_W25Q_ERR_PARAM;

    int32_t idx = FsW25q_FindIndex(name);
    if (idx < 0)
        return FS_W25Q_ERR_NOT_FOUND;

    for (uint32_t i = (uint32_t)idx; i + 1U < s_Table.entry_count; i++)
        s_Table.entries[i] = s_Table.entries[i + 1U];
    memset(&s_Table.entries[s_Table.entry_count - 1U], 0, sizeof(FsW25qEntry_t));
    s_Table.entry_count--;

    return FsW25q_StoreTable();
}

/* 打开目录遍历（f_opendir） */
FsW25qErr_t FsW25q_Opendir(FsW25qDir_t *dir)
{
    if (!s_Mounted)
        return FS_W25Q_ERR_NOT_READY;
    if (dir == NULL)
        return FS_W25Q_ERR_PARAM;

    dir->idx = 0;
    return FS_W25Q_OK;
}

/* 读下一个目录项（f_readdir）：遍历结束 info->fname[0] = '\0' 且返回 OK（FatFS 约定） */
FsW25qErr_t FsW25q_Readdir(FsW25qDir_t *dir, FsW25qFilInfo_t *info)
{
    if (!s_Mounted)
        return FS_W25Q_ERR_NOT_READY;
    if (dir == NULL || info == NULL)
        return FS_W25Q_ERR_PARAM;

    memset(info, 0, sizeof(*info));
    if (dir->idx >= s_Table.entry_count)
        return FS_W25Q_OK;

    const FsW25qEntry_t *e = &s_Table.entries[dir->idx++];
    strncpy(info->fname, e->name, FS_W25Q_NAME_MAX - 1U);
    info->fname[FS_W25Q_NAME_MAX - 1U] = '\0';
    info->fsize = e->length;
    info->crc32 = e->crc32;
    info->offset = e->offset;
    return FS_W25Q_OK;
}

/* 数据区空洞总字节数（f_getfree 简化版；未挂载返回 0）
 * 注意：活动写句柄已分配未提交的区域不计入占用，会被算进空闲总量 */
uint32_t FsW25q_GetFree(void)
{
    if (!s_Mounted)
        return 0U;

    uint32_t count = FsW25q_CollectRegions();
    uint32_t free_bytes = 0;
    uint32_t cand = FS_W25Q_DATA_BASE;

    for (uint32_t i = 0; i < count; i++)
    {
        if (s_RegionBegin[i] > cand)
            free_bytes += s_RegionBegin[i] - cand;
        if (s_RegionEnd[i] > cand)
            cand = s_RegionEnd[i];
    }
    if (W25QXX_CHIP_SIZE > cand)
        free_bytes += W25QXX_CHIP_SIZE - cand;

    return free_bytes;
}

/* 读回文件数据算 CRC32 与表比对（增值项，FatFS 无对应内容级校验）
 * name: 文件名; calc_crc: 输出计算终值（校验通过与否均写入）
 * 返回: FS_W25Q_OK 一致 / FS_W25Q_ERR_MISMATCH 不一致 / 其他为环节错误 */
FsW25qErr_t FsW25q_Verify(const char *name, uint32_t *calc_crc)
{
    if (!s_Mounted)
        return FS_W25Q_ERR_NOT_READY;
    if (name == NULL || calc_crc == NULL)
        return FS_W25Q_ERR_PARAM;

    int32_t idx = FsW25q_FindIndex(name);
    if (idx < 0)
        return FS_W25Q_ERR_NOT_FOUND;

    const FsW25qEntry_t *e = &s_Table.entries[idx];

    uint32_t crc = FS_W25Q_CRC32_INIT;
    uint32_t remaining = e->length;
    uint32_t addr = e->offset;
    while (remaining > 0)
    {
        uint32_t chunk = (remaining > FS_W25Q_CHUNK) ? FS_W25Q_CHUNK : remaining;
        if (W25QXX_Read(addr, s_ChunkBuf, chunk) != W25QXX_OK)
            return FS_W25Q_ERR_FLASH;
        crc = FsW25q_Crc32Update(crc, s_ChunkBuf, chunk);
        addr += chunk;
        remaining -= chunk;
    }

    *calc_crc = FS_W25Q_CRC32_FINAL(crc);
    return (*calc_crc == e->crc32) ? FS_W25Q_OK : FS_W25Q_ERR_MISMATCH;
}
