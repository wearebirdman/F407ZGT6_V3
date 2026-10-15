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

/*
 * 记录层存储模型（A/B 双分区，掉电安全）：整片对半划分为两个记录槽，
 * 由 AT24Cxx_SaveRecord/LoadRecord 统一管理，调用方不接触任何存储地址。
 * 槽结构：[magic 4B][seq 4B][len 2B][crc32 4B][数据 ≤ AT24CXX_REC_DATA_MAX]
 * 写入：数据先落非活动槽、头部最后写（seq=历史最大值+1）——任一步掉电时
 *       旧活动槽始终完整，半写的槽由 CRC 校验兜底识别；
 * 读取：双槽各自验 CRC，取 seq 大的有效槽；皆无效返回 AT24CXX_ERR_NOT_FOUND。
 * 模型限制：当前为单记录（整片一份数据，触摸校准是唯一使用者）；
 *           将来出现第二类持久化数据时扩展记录 id，禁止绕过本层手写地址读写。
 */
#define AT24CXX_SLOT_SIZE    (AT24CXX_CHIP_SIZE / 2U) /* 单记录槽容量（字节） */
#define AT24CXX_REC_HDR_SIZE 14U                      /* 记录头：magic+seq+len+crc32 */
#define AT24CXX_REC_DATA_MAX (AT24CXX_SLOT_SIZE - AT24CXX_REC_HDR_SIZE) /* 单记录数据上限 */

/* 示例代码开关 */
#define AT24CXX_ENABLE_EXAMPLE 0 /* 1=启用示例代码，0=停用 */

/* 错误码定义 */
typedef enum {
    AT24CXX_OK = 0,      /* 成功 */
    AT24CXX_ERR_DEVICE,  /* 设备未应答（未检测到） */
    AT24CXX_ERR_TIMEOUT, /* 超时（写周期等待超时） */
    AT24CXX_ERR_ADDR,    /* 地址越界 */
    AT24CXX_ERR_PARAM,   /* 参数错误 */
    AT24CXX_ERR_NOT_FOUND, /* 无有效记录（从未保存或双槽皆损坏） */
} AT24CxxErr_t;

/* 芯片信息结构体 */
typedef struct {
    uint32_t size_bytes; /* 总容量（字节） */
    uint32_t page_size;  /* 页大小（字节） */
    uint32_t page_count; /* 页数量 */
    uint16_t dev_addr;   /* 7位设备地址 */
} AT24CxxInfo_t;

/* 函数接口 */
AT24CxxErr_t AT24Cxx_Init(void);                                              /* 初始化（检测设备在位） */
void AT24Cxx_GetInfo(AT24CxxInfo_t *info);                                    /* 获取芯片信息 */
AT24CxxErr_t AT24Cxx_Read(uint16_t addr, uint8_t *buf, uint16_t len);         /* 读取数据 */
AT24CxxErr_t AT24Cxx_PageWrite(uint16_t addr, const uint8_t *buf, uint16_t len); /* 页写入（单次最多一页） */
AT24CxxErr_t AT24Cxx_Write(uint16_t addr, const uint8_t *buf, uint16_t len);     /* 写入数据（自动处理跨页） */

/* 记录层接口（A/B 双分区，掉电安全；持久化数据统一走这组 API，不直接用地址读写数据区） */
AT24CxxErr_t AT24Cxx_SaveRecord(const uint8_t *data, uint16_t len);          /* 保存记录：写非活动槽并提交，旧记录自动保留为备份 */
AT24CxxErr_t AT24Cxx_LoadRecord(uint8_t *data, uint16_t buf_max, uint16_t *out_len); /* 读出最新有效记录，*out_len 为记录长度 */
#if AT24CXX_ENABLE_EXAMPLE
void AT24Cxx_Example(void);                                                   /* 使用示例 */
#endif

#endif /* __AT24CXX_H */
