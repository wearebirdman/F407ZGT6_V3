#include "net_task.h"
#include "key.h"
#include "led.h"
#include "usart.h"

#include <stdio.h>
#include <string.h>

#include "lwip/api.h"
#include "lwip/netif.h"

extern struct netif gnetif; /* 定义于 LWIP/App/lwip.c（lwip.h 未导出 extern） */

#define NET_SERVER_PORT          5000 /* TCP 监听端口 */
#define NET_RX_LINE_MAX          96   /* 单条命令长度上限（超长截断丢弃） */
#define NET_TX_BUF_MAX           256  /* 单条响应/事件缓冲上限 */
#define NET_RECV_TIMEOUT_MS      100  /* 接收超时：保证按键事件周期性推送 */
#define NET_LWIP_READY_DELAY_MS  100  /* 等待 LwIP 初始化的轮询间隔 */

/* 按键事件名表：索引对应 KeyEvt_t */
static const char *const s_KeyEvtName[] =
{
    [KEY_EVENT_NONE]         = "NONE",
    [KEY_EVENT_SHORT_PRESS]  = "SHORT_PRESS",
    [KEY_EVENT_LONG_PRESS]   = "LONG_PRESS",
    [KEY_EVENT_DOUBLE_CLICK] = "DOUBLE_CLICK",
};

/* 发送字符串到客户端 */
static void Net_SendStr(struct netconn *conn, const char *str)
{
    netconn_write(conn, str, strlen(str), NETCONN_COPY);
}

/* 按键事件推送：取空 KeyQueue 中全部事件发往客户端（KeyQueue 无其他消费者，零侵入联动） */
static void Net_KeyEventPush(struct netconn *conn)
{
    KeyMsg_t key_msg;
    char tx_buf[NET_TX_BUF_MAX];
    int len;

    while (osMessageQueueGet(KeyQueueHandle, &key_msg, NULL, 0) == osOK)
    {
        len = snprintf(tx_buf, sizeof(tx_buf), "EVENT KEY%u %s\n",
                       key_msg.key_id,
                       (key_msg.event <= KEY_EVENT_DOUBLE_CLICK) ? s_KeyEvtName[key_msg.event] : "UNKNOWN");
        if (len > 0)
            netconn_write(conn, tx_buf, (size_t)len, NETCONN_COPY);
    }
}

/* status 命令：上报链路/IP/掩码/网关/MAC */
static void Net_CmdStatus(struct netconn *conn)
{
    char tx_buf[NET_TX_BUF_MAX];
    const ip4_addr_t *ip   = ip_2_ip4(&gnetif.ip_addr);
    const ip4_addr_t *mask = ip_2_ip4(&gnetif.netmask);
    const ip4_addr_t *gw   = ip_2_ip4(&gnetif.gw);
    int len;

    len = snprintf(tx_buf, sizeof(tx_buf),
                   "OK link=%s ip=%u.%u.%u.%u mask=%u.%u.%u.%u gw=%u.%u.%u.%u mac=%02X:%02X:%02X:%02X:%02X:%02X\n",
                   netif_is_link_up(&gnetif) ? "up" : "down",
                   ip4_addr1(ip), ip4_addr2(ip), ip4_addr3(ip), ip4_addr4(ip),
                   ip4_addr1(mask), ip4_addr2(mask), ip4_addr3(mask), ip4_addr4(mask),
                   ip4_addr1(gw), ip4_addr2(gw), ip4_addr3(gw), ip4_addr4(gw),
                   gnetif.hwaddr[0], gnetif.hwaddr[1], gnetif.hwaddr[2],
                   gnetif.hwaddr[3], gnetif.hwaddr[4], gnetif.hwaddr[5]);
    if (len > 0)
        netconn_write(conn, tx_buf, (size_t)len, NETCONN_COPY);
}

/* led 命令：组装 LedMsg_t 入 LedQueue，由 Led_Disp 任务消费执行 */
static void Net_CmdLed(struct netconn *conn, const char *args)
{
    int led_id = 0;
    char action[8] = {0};
    LedMsg_t led_msg;

    if (sscanf(args, "%d %7s", &led_id, action) != 2)
    {
        Net_SendStr(conn, "ERR usage: led <1|2|3> <on|off|toggle>\n");
        return;
    }

    if ((led_id < LED_1_ID) || (led_id > LED_3_ID))
    {
        Net_SendStr(conn, "ERR bad led id\n");
        return;
    }

    led_msg.led_id = (uint8_t)led_id;
    if (strcmp(action, "on") == 0)
        led_msg.state = LED_ON;
    else if (strcmp(action, "off") == 0)
        led_msg.state = LED_OFF;
    else if (strcmp(action, "toggle") == 0)
        led_msg.state = LED_TOGGLE;
    else
    {
        Net_SendStr(conn, "ERR bad action\n");
        return;
    }

    if (osMessageQueuePut(LedQueueHandle, &led_msg, 0, 0) == osOK)
        Net_SendStr(conn, "OK\n");
    else
        Net_SendStr(conn, "ERR led queue full\n");
}

