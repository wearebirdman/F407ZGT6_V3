# -*- coding: utf-8 -*-
"""
F407ZGT6_V3 LwIP demo PC 上位机（纯标准库，Python 3.8+）

用法：
    python net_client.py                      # 连接默认 192.168.1.10:5000
    python net_client.py -i 192.168.1.10 -p 5000
    python net_client.py -c "status"          # 非交互：发一条命令打印响应后退出

交互模式直接输入板端命令：
    help | status | led <1|2|3> <on|off|toggle> | echo <text> | keymon <on|off>
板端主动推送的按键事件（EVENT KEY...）由接收线程实时打印。
输入 quit / exit 或 Ctrl+C 退出。

接线与网络：
    板子静态 IP 192.168.1.10/24，网关 192.168.1.1（CubeMX LwIP 配置）。
    PC 网卡需配同网段地址（如 192.168.1.100/24），或板子与 PC 同接一台路由器/交换机。
"""

import argparse
import socket
import sys
import threading

DEFAULT_IP = "192.168.1.10"
DEFAULT_PORT = 5000
RECV_BUF = 1024


def rx_loop(sock: socket.socket) -> None:
    """后台接收线程：打印板端响应与 EVENT 推送；连接断开时结束进程。"""
    try:
        while True:
            data = sock.recv(RECV_BUF)
            if not data:
                print("\n[*] 板端关闭连接")
                break
            # 板端按行输出，直接解码打印（容错处理半包中文不适用，本协议纯 ASCII）
            sys.stdout.write(data.decode("ascii", errors="replace"))
            sys.stdout.flush()
    except OSError:
        pass
    finally:
        # 接收结束意味着连接不可用，退出整个进程
        import os
        os._exit(0)


def main() -> int:
    parser = argparse.ArgumentParser(description="F407 LwIP demo TCP client")
    parser.add_argument("-i", "--ip", default=DEFAULT_IP, help=f"板子 IP（默认 {DEFAULT_IP}）")
    parser.add_argument("-p", "--port", type=int, default=DEFAULT_PORT, help=f"端口（默认 {DEFAULT_PORT}）")
    parser.add_argument("-c", "--cmd", help="非交互模式：发送单条命令后退出")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5.0)
    try:
        sock.connect((args.ip, args.port))
    except OSError as e:
        print(f"[!] 连接 {args.ip}:{args.port} 失败：{e}")
        print("    请确认：1) 板子已上电且网线已插 2) PC 与板子同网段 3) ping 板子 IP 可通")
        return 1
    sock.settimeout(None)
    print(f"[*] 已连接 {args.ip}:{args.port}")

    t = threading.Thread(target=rx_loop, args=(sock,), daemon=True)
    t.start()

    if args.cmd:
        sock.sendall((args.cmd + "\n").encode("ascii"))
        try:
            t.join(timeout=2.0)  # 等待响应打印
        except RuntimeError:
            pass
        sock.close()
        return 0

    try:
        while True:
            line = input()
            if line.strip().lower() in ("quit", "exit"):
                break
            if not line.strip():
                continue
            sock.sendall((line + "\n").encode("ascii"))
    except (EOFError, KeyboardInterrupt):
        pass
    except OSError as e:
        print(f"\n[!] 发送失败：{e}")
    finally:
        sock.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
