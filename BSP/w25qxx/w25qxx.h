#ifndef __W25QXX_H
#define __W25QXX_H

#include "main.h"

/* SPI 与片选引脚配置宏 */
#define W25QXX_SPI     hspi1
#define W25QXX_CS_PORT GPIOA
#define W25QXX_CS_PIN  GPIO_PIN_15

/* 芯片型号选择（取消注释其中一个） */
/* #define W25Q32 */
#define W25Q128

/* 芯片容量通用参数定义（所有型号相同） */
#define W25QXX_PAGE_SIZE      256   /* 页大小：256 字节 */
#define W25QXX_SECTOR_SIZE    4096  /* 扇区大小：4KB,一个扇区包含 16 个页 */
#define W25QXX_BLOCK_SIZE     65536 /* 块大小：64KB,一个块包含 16 个扇区 */
#define W25QXX_BLOCK_32K_SIZE 32768 /* 32KB 块大小 */

/* W25Q32 芯片容量参数（4MB = 32Mbit） */
#ifdef W25Q32
#define W25QXX_CHIP_SIZE    4194304UL   /* 总容量：4MB */
#define W25QXX_PAGE_COUNT   16384       /* 页数量 (4MB / 256B) */
#define W25QXX_SECTOR_COUNT 1024        /* 扇区数量 (4MB / 4KB) */
#define W25QXX_BLOCK_COUNT  64          /* 块数量 (4MB / 64KB) */
#define W25QXX_EXPECTED_ID  0xEF4016    /* JEDEC ID (Winbond 0xEF, Type 0x40, Size 0x16) */
#endif

/* W25Q128 芯片容量参数（128MB = 1024Mbit） */
#ifdef W25Q128
#define W25QXX_CHIP_SIZE    134217728UL /* 总容量：128MB */
#define W25QXX_PAGE_COUNT   524288      /* 页数量 (128MB / 256B) */
#define W25QXX_SECTOR_COUNT 32768       /* 扇区数量 (128MB / 4KB) */
#define W25QXX_BLOCK_COUNT  2048        /* 块数量 (128MB / 64KB) */
#define W25QXX_EXPECTED_ID  0xEF4018    /* JEDEC ID (Winbond 0xEF, Type 0x40, Size 0x18) */
#endif

/* 检查是否选择了芯片型号 */
#ifndef W25QXX_CHIP_SIZE
#error "Please define W25Q32 or W25Q128 in w25qxx.h"
#endif

/*
 * 使用24位地址模式，最大支持16MB容量的芯片：
 * 低8位地址（A0-A7）：页偏移量
 * 中8位地址（A8-A15）：A8-A11为页号，A12-A15为块内扇区号
 * 高8位地址（A16-A23）：块号
 */

/* 常用地址宏 */
#define W25QXX_TEST_ADDR        0x000000                                 /* 测试地址（扇区首地址） */
#define W25QXX_LAST_SECTOR_ADDR (W25QXX_CHIP_SIZE - W25QXX_SECTOR_SIZE)  /* 最后一个扇区首地址 */

/* 地址对齐辅助宏 */
#define W25QXX_PAGE_ALIGN(addr)      ((addr) & ~(W25QXX_PAGE_SIZE - 1))      /* 页对齐 */
#define W25QXX_SECTOR_ALIGN(addr)    ((addr) & ~(W25QXX_SECTOR_SIZE - 1))    /* 扇区对齐 */
#define W25QXX_BLOCK_ALIGN(addr)     ((addr) & ~(W25QXX_BLOCK_SIZE - 1))     /* 块对齐 */
#define W25QXX_BLOCK_32K_ALIGN(addr) ((addr) & ~(W25QXX_BLOCK_32K_SIZE - 1)) /* 32KB 块对齐 */

/* 地址检查辅助宏 */
#define W25QXX_IS_SECTOR_ADDR(addr)    (((addr) % W25QXX_SECTOR_SIZE) == 0)    /* 是否为扇区首地址 */
#define W25QXX_IS_BLOCK_64K_ADDR(addr) (((addr) % W25QXX_BLOCK_SIZE) == 0)     /* 是否为 64KB 块首地址 */
#define W25QXX_IS_BLOCK_32K_ADDR(addr) (((addr) % W25QXX_BLOCK_32K_SIZE) == 0) /* 是否为 32KB 块首地址 */

/* W25QXX 指令集定义 */
#define W25QXX_JEDEC_ID           0x9F /* 读取 JEDEC ID */
#define W25QXX_READ               0x03 /* 读数据 */
#define W25QXX_FAST_READ          0x0B /* 快速读 */
#define W25QXX_WRITE_EN           0x06 /* 写使能 */
#define W25QXX_WRITE_DIS          0x04 /* 写禁止 */
#define W25QXX_PAGE_PROGRAM       0x02 /* 页编程 */
#define W25QXX_SECTOR_ERASE       0x20 /* 扇区擦除（4KB） */
#define W25QXX_BLOCK_ERASE_32K    0x52 /* 块擦除（32KB） */
#define W25QXX_BLOCK_ERASE_64K    0xD8 /* 块擦除（64KB） */
#define W25QXX_CHIP_ERASE         0xC7 /* 整片擦除 */
#define W25QXX_READ_STATUS1       0x05 /* 读状态寄存器 1 */
#define W25QXX_READ_STATUS2       0x35 /* 读状态寄存器 2 */
#define W25QXX_READ_STATUS3       0x15 /* 读状态寄存器 3 */
#define W25QXX_WRITE_STATUS1      0x01 /* 写状态寄存器 1 */
#define W25QXX_WRITE_STATUS2      0x31 /* 写状态寄存器 2 */
#define W25QXX_WRITE_STATUS3      0x11 /* 写状态寄存器 3 */
#define W25QXX_POWER_DOWN         0xB9 /* 掉电模式 */
#define W25QXX_RELEASE_POWER_DOWN 0xAB /* 释放掉电模式 */

