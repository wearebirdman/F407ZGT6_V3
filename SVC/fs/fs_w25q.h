#ifndef __FS_W25Q_H
#define __FS_W25Q_H

#include "main.h"

/* ========== 模块定位 ==========
 * W25Qxx 文件系统层（NOR Flash 后端）：函数命名与用法 1:1 映射 FatFS
 * （f_mount/f_open/f_read/f_write/f_sync/f_stat/f_unlink/f_opendir/f_readdir/f_getfree），
 * 后续在 SD 卡等平台落地 FatFS 时上层代码形态零改动。
 * 目录表（扇区 0/1 双表乒乓：magic/version/seq/条目数组/表 CRC）+ 数据区（4KB 扇区对齐，first-fit 空洞复用）。
 * 双表原子提交：每次落表写入非活动副本（seq+1），读回 CRC 校验通过才切换活动副本；
 * 提交窗口内掉电必保留上一份完整表，挂载取两副本中 seq 大的有效者，杜绝"掉电丢整个目录"。
 * 本层协议无关、零打印：数据来源（Ymodem/SD/内部搬运）与结果显示均归上层组合/命令层。
 * 容量与几何参数全部取自 BSP 编译期宏（W25QXX_CHIP_SIZE/SECTOR_SIZE），
 * 切换 W25Q32/W25Q128 本层零改动；挂载时校验表条目不超出当前芯片容量。
 *
 * 使用约定（常挂载模型）：
 *   上电调用 FsW25q_Mount 一次（内部含 W25Qxx_Init 验片+唤醒），此后全程不卸载；
 *   未挂载时调用任何接口返回 FS_W25Q_ERR_NOT_READY，不做隐式懒挂载。
 * 写模式语义（NOR 特性，与 FatFS 的唯一差异）：
 *   Open(WRITE) 必须声明文件总大小，打开即完成空洞分配+整块擦除（NOR 先擦后写）；
 *   Write 仅支持顺序追加（禁止中途 Lseek 随机写，保证 CRC 流式累积有效）；
 *   Sync/Close 才把条目原子切换进目录表——Sync 前旧同名文件保持完整，掉电不损目录。
 *   写句柄全局唯一：存在未 Close 的写句柄时再次 Open(WRITE) 返回 FS_W25Q_ERR_BUSY
 *  （防止两个未提交句柄分配到同一空洞互相覆盖）；写句柄用完必须 Close，
 *   否则句柄标志悬挂至重启。读句柄无此限制；Remove 不影响已打开的读句柄
 *  （NOR 数据物理仍在，读完自然失效）；但写句柄活跃期间禁止对同名 Remove——
 *   后续 Close 会把该名字重新登记，形成"删了又复活"。
 * 约束：SPI1 总线单主人假设——仅 Uart_Proc 任务上下文访问；
 * 出现第二个 Flash 访问方时，须先在总线层加互斥锁。
 * 磁盘格式版本 2（双表 + seq + 数据区基址 0x2000），与旧 res_w25q/F4RP v1 单表格式不兼容：
 * 首次挂载检测旧格式或双副本均无效 → 自动格式化重建空表，存量资源需重新下载。
 */
#define FS_W25Q_MAGIC      0x50523446U /* 表标识 "F4RP"（小端字节序 F,4,R,P） */
#define FS_W25Q_VERSION    2U          /* 表布局版本（2 = 双表乒乓 + seq） */
#define FS_W25Q_MAX_FILES  16U         /* 条目数上限 */
#define FS_W25Q_NAME_MAX   16U         /* 文件名最大长度（含终止符） */
#define FS_W25Q_DATA_BASE  0x00002000U /* 数据区起始地址（扇区 2，扇区 0/1 为双份目录表） */

/* 打开模式标志（映射 FatFS FA_*，读写互斥，单句柄单模式） */
#define FS_W25Q_FA_READ   0x01U /* 读模式：按已有文件名打开 */
#define FS_W25Q_FA_WRITE  0x02U /* 写模式：创建或覆盖，须声明 size */

/* 目录条目（32 字节，字段顺序即 Flash 存储顺序，禁止插入填充） */
typedef struct {
    uint32_t offset;                /* 数据区起始地址 */
    uint32_t length;                /* 文件字节长度 */
    uint32_t crc32;                 /* 文件数据 CRC32 终值 */
    uint16_t flags;                 /* 属性标志（预留） */
    uint16_t reserved;              /* 对齐填充 */
    char name[FS_W25Q_NAME_MAX];    /* 文件名（NUL 终止） */
} FsW25qEntry_t;

