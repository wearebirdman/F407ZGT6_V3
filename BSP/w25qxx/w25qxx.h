#ifndef __W25QXX_H
#define __W25QXX_H

#include "main.h"

/* SPI 与片选引脚配置宏 */
#define W25QXX_SPI     hspi1
#define W25QXX_CS_PORT GPIOG
#define W25QXX_CS_PIN  GPIO_PIN_15

/* 芯片型号选择（取消注释其中一个） */
/* #define W25Q32 */
#define W25Q128

/* 芯片容量通用参数定义（所有型号相同） */
#define W25QXX_PAGE_SIZE      256   /* 页大小：256 字节 */
#define W25QXX_SECTOR_SIZE    4096  /* 扇区大小：4KB,一个扇区包含 16 个页 */
#define W25QXX_BLOCK_SIZE     65536 /* 块大小：64KB,一个块包含 16 个扇区 */

/* W25Q32 芯片容量参数（4MB = 32Mbit） */
#ifdef W25Q32
#define W25QXX_CHIP_SIZE    4194304UL   /* 总容量：4MB */
#define W25QXX_PAGE_COUNT   16384       /* 页数量 (4MB / 256B) */
#define W25QXX_SECTOR_COUNT 1024        /* 扇区数量 (4MB / 4KB) */
#define W25QXX_BLOCK_COUNT  64          /* 块数量 (4MB / 64KB) */
#define W25QXX_EXPECTED_ID  0xEF4016    /* JEDEC ID (Winbond 0xEF, Type 0x40, Size 0x16) */
#endif

/* W25Q128 芯片容量参数（16MB = 128Mbit） */
#ifdef W25Q128
#define W25QXX_CHIP_SIZE    16777216UL  /* 总容量：16MB */
#define W25QXX_PAGE_COUNT   65536       /* 页数量 (16MB / 256B) */
#define W25QXX_SECTOR_COUNT 4096        /* 扇区数量 (16MB / 4KB) */
#define W25QXX_BLOCK_COUNT  256         /* 块数量 (16MB / 64KB) */
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
#define W25QXX_TEST_ADDR 0x000000 /* 测试地址（扇区首地址，示例代码用） */

/* 地址对齐辅助宏 */
#define W25QXX_SECTOR_ALIGN(addr) ((addr) & ~(W25QXX_SECTOR_SIZE - 1)) /* 扇区对齐 */

/* W25QXX 指令集定义 */
#define W25QXX_JEDEC_ID           0x9F /* 读取 JEDEC ID */
#define W25QXX_READ               0x03 /* 读数据 */
#define W25QXX_WRITE_EN           0x06 /* 写使能 */
#define W25QXX_WRITE_DIS          0x04 /* 写禁止 */
#define W25QXX_PAGE_PROGRAM       0x02 /* 页编程 */
#define W25QXX_SECTOR_ERASE       0x20 /* 扇区擦除（4KB） */
#define W25QXX_READ_STATUS1       0x05 /* 读状态寄存器 1 */
#define W25QXX_POWER_DOWN         0xB9 /* 掉电模式 */
#define W25QXX_RELEASE_POWER_DOWN 0xAB /* 释放掉电模式 */

/* W25QXX 状态寄存器 1 位定义 */
#define W25QXX_SR1_BUSY 0x01 /* 忙标志（1 = 忙） */
#define W25QXX_SR1_WEL  0x02 /* 写使能锁存（1 = 已使能） */
#define W25QXX_SR1_BP0  0x04 /* 块保护位 0 */
#define W25QXX_SR1_BP1  0x08 /* 块保护位 1 */
#define W25QXX_SR1_BP2  0x10 /* 块保护位 2 */

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

/* 函数接口（按消费方分类） */

/* ---- A 类：文件系统核心（SVC/fs 唯一消费）---- */
W25QXXErr_t W25QXX_Init(void);                                       /* 初始化（验证芯片 ID，含唤醒） */
void W25QXX_GetInfo(W25QXXInfo_t *info);                             /* 获取芯片信息 */
W25QXXErr_t W25QXX_Read(uint32_t addr, uint8_t *buf, uint32_t len);  /* 读取数据（整块接收） */
W25QXXErr_t W25QXX_Write(uint32_t addr, const uint8_t *buf, uint32_t len); /* 写入数据（自动跨页拆分，需目标区已擦除） */
W25QXXErr_t W25QXX_SectorErase(uint32_t addr);                       /* 扇区擦除（4KB） */

/* ---- B 类：电源管理（系统低功耗策略用，文件系统不调用）---- */
W25QXXErr_t W25QXX_PowerDown(void);                                  /* 进入掉电模式 */
W25QXXErr_t W25QXX_WakeUp(void);                                     /* 退出掉电模式 */

#if W25QXX_ENABLE_EXAMPLE
void W25QXX_Example(void);                                           /* 使用示例 */
#endif

#endif /* __W25QXX_H */