/* W25QXX 状态寄存器 1 位定义 */
#define W25QXX_SR1_BUSY 0x01 /* 忙标志（1 = 忙） */
#define W25QXX_SR1_WEL  0x02 /* 写使能锁存（1 = 已使能） */
#define W25QXX_SR1_BP0  0x04 /* 块保护位 0 */
#define W25QXX_SR1_BP1  0x08 /* 块保护位 1 */
#define W25QXX_SR1_BP2  0x10 /* 块保护位 2 */
#define W25QXX_SR1_TB   0x20 /* 顶部/底部保护 */
#define W25QXX_SR1_SEC  0x40 /* 扇区/块保护 */
#define W25QXX_SR1_SRP0 0x80 /* 状态寄存器保护 0 */

/* 示例代码开关 */
#define W25QXX_ENABLE_EXAMPLE 0 /* 1=启用示例代码，0=停用 */

/* 错误码定义 */
typedef enum {
    W25QXX_OK = 0,        /* 成功 */
    W25QXX_ERR_ID,        /* 芯片 ID 错误 */
    W25QXX_ERR_TIMEOUT,   /* 超时 */
    W25QXX_ERR_PROTECTED, /* 写保护 */
    W25QXX_ERR_ADDR,      /* 地址越界 */
    W25QXX_ERR_PARAM,     /* 参数错误 */
} W25QXXErr_t;

/* 芯片信息结构体 */
typedef struct {
    uint32_t jedec_id;     /* JEDEC ID */
    uint8_t manufacturer;  /* 制造商 ID */
    uint8_t memory_type;   /* 存储类型 */
    uint8_t capacity;      /* 容量 ID */
    uint32_t size_kb;      /* 容量（KB） */
    uint32_t size_mb;      /* 容量（MB） */
    uint32_t page_count;   /* 页数量 */
    uint32_t sector_count; /* 扇区数量 */
    uint32_t block_count;  /* 块数量 */
} W25QXXInfo_t;

/* 函数接口 */
W25QXXErr_t W25QXX_Init(void);                                               /* 初始化（验证芯片ID） */
void W25QXX_GetInfo(W25QXXInfo_t *info);                                     /* 获取芯片信息 */
uint32_t W25QXX_ReadID(void);                                                /* 读取 JEDEC ID（SPI 失败返回 0） */
uint8_t W25QXX_ReadStatus1(void);                                            /* 读状态寄存器 1（SPI 失败返回 0xFF） */
uint8_t W25QXX_ReadStatus2(void);                                            /* 读状态寄存器 2（SPI 失败返回 0xFF） */
uint8_t W25QXX_ReadStatus3(void);                                            /* 读状态寄存器 3（SPI 失败返回 0xFF） */
W25QXXErr_t W25QXX_WriteEnable(void);                                        /* 写使能 */
W25QXXErr_t W25QXX_WriteDisable(void);                                       /* 写禁止 */
W25QXXErr_t W25QXX_WaitBusy(void);                                           /* 等待芯片空闲（默认超时） */
W25QXXErr_t W25QXX_Read(uint32_t addr, uint8_t *buf, uint32_t len);          /* 读取数据 */
W25QXXErr_t W25QXX_Write(uint32_t addr, uint8_t *buf, uint32_t len);         /* 写入数据（自动处理跨页） */
W25QXXErr_t W25QXX_PageWrite(uint32_t addr, uint8_t *buf, uint16_t len);     /* 页写入（单次最多256字节） */
W25QXXErr_t W25QXX_SectorErase(uint32_t addr);                               /* 扇区擦除（4KB） */
W25QXXErr_t W25QXX_BlockErase32K(uint32_t addr);                             /* 块擦除（32KB） */
W25QXXErr_t W25QXX_BlockErase64K(uint32_t addr);                             /* 块擦除（64KB） */
W25QXXErr_t W25QXX_ChipErase(void);                                          /* 整片擦除 */
W25QXXErr_t W25QXX_PowerDown(void);                                          /* 进入掉电模式 */
W25QXXErr_t W25QXX_WakeUp(void);                                             /* 退出掉电模式 */
uint8_t W25QXX_IsBusy(void);                                                 /* 查询芯片是否忙碌 */
uint8_t W25QXX_IsWriteEnabled(void);                                         /* 查询写使能状态 */
void W25QXX_Example(void);                                                   /* 使用示例 */

#endif /* __W25QXX_H */
