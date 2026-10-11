#ifndef __YMODEM_H
#define __YMODEM_H

#include "main.h"
#include "usart.h"

/* Ymodem 接收通道配置 */
#define YMODEM_UART huart1 /* 与调试输出共用的 UART 句柄，传输期间独占 */

/* 文件名最大长度（含终止符） */
#define YMODEM_NAME_MAX  64U

/* 错误码定义 */
typedef enum {
    YMODEM_OK = 0,        /* 接收完成 */
    YMODEM_ERR_TIMEOUT,   /* 握手/包等待超时（发送方未响应） */
    YMODEM_ERR_ABORTED,   /* 发送方 CAN 中止 */
    YMODEM_ERR_PROTOCOL,  /* 连续坏包超限 */
    YMODEM_ERR_PARAM,     /* 头块解析失败（文件名/长度非法） */
    YMODEM_ERR_REJECT,    /* 接收方回调拒绝（存储层返回非 0） */
} YmodemErr_t;

/* 数据接收回调集合（推送式：协议层把收到的数据逐包推给上层）
 * 所有回调返回 0 = 继续接收，非 0 = 中止本次传输 */
typedef struct {
    /* 头块到达：name=文件名，size=文件字节数（八进制解析） */
    int (*on_header)(const char *name, uint32_t size, void *ctx);
    /* 数据包到达：data=数据指针，len=有效长度，offset=已接收累计偏移 */
    int (*on_data)(const uint8_t *data, uint32_t len, uint32_t offset, void *ctx);
    /* 传输结束：total_size=实际接收总字节数 */
    int (*on_end)(uint32_t total_size, void *ctx);
    void *ctx; /* 调用方上下文，透传给三个回调 */
} YmodemSink_t;

/* 接收单文件（CRC 模式，1K/128 包兼容，单文件批次）
 * 前置：调用方已停止常规 UART 接收并静默输出
 * 返回: YmodemErr_t；若回调返回非 0 则返回 YMODEM_ERR_REJECT */
YmodemErr_t Ymodem_Receive(const YmodemSink_t *sink);

#endif /* __YMODEM_H */
