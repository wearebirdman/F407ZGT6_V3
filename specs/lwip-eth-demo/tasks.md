# 任务清单 — LwIP Demo + 修复落实

1. [x] P0-2：ethernetif.c `INTERFACE_THREAD_STACK_SIZE` 350→1024
2. [x] P1-1：ethernetif.c link 线程加 PHY 速率/双工同步 + PHY 地址 0/1 自动重试
3. [x] P1-2：lwipopts.h `TCPIP_THREAD_STACKSIZE` 1024→2048
4. [x] P1-3：FreeRTOSConfig.h USER CODE 区开两个钩子宏
5. [x] P1-4：freertos.c USER CODE Application 区实现两个钩子（直接 Error_Handler）
6. [x] 新建 APP/net/net_task.h / net_task.c（TCP 服务器 + 命令解析 + 按键推送）
7. [x] freertos.c：Includes 加 net_task.h，Variables 加任务属性，RTOS_THREADS 创建 Net_Task
8. [x] Makefile：加源文件与 -IAPP/net
9. [x] .eide/eide.yml：srcDirs 加 LWIP（APP/net 随 APP 递归包含），incList 补齐 LwIP 路径
10. [x] 新建 tools/pc_client/net_client.py 上位机
11. [x] 构建验证：EIDE 工具链 arm-none-eabi-gcc -fsyntax-only -Wall 检查 net_task.c / ethernetif.c / freertos.c / lwip.c 全部通过；期间发现并修复 LWIP_SO_RCVTIMEO 未开导致 netconn_set_recvtimeout 不可用（已在 lwipopts.h USER CODE 区开启）
12. [x] 输出总结（改动清单 / 烧录测试步骤 / CubeMX 重生成注意）
