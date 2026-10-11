#include "uart_task.h"
#include "res_w25q.h"
#include "usart.h"
#include "w25qxx.h"

#include <string.h>

/* ========== 命令处理 ========== */

/* W25QXX 懒初始化：失败则下次命令时重试，不占启动流程
 * 返回: 1 = 已就绪, 0 = 初始化失败 */
static uint8_t Uart_W25EnsureInit(void)
{
    static uint8_t s_W25Inited = 0; /* 1 = 初始化已成功 */

    if (s_W25Inited)
        return 1;
    if (W25QXX_Init() == W25QXX_OK)
    {
        s_W25Inited = 1;
        return 1;
    }
    return 0;
}

/* 打印 W25QXX 芯片信息（命令 w25info） */
static void Uart_CmdW25Info(void)
{
    W25QXXInfo_t info;

    if (!Uart_W25EnsureInit())
    {
        Uart_Printf("W25QXX init failed\r\n");
        return;
    }

    W25QXX_GetInfo(&info);
    Uart_Printf("JEDEC: 0x%06lX | Capacity: %lu MB | Sector: %lu | Page: %lu\r\n",
                info.jedec_id, info.size_mb, info.sector_count, info.page_count);
}

/* 取出下一个空格分隔参数到 dst
 * args: 剩余参数串
 * dst/dst_max: 参数输出缓冲及容量
 * 返回: 本参数之后的剩余串指针；无更多参数返回 NULL */
static const char *Uart_NextArg(const char *args, char *dst, uint16_t dst_max)
{
    while (*args == ' ')
        args++;
    if (*args == '\0')
        return NULL;

    uint16_t n = 0;
    while (*args != '\0' && *args != ' ' && n + 1U < dst_max)
        dst[n++] = *args++;
    dst[n] = '\0';
    while (*args != '\0' && *args != ' ')
        args++; /* 超长部分丢弃到下一空格 */

    return args;
}

/* 十进制字符串解析为 uint32；含非法字符或空串返回 0 */
static uint8_t Uart_ParseU32(const char *s, uint32_t *out)
{
    if (*s == '\0')
        return 0;

    uint32_t v = 0;
    while (*s >= '0' && *s <= '9')
    {
        v = v * 10U + (uint32_t)(*s - '0');
        s++;
    }
    if (*s != '\0')
        return 0;

    *out = v;
    return 1;
}

/* 分区表命令：part=列目录 | part chk <name>=读回校验 | part gen <name> <size>=写测试图案
 * args: 命令词 part 之后的串 */
static void Uart_CmdPart(const char *args)
{
    char token[RES_W25Q_NAME_MAX];

    while (*args == ' ')
        args++;

    if (*args == '\0')
    {
        ResW25q_List();
        return;
    }

    if (strncmp(args, "chk", 3) == 0 && (args[3] == '\0' || args[3] == ' '))
    {
        args += 3;
        if (Uart_NextArg(args, token, sizeof(token)) == NULL)
            Uart_Printf("usage: part chk <name>\r\n");
        else
            ResW25q_Verify(token);
        return;
    }

    if (strncmp(args, "gen", 3) == 0 && (args[3] == '\0' || args[3] == ' '))
    {
        char num[16];

        args += 3;
        args = Uart_NextArg(args, token, sizeof(token));
        if (args == NULL)
        {
            Uart_Printf("usage: part gen <name> <size>\r\n");
            return;
        }
        args = Uart_NextArg(args, num, sizeof(num));

        uint32_t size;
        if (args == NULL || !Uart_ParseU32(num, &size) || size == 0)
        {
            Uart_Printf("usage: part gen <name> <size>\r\n");
            return;
        }
        ResW25q_GenPattern(token, size);
        return;
    }

    Uart_Printf("unknown: part %s\r\n", args);
}

/* Ymodem 接收命令：提示发送方后调用资源入库服务（接管串口→协议接收→W25Qxx 落盘→恢复） */
static void Uart_CmdYmodem(void)
{
    Uart_Printf("ymodem: ready, start sender now (<=30s)...\r\n");
    ResW25q_YmodemStore();
}

/* 命令分发：结行（尾部 \r 已由调用方剥离）精确匹配，未命中命令镜像回显
 * line: 已终止的命令行 */
static void Uart_CmdDispatch(const char *line)
{
    if (strcmp(line, "w25info") == 0)
    {
        Uart_CmdW25Info();
        return;
    }

    if (strcmp(line, "ym") == 0)
    {
        Uart_CmdYmodem();
        return;
    }

    if (strncmp(line, "part", 4) == 0 && (line[4] == '\0' || line[4] == ' '))
    {
        Uart_CmdPart(line + 4);
        return;
    }

    Uart_Printf("[Rx] %s\r\n", line);
}

/* RX 任务：等信号量→取帧→按 \n 组行→命令分发（未知命令镜像 [Rx] 回显）。
 * 新增命令时在 Uart_CmdDispatch 中追加匹配分支 */
void Uart_Proc(void *argument)
{
    (void)argument;

    static char s_RxLine[UART_RX_LINE_MAX + 1];
    static uint16_t s_RxLineLen = 0;

    for (;;)
    {
        uint8_t *buf;
        uint16_t len;

        osSemaphoreAcquire(Uart1RxSemHandle, osWaitForever);

        buf = Uart_RxFetchFrame(&len);
        if (buf == NULL || len == 0)
            continue;

        /* 快拷到栈缓冲，缩短 DMA 回收该缓冲前的读取窗口 */
        uint8_t frame[UART_RX_BUF_MAX];
        memcpy(frame, buf, len);

        for (uint16_t i = 0; i < len; i++)
        {
            char c = (char)frame[i];

            if (c == '\n')
            {
                s_RxLine[s_RxLineLen] = '\0';
                if (s_RxLineLen > 0 && s_RxLine[s_RxLineLen - 1] == '\r')
                    s_RxLine[--s_RxLineLen] = '\0'; /* 剥离 CRLF 行尾的 \r */
                Uart_CmdDispatch(s_RxLine);
                s_RxLineLen = 0;
            }
            else if (s_RxLineLen < UART_RX_LINE_MAX)
            {
                s_RxLine[s_RxLineLen++] = c;
            }
            /* 行超长：丢弃后续字节直到下一个 \n */
        }
    }
}