/* keymon 命令：开关按键事件推送 */
static void Net_CmdKeymon(struct netconn *conn, const char *args, uint8_t *keymon_on)
{
    if (strncmp(args, "on", 2) == 0)
    {
        *keymon_on = 1U;
        Net_SendStr(conn, "OK keymon=on\n");
    }
    else if (strncmp(args, "off", 3) == 0)
    {
        *keymon_on = 0U;
        Net_SendStr(conn, "OK keymon=off\n");
    }
    else
    {
        Net_SendStr(conn, "ERR usage: keymon <on|off>\n");
    }
}

/* 命令分发：cmd 为已去 \r\n 的整行 */
static void Net_CmdDispatch(struct netconn *conn, char *cmd, uint8_t *keymon_on)
{
    if (cmd[0] == '\0')
        return;
    else if (strncmp(cmd, "led ", 4) == 0)
        Net_CmdLed(conn, cmd + 4);
    else if (strncmp(cmd, "keymon ", 7) == 0)
        Net_CmdKeymon(conn, cmd + 7, keymon_on);
    else if (strncmp(cmd, "echo ", 5) == 0)
    {
        char tx_buf[NET_TX_BUF_MAX];
        int len = snprintf(tx_buf, sizeof(tx_buf), "OK %s\n", cmd + 5);
        if (len > 0)
            netconn_write(conn, tx_buf, (size_t)len, NETCONN_COPY);
    }
    else if (strcmp(cmd, "status") == 0)
        Net_CmdStatus(conn);
    else if (strcmp(cmd, "help") == 0)
        Net_SendStr(conn, "OK cmds: help | status | led <1|2|3> <on|off|toggle> | echo <text> | keymon <on|off>\n");
    else
        Net_SendStr(conn, "ERR unknown cmd, try 'help'\n");
}

/* 单客户端服务循环：收命令 + 周期推送按键事件，对端断开返回 */
static void Net_ClientServe(struct netconn *conn)
{
    struct netbuf *inbuf = NULL;
    void *data = NULL;
    uint16_t data_len = 0;
    char rx_line[NET_RX_LINE_MAX];
    uint16_t rx_len = 0;
    uint8_t keymon_on = 1U;
    err_t recv_err;

    netconn_set_recvtimeout(conn, NET_RECV_TIMEOUT_MS);

    for (;;)
    {
        if (keymon_on != 0U)
            Net_KeyEventPush(conn);

        recv_err = netconn_recv(conn, &inbuf);
        if (recv_err == ERR_TIMEOUT)
            continue; /* 超时仅用于周期推送 */
        if (recv_err != ERR_OK)
            break;    /* 对端关闭或出错，退出服务 */

        do
        {
            netbuf_data(inbuf, &data, &data_len);
            uint8_t *p = (uint8_t *)data;
            for (uint16_t i = 0; i < data_len; i++)
            {
                if (p[i] == '\n')
                {
                    rx_line[rx_len] = '\0';
                    if ((rx_len > 0) && (rx_line[rx_len - 1] == '\r'))
                        rx_line[rx_len - 1] = '\0'; /* 兼容 \r\n 行尾 */
                    Net_CmdDispatch(conn, rx_line, &keymon_on);
                    rx_len = 0;
                }
                else if (rx_len < (NET_RX_LINE_MAX - 1))
                {
                    rx_line[rx_len++] = (char)p[i];
                }
                /* 超出行缓冲上限的字节丢弃，直至换行 */
            }
        } while (netbuf_next(inbuf) >= 0);

        netbuf_delete(inbuf);
    }
}

/* 以太网 TCP 服务器任务入口 */
void Net_Task(void *argument)
{
    (void)argument;

    struct netconn *listen_conn = NULL;
    struct netconn *client_conn = NULL;
    ip_addr_t client_addr;
    uint16_t client_port = 0;
    const ip4_addr_t *cip;

    /* 等待 defaultTask 完成 MX_LWIP_Init：netif_default 就绪即 tcpip 线程可用，
       否则 netconn API 会因内部 mbox 未建而断言 */
    while (netif_default == NULL)
        osDelay(NET_LWIP_READY_DELAY_MS);

    listen_conn = netconn_new(NETCONN_TCP);
    if ((listen_conn == NULL) ||
        (netconn_bind(listen_conn, IP_ADDR_ANY, NET_SERVER_PORT) != ERR_OK) ||
        (netconn_listen(listen_conn) != ERR_OK))
    {
        Uart_Printf("[NET] server init failed\n");
        osThreadTerminate(osThreadGetId());
    }

    Uart_Printf("[NET] TCP server listening, port %d\n", NET_SERVER_PORT);

    for (;;)
    {
        if (netconn_accept(listen_conn, &client_conn) != ERR_OK)
            continue;

        /* 单客户端服务：接受新连接即进入服务循环，断开后再 accept 下一个 */
        netconn_getaddr(client_conn, &client_addr, &client_port, 0);
        cip = ip_2_ip4(&client_addr);
        Uart_Printf("[NET] client %u.%u.%u.%u:%u connected\n",
                    ip4_addr1(cip), ip4_addr2(cip), ip4_addr3(cip), ip4_addr4(cip), client_port);

        Net_SendStr(client_conn, "F407 LwIP demo ready, try 'help'\n");
        Net_ClientServe(client_conn);

        netconn_close(client_conn);
        netconn_delete(client_conn);
        Uart_Printf("[NET] client disconnected\n");
    }
}