/* 文件对象（句柄，映射 FatFS FIL；调用方分配，建议任务栈局部变量） */
typedef struct {
    uint32_t offset;                /* 数据区起始地址 */
    uint32_t size;                  /* 读模式 = 文件长度；写模式 = Open 声明的容量上限 */
    uint32_t pos;                   /* 读写游标 */
    uint32_t crc;                   /* 写模式：已写数据 CRC32 流式累积值 */
    uint16_t mode;                  /* FS_W25Q_FA_READ / FS_W25Q_FA_WRITE */
    uint16_t dirty;                 /* 1 = 写模式有未 Sync 数据（Close 时自动补 Sync） */
    char name[FS_W25Q_NAME_MAX];    /* 文件名副本（Sync/Close 登记用） */
} FsW25qFil_t;

/* 文件信息（映射 FatFS FILINFO，Stat/Readdir 输出） */
typedef struct {
    char fname[FS_W25Q_NAME_MAX];   /* 文件名 */
    uint32_t fsize;                 /* 文件字节长度 */
    uint32_t crc32;                 /* 文件数据 CRC32 终值 */
    uint32_t offset;                /* 物理起始地址（诊断用） */
} FsW25qFilInfo_t;

/* 目录迭代器（映射 FatFS DIR） */
typedef struct {
    uint32_t idx;                   /* 当前遍历索引 */
} FsW25qDir_t;

/* 错误码定义 */
typedef enum {
    FS_W25Q_OK = 0,          /* 成功 */
    FS_W25Q_ERR_FLASH,       /* Flash 访问失败（含 W25QXX 初始化失败） */
    FS_W25Q_ERR_PARAM,       /* 参数错误（含未挂载调用 Open、空文件 Sync 等非法状态） */
    FS_W25Q_ERR_FULL,        /* 目录条目数已满 */
    FS_W25Q_ERR_NO_SPACE,    /* 数据区无足够连续空间 / 写入超出声明容量 */
    FS_W25Q_ERR_NOT_FOUND,   /* 未找到指定文件 */
    FS_W25Q_ERR_MISMATCH,    /* 读回校验不一致 */
    FS_W25Q_ERR_GEOMETRY,    /* 表条目超出当前芯片容量（换芯片或表损坏） */
    FS_W25Q_ERR_NOT_READY,   /* 未挂载（先调 FsW25q_Mount） */
    FS_W25Q_ERR_BUSY,        /* 已有未 Close 的写句柄（写句柄全局唯一） */
} FsW25qErr_t;

/* 函数接口（与 FatFS 对应关系见尾注；全层零打印，结果经返回码与出参交付） */

/* 挂载 */
FsW25qErr_t FsW25q_Mount(void);                                                            /* f_mount：上电调一次，常挂载不卸载 */

/* 文件操作 */
FsW25qErr_t FsW25q_Open(FsW25qFil_t *fil, const char *name, uint16_t mode, uint32_t size); /* f_open：WRITE 模式含分配+擦除 */
FsW25qErr_t FsW25q_Close(FsW25qFil_t *fil);                                                /* f_close：写模式有脏数据先 Sync */
FsW25qErr_t FsW25q_Sync(FsW25qFil_t *fil);                                                 /* f_sync：中途落表，掉电保数据 */
FsW25qErr_t FsW25q_Read(FsW25qFil_t *fil, uint8_t *buf, uint32_t btr, uint32_t *br);       /* f_read：EOF 短读，*br 为实际值 */
FsW25qErr_t FsW25q_Write(FsW25qFil_t *fil, const uint8_t *buf, uint32_t btw, uint32_t *bw); /* f_write：顺序追加 */
FsW25qErr_t FsW25q_Lseek(FsW25qFil_t *fil, uint32_t pos);                                  /* f_lseek：仅读模式 */

/* 目录与维护 */
FsW25qErr_t FsW25q_Stat(const char *name, FsW25qFilInfo_t *info);                          /* f_stat */
FsW25qErr_t FsW25q_Remove(const char *name);                                                /* f_unlink：数据区自动成空洞 */
FsW25qErr_t FsW25q_Opendir(FsW25qDir_t *dir);                                               /* f_opendir */
FsW25qErr_t FsW25q_Readdir(FsW25qDir_t *dir, FsW25qFilInfo_t *info);                        /* f_readdir：结束 fname[0]='\0' */
uint32_t FsW25q_GetFree(void);                                                              /* f_getfree 简化：空洞总字节数 */
FsW25qErr_t FsW25q_Verify(const char *name, uint32_t *calc_crc);                            /* 增值项：读回 CRC 比对（FatFS 无对应） */

#endif /* __FS_W25Q_H */
