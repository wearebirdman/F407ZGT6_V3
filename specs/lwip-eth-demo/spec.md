# LwIP 以太网 Demo + 审阅修复落实 — 规格说明

日期：2026-11-02 ｜ 工程：F407ZGT6_V3（GCC + EIDE + CubeMX，FreeRTOS + LwIP 2.1.2）

## 1. 目标

1. 以**纯代码方式**落实上轮 LwIP 审阅的修复项（用户明确：不动 CubeMX 配置）。
2. 新增 LwIP 以太网 demo：netconn TCP 服务器，与板载 LED / 按键 / 串口联动。
3. 工程内建 `tools/pc_client/` 子文件夹，提供 Python 上位机。

## 2. 修复项（P0/P1）

| 编号 | 文件 | 修改 |
|---|---|---|
| P0-1 | `.eide/eide.yml` | srcDirs 增加 `LWIP`、`APP/net`；incList 补齐 LwIP 全部头文件路径（照抄 Makefile C_INCLUDES 中 LwIP 相关项） |
| P0-2 | `LWIP/Target/ethernetif.c`（USER CODE 区） | `INTERFACE_THREAD_STACK_SIZE` 350 → 1024 |
| P1-1 | `LWIP/Target/ethernetif.c` link 线程 USER CODE 区 | link up 时读 PHY SCSR(0x1F) 解析速率/双工，`HAL_ETH_GetMACConfig/SetMACConfig` 同步 MAC；MDIO 读失败时 PHY 地址 0/1 自动切换重试 |
| P1-2 | `LWIP/Target/lwipopts.h` | `TCPIP_THREAD_STACKSIZE` 1024 → 2048（CubeMX 参数区，改后需提示用户 CubeMX 同步） |
| P1-3 | `Core/Inc/FreeRTOSConfig.h` USER CODE Defines 区 | `#define configCHECK_FOR_STACK_OVERFLOW 1`、`#define configUSE_MALLOC_FAILED_HOOK 1` |
| P1-4 | `Core/Src/freertos.c` USER CODE Application 区 | 实现 `vApplicationStackOverflowHook` / `vApplicationMallocFailedHook`，体内直接调 `Error_Handler()`（故障上下文禁止互斥锁打印） |

## 3. Demo 设计

### 3.1 新增文件 `APP/net/net_task.h` / `APP/net/net_task.c`

- 任务入口 `void Net_Task(void *argument)`；在 `freertos.c` USER CODE RTOS_THREADS 区创建（属性结构体放 USER CODE Variables 区，include 放 USER CODE Includes 区），保证 CubeMX 重生成不丢。
- 任务参数：stack 1024 字节、优先级 `osPriorityNormal`。
- 启动同步：先轮询等待 `netif_default != NULL`（即 defaultTask 中 `MX_LWIP_Init()` 完成、tcpip 线程就绪），再调用 netconn API，避免空 mbox 断言。
- `gnetif` 在 net_task.c 内 `extern struct netif gnetif;` 声明使用。

### 3.2 TCP 服务器行为

- netconn API，`NETCONN_TCP`，监听 **5000** 端口；单客户端服务（新连接到来时关闭旧连接）。
- 行命令协议（`\n` 结尾，回显 `OK`/`ERR` 前缀）：
  - `help` → 命令清单
  - `status` → link 状态、IP/掩码/网关、MAC
  - `led <1|2|3> <on|off|toggle>` → 组装 `LedMsg_t` 入 `LedQueueHandle`，由 Led_Disp 执行（复用既有联动链路）
  - `echo <text>` → 原样返回
  - `keymon <on|off>` → 开关按键事件推送
- 按键联动：客户端连接且 keymon 开时，Net_Task 以 0 超时轮询消费 `KeyQueueHandle`（当前无其他消费者，零侵入），向客户端推送 `EVENT KEY<n> <SHORT_PRESS|LONG_PRESS|DOUBLE_CLICK>`。
- 串口联动：客户端连接/断开/命令执行通过 `Uart_Printf` 输出（自带互斥锁）。
- LCD 不直接操作（LVGL 无 OS 锁，跨线程调用有风险），demo 联动范围 = NET↔LED↔KEY↔UART。

### 3.3 上位机 `tools/pc_client/net_client.py`

- 纯标准库 Python 3（socket + threading + argparse），无需安装依赖。
- 功能：连接 `192.168.1.10:5000`（可用参数覆盖），交互式发送命令；后台线程接收并打印响应与 `EVENT` 推送。
- 附 `tools/pc_client/README` 说明（并入 .py 头部注释，不单独建 md）。

## 4. 构建配置同步

- `Makefile`（根目录，CubeMX 生成）：C_SOURCES 增加 `APP/net/net_task.c`，C_INCLUDES 增加 `-IAPP/net`。
- `.eide/eide.yml`：同 P0-1。

## 5. 验证

1. `make -j`（若 arm-none-eabi-gcc 在 PATH）；不可用则静态自查。
2. 编译错误逐一修复至通过。
3. 烧录测试步骤写入最终总结（PC 设 192.168.1.x/24，ping 192.168.1.10，python net_client.py）。

## 6. CubeMX 重生成注意事项（写入总结）

- lwipopts.h 的 TCPIP_THREAD_STACKSIZE、FreeRTOSConfig.h 两个钩子宏：属 CubeMX 参数区，重生成会覆盖 → 日后在 CubeMX GUI 同步设置。
- ethernetif.c / freertos.c 的修改全部位于 USER CODE 区，重生成安全。

## 7. 不做的事

- 不改 CubeMX .ioc 配置。
- 不动 LVGL/LCD 显示内容。
- 不引入 DHCP、多客户端、UDP。
