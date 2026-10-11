#ifndef __RES_W25Q_H
#define __RES_W25Q_H

#include "main.h"
#include "ymodem.h" /* SVC 平层依赖：入库服务复用协议层错误码与 sink 类型 */

/* ========== 模块定位 ==========
 * 资源存储服务（W25Qxx 后端）：管理"接收到的数据存入 W25Qxx"的完整过程——
 * 分区分配/擦除、逐块落盘、目录登记、读回校验、Ymodem 接收入库一条龙。
 * 后续新增其他存储后端（内部 Flash、SD 卡等）时，在本目录平级新增
 * res_xxx.c/h 文件对即可，ymodem 协议层与各后端互不依赖。
 *
 * 分区布局（W25Qxx 16MB）：
 *   0x000000  扇区 0：分区表（本模块管理，magic/版本/条目数组/表 CRC）
 *   0x001000~ 数据区：每个资源按 4KB 扇区对齐分配，first-fit 空洞复用
 * 约束：SPI1 总线单主人假设——仅本模块（Uart_Proc 任务上下文）访问 W25Qxx 写入路径，
 * 未来出现第二个 Flash 写入方时，须先在总线层加互斥锁。
 */
#define RES_W25Q_MAGIC       0x50523446U /* 表标识 "F4RP"（小端字节序 F,4,R,P） */
#define RES_W25Q_VERSION     1U          /* 表布局版本 */
#define RES_W25Q_MAX_FILES   16U         /* 条目数上限 */
#define RES_W25Q_NAME_MAX    16U         /* 文件名最大长度（含终止符） */
#define RES_W25Q_DATA_BASE   0x00001000U /* 数据区起始地址（扇区 1） */

/* 分区条目（32 字节，字段顺序即 Flash 存储顺序，禁止插入填充） */
typedef struct {
    uint32_t offset;                    /* 数据区起始地址 */
    uint32_t length;                    /* 文件字节长度 */
    uint32_t crc32;                     /* 文件数据 CRC32 终值 */
    uint16_t flags;                     /* 属性标志（预留） */
    uint16_t reserved;                  /* 对齐填充 */
    char name[RES_W25Q_NAME_MAX];       /* 文件名（NUL 终止） */
} ResW25qEntry_t;

/* 错误码定义 */
typedef enum {
    RES_W25Q_OK = 0,        /* 成功 */
    RES_W25Q_ERR_FLASH,     /* Flash 访问失败（含 W25QXX 初始化失败） */
    RES_W25Q_ERR_PARAM,     /* 参数错误 */
    RES_W25Q_ERR_FULL,      /* 条目数已满 */
    RES_W25Q_ERR_NO_SPACE,  /* 数据区无足够连续空间 */
    RES_W25Q_ERR_NOT_FOUND, /* 未找到指定资源 */
    RES_W25Q_ERR_MISMATCH,  /* 读回校验不一致 */
} ResW25qErr_t;

/* 函数接口（CRC32 等纯工具已内聚为模块内部实现，对外只暴露领域 API） */
ResW25qErr_t ResW25q_Init(void);                                                        /* 初始化（幂等）：加载表，无效则格式化重建 */
ResW25qErr_t ResW25q_Find(const char *name, ResW25qEntry_t *out);                      /* 按名字查条目信息（资源消费方使用） */
void ResW25q_List(void);                                                                /* 打印分区目录 */
ResW25qErr_t ResW25q_Alloc(const char *name, uint32_t size, uint32_t *offset);         /* 分配数据区并擦除覆盖扇区（不写表） */
ResW25qErr_t ResW25q_Commit(const char *name, uint32_t offset, uint32_t length,
                            uint32_t crc32);                                            /* 写入/更新条目并持久化表 */
ResW25qErr_t ResW25q_Verify(const char *name);                                          /* 读回数据算 CRC 与表比对 */
ResW25qErr_t ResW25q_GenPattern(const char *name, uint32_t size);                      /* 自测：按地址图案生成数据写入并登记（绕过 Ymodem 的闭环测试） */
YmodemErr_t ResW25q_YmodemStore(void);                                                  /* Ymodem 接收入库一条龙：接管串口→协议接收→W25Qxx 落盘→恢复 */

#endif /* __RES_W25Q_H */
