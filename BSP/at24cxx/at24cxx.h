#ifndef __AT24CXX_H
#define __AT24CXX_H

#include "main.h"

/* I2C 与设备地址配置宏 */
#define AT24CXX_I2C      hi2c1 /* 使用的 I2C 句柄 */
#define AT24CXX_DEV_ADDR 0x50  /* 7位设备地址（常用0x50，写0xA0/读0xA1） */

/* 芯片型号选择（取消注释其中一个） */
#define AT24C02
/* #define AT24C04 */
/* #define AT24C08 */
/* #define AT24C16 */
/* #define AT24C32 */
/* #define AT24C64 */

/* AT24C02 芯片容量参数（256 字节 = 2Kbit） */
#ifdef AT24C02
#define AT24CXX_PAGE_SIZE   16                   /* 页大小：16 字节 */
#define AT24CXX_CHIP_SIZE   256                  /* 总容量：256 字节（2Kbit） */
#define AT24CXX_PAGE_COUNT  16                   /* 页数量 (256 / 16) */
#define AT24CXX_MEMADD_SIZE I2C_MEMADD_SIZE_8BIT /* 存储器地址宽度：8 位 */
#endif

/* AT24C04 芯片容量参数（512 字节 = 4Kbit） */
#ifdef AT24C04
#define AT24CXX_PAGE_SIZE   16
#define AT24CXX_CHIP_SIZE   512
#define AT24CXX_PAGE_COUNT  32
#define AT24CXX_MEMADD_SIZE I2C_MEMADD_SIZE_8BIT
#endif

/* AT24C08 芯片容量参数（1024 字节 = 8Kbit） */
#ifdef AT24C08
#define AT24CXX_PAGE_SIZE   16
#define AT24CXX_CHIP_SIZE   1024
#define AT24CXX_PAGE_COUNT  64
#define AT24CXX_MEMADD_SIZE I2C_MEMADD_SIZE_8BIT
#endif

/* AT24C16 芯片容量参数（2048 字节 = 16Kbit） */
#ifdef AT24C16
#define AT24CXX_PAGE_SIZE   16
#define AT24CXX_CHIP_SIZE   2048
#define AT24CXX_PAGE_COUNT  128
#define AT24CXX_MEMADD_SIZE I2C_MEMADD_SIZE_8BIT
#endif

/* AT24C32 芯片容量参数（4096 字节 = 32Kbit） */
#ifdef AT24C32
#define AT24CXX_PAGE_SIZE   32
#define AT24CXX_CHIP_SIZE   4096
#define AT24CXX_PAGE_COUNT  128
#define AT24CXX_MEMADD_SIZE I2C_MEMADD_SIZE_16BIT
#endif

/* AT24C64 芯片容量参数（8192 字节 = 64Kbit） */
#ifdef AT24C64
#define AT24CXX_PAGE_SIZE   32
#define AT24CXX_CHIP_SIZE   8192
#define AT24CXX_PAGE_COUNT  256
#define AT24CXX_MEMADD_SIZE I2C_MEMADD_SIZE_16BIT
#endif

/* 检查是否选择了芯片型号 */
#ifndef AT24CXX_CHIP_SIZE
#error "Please define AT24C02 or other model in at24cxx.h"
#endif

/* 常用地址宏 */
#define AT24CXX_TEST_ADDR 0x00 /* 测试地址 */

/*
 * EEPROM 数据分配表：所有模块的持久化数据地址统一由本表管理
 * （容量预算小、重叠代价高，集中一张表才能一眼看出冲突）。
 * 新增数据区时：
 * 1. 在此定义 ADDR / SIZE
 * 2. 各外设头文件只引用本表宏，不自行拍地址
 * 当前分配：
 * 0x00 ~ 0x0F  测试区（AT24CXX_Example，16 字节）
 * 0x28 ~ 0x38  触摸校准参数（17 字节）
 * 空闲起始：   0x39
 */
#define EEPROM_ADDR_TOUCH_CAL 40 /* 触摸校准参数起始地址 */
#define EEPROM_SIZE_TOUCH_CAL 17 /* 触摸校准参数长度 */

/* 示例代码开关 */
#define AT24CXX_ENABLE_EXAMPLE 1 /* 1=启用示例代码，0=停用 */

/* 错误码定义 */
typedef enum {
    AT24CXX_OK = 0,      /* 成功 */
    AT24CXX_ERR_DEVICE,  /* 设备未应答（未检测到） */
    AT24CXX_ERR_TIMEOUT, /* 超时（写周期等待超时） */
    AT24CXX_ERR_ADDR,    /* 地址越界 */
    AT24CXX_ERR_PARAM,   /* 参数错误 */
} AT24CXXErr_t;

/* 芯片信息结构体 */
typedef struct {
    uint32_t size_bytes; /* 总容量（字节） */
    uint32_t page_size;  /* 页大小（字节） */
    uint32_t page_count; /* 页数量 */
    uint16_t dev_addr;   /* 7位设备地址 */
} AT24CXXInfo_t;

/* 函数接口 */
AT24CXXErr_t AT24CXX_Init(void);                                              /* 初始化（检测设备在位） */
void AT24CXX_GetInfo(AT24CXXInfo_t *info);                                    /* 获取芯片信息 */
AT24CXXErr_t AT24CXX_Read(uint16_t addr, uint8_t *buf, uint16_t len);         /* 读取数据 */
AT24CXXErr_t AT24CXX_PageWrite(uint16_t addr, uint8_t *buf, uint16_t len);    /* 页写入（单次最多一页） */
AT24CXXErr_t AT24CXX_Write(uint16_t addr, uint8_t *buf, uint16_t len);        /* 写入数据（自动处理跨页） */
void AT24CXX_Example(void);                                                   /* 使用示例 */

#endif /* __AT24CXX_H */
