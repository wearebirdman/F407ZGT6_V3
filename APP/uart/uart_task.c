#include "uart_task.h"
#include "usart.h"

#include <string.h>

/* RX 任务：等信号量→取帧→按 \n 组行→镜像 [Rx] 上屏。
 * 指令解析分发点：后续支持命令时在结行后判断前缀 */
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
                Uart_Printf("[Rx] %s\r\n", s_RxLine);
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
