#include "uart_task.h"
#include "fs_w25q.h"
#include "usart.h"

#include <string.h>

/* ========== 命令解析辅助 ========== */

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

/* ========== 文件系统测试命令（临时测试逻辑：测试前写、测试后删） ========== */

/* 打印 fs 操作结果
 * op: 操作名; name: 目标文件名（可为 NULL）; ret: 文件系统错误码 */
static void Uart_FsRet(const char *op, const char *name, FsW25qErr_t ret)
{
    if (name != NULL)
        Uart_Printf("fs: %s %s -> %d\r\n", op, name, (int)ret);
    else
        Uart_Printf("fs: %s -> %d\r\n", op, (int)ret);
}

/* 列目录（Opendir/Readdir 遍历） */
static void Uart_FsLs(void)
{
    FsW25qDir_t dir;
    FsW25qFilInfo_t info;

    Uart_Printf("== free %lu KB ==\r\n", FsW25q_GetFree() / 1024U);
    if (FsW25q_Opendir(&dir) != FS_W25Q_OK)
        return;

    for (;;)
    {
        if (FsW25q_Readdir(&dir, &info) != FS_W25Q_OK || info.fname[0] == '\0')
            break;
        Uart_Printf("%-15s off=0x%06lX len=%lu crc=0x%08lX\r\n",
                    info.fname, info.offset, info.fsize, info.crc32);
    }
}

/* 创建图案文件：Open(WRITE)+顺序 Write(256B 块)+Close，分阶段报耗时 */
static void Uart_FsMk(const char *name, uint32_t size)
{
    static uint8_t s_Pat[256];
    FsW25qFil_t fil;

    uint32_t t0 = osKernelGetTickCount();
    FsW25qErr_t ret = FsW25q_Open(&fil, name, FS_W25Q_FA_WRITE, size);
    uint32_t t1 = osKernelGetTickCount();
    if (ret != FS_W25Q_OK)
    {
        Uart_FsRet("open", name, ret);
        return;
    }

    uint32_t done = 0;
    while (done < size)
    {
        uint32_t chunk = (size - done > sizeof(s_Pat)) ? sizeof(s_Pat) : (size - done);
        for (uint32_t i = 0; i < chunk; i++)
            s_Pat[i] = (uint8_t)(done + i);

        uint32_t bw;
        ret = FsW25q_Write(&fil, s_Pat, chunk, &bw);
        if (ret != FS_W25Q_OK)
        {
            Uart_FsRet("write", name, ret);
            FsW25q_Close(&fil);
            return;
        }
        done += chunk;
    }

    uint32_t t2 = osKernelGetTickCount();
    ret = FsW25q_Close(&fil);
    uint32_t t3 = osKernelGetTickCount();
    Uart_FsRet("close", name, ret);
    Uart_Printf("mk: %s | open(erase) %lu ms | write %lu ms | close %lu ms\r\n",
                name, t1 - t0, t2 - t1, t3 - t2);
}

/* 校验文件（Verify：整文件读回 + CRC 比对） */
static void Uart_FsChk(const char *name)
{
    uint32_t calc_crc;
    FsW25qErr_t ret = FsW25q_Verify(name, &calc_crc);

    if (ret == FS_W25Q_OK)
        Uart_Printf("fs: %s verify OK\r\n", name);
    else if (ret == FS_W25Q_ERR_MISMATCH)
        Uart_Printf("fs: %s CRC mismatch (calc 0x%08lX)\r\n", name, calc_crc);
    else
        Uart_FsRet("chk", name, ret);
}

/* 读文件内容十六进制dump（Lseek+Read，最多 32 字节） */
static void Uart_FsRd(const char *name, uint32_t pos, uint32_t len)
{
    uint8_t buf[32];
    FsW25qFil_t fil;

    if (len == 0 || len > sizeof(buf))
        len = sizeof(buf);

    FsW25qErr_t ret = FsW25q_Open(&fil, name, FS_W25Q_FA_READ, 0);
    if (ret != FS_W25Q_OK)
    {
        Uart_FsRet("open", name, ret);
        return;
    }

    ret = FsW25q_Lseek(&fil, pos);
    if (ret != FS_W25Q_OK)
    {
        Uart_FsRet("seek", name, ret);
        FsW25q_Close(&fil);
        return;
    }

    uint32_t br;
    ret = FsW25q_Read(&fil, buf, len, &br);
    FsW25q_Close(&fil);
    if (ret != FS_W25Q_OK)
    {
        Uart_FsRet("read", name, ret);
        return;
    }

    Uart_Printf("rd: %s @%lu, %lu bytes:\r\n", name, pos, br);
    for (uint32_t i = 0; i < br; i++)
        Uart_Printf("%02X ", buf[i]);
    Uart_Printf("\r\n");
}

/* 删除全部条目（反复删首项直到目录空） */
static void Uart_FsRmall(void)
{
    FsW25qDir_t dir;
    FsW25qFilInfo_t info;
    uint32_t removed = 0;

    for (;;)
    {
        if (FsW25q_Opendir(&dir) != FS_W25Q_OK)
            break;
        if (FsW25q_Readdir(&dir, &info) != FS_W25Q_OK || info.fname[0] == '\0')
            break;
        if (FsW25q_Remove(info.fname) != FS_W25Q_OK)
            break;
        removed++;
    }

    Uart_Printf("fs: rmall removed %lu\r\n", removed);
}

/* ========== 性能基准 ========== */
#define FS_BENCH_SIZE    262144U       /* 基准文件 256KB */
#define FS_BENCH_CHUNK   1024U         /* 顺序读写块大小 */
#define FS_BENCH_KB      (FS_BENCH_SIZE / 1024U)
#define FS_BENCH_SECTORS (FS_BENCH_SIZE / 4096U)
#define FS_BENCH_RAND_N  1000U         /* 随机读次数（模拟 32B 字模） */
#define FS_BENCH_STAT_N  100000U       /* 元数据查询次数 */

/* 毫秒转吞吐 KB/s（bytes/ms ≈ KiB/s；dt=0 表示快于 tick 分辨率，返回 0） */
static uint32_t Uart_BenchRate(uint32_t bytes, uint32_t dt_ms)
{
    if (dt_ms == 0)
        return 0;
    return bytes / dt_ms;
}

/* 性能基准：覆盖文件系统全部路径，分段报耗时与吞吐
 * 注意：擦除+写入合计约 3~5s，期间 Uart_Proc 阻塞属预期 */
static void Uart_FsBench(void)
{
    static uint8_t s_Buf[FS_BENCH_CHUNK];
    FsW25qFil_t fil;
    FsW25qFilInfo_t info;
    uint32_t t0, t1, done, bw, br;

    for (uint32_t i = 0; i < FS_BENCH_CHUNK; i++)
        s_Buf[i] = (uint8_t)i;

    Uart_Printf("bench: %lu KB, erase %lu sectors\r\n", FS_BENCH_KB, FS_BENCH_SECTORS);

    /* 1. Open(WRITE)：first-fit 分配 + 整块擦除 */
    t0 = osKernelGetTickCount();
    if (FsW25q_Open(&fil, "bench", FS_W25Q_FA_WRITE, FS_BENCH_SIZE) != FS_W25Q_OK)
    {
        Uart_Printf("bench: open fail (busy/no space?)\r\n");
        return;
    }
    t1 = osKernelGetTickCount();
    Uart_Printf("open+erase  : %lu ms | %lu.%03lu ms/sector\r\n",
                t1 - t0, (t1 - t0) / FS_BENCH_SECTORS,
                ((t1 - t0) % FS_BENCH_SECTORS) * 1000U / FS_BENCH_SECTORS);

    /* 2. 顺序写 256KB（含跨页拆分、页编程等待、CRC 累积） */
    done = 0;
    t0 = osKernelGetTickCount();
    while (done < FS_BENCH_SIZE)
    {
        if (FsW25q_Write(&fil, s_Buf, FS_BENCH_CHUNK, &bw) != FS_W25Q_OK)
        {
            Uart_Printf("bench: write fail\r\n");
            FsW25q_Close(&fil);
            return;
        }
        done += bw;
    }
    t1 = osKernelGetTickCount();
    Uart_Printf("seq write   : %lu ms | %lu KB/s\r\n", t1 - t0, Uart_BenchRate(done, t1 - t0));

    /* 3. Close：Sync 目录表落盘（扇区 0 擦+写+CRC） */
    t0 = osKernelGetTickCount();
    if (FsW25q_Close(&fil) != FS_W25Q_OK)
    {
        Uart_Printf("bench: close fail\r\n");
        return;
    }
    t1 = osKernelGetTickCount();
    Uart_Printf("close(sync) : %lu ms\r\n", t1 - t0);

    /* 4. 顺序读 256KB 到 EOF */
    if (FsW25q_Open(&fil, "bench", FS_W25Q_FA_READ, 0) != FS_W25Q_OK)
    {
        Uart_Printf("bench: open rd fail\r\n");
        return;
    }
    t0 = osKernelGetTickCount();
    done = 0;
    while (done < FS_BENCH_SIZE)
    {
        if (FsW25q_Read(&fil, s_Buf, FS_BENCH_CHUNK, &br) != FS_W25Q_OK || br == 0)
            break;
        done += br;
    }
    t1 = osKernelGetTickCount();
    Uart_Printf("seq read    : %lu ms | %lu KB/s (%lu B)\r\n",
                t1 - t0, Uart_BenchRate(done, t1 - t0), done);

    /* 5. 随机读 32B × 1000（模拟字库字模取字：Lseek+Read） */
    uint32_t seed = 12345U;
    t0 = osKernelGetTickCount();
    for (uint32_t i = 0; i < FS_BENCH_RAND_N; i++)
    {
        seed = seed * 1664525U + 1013904223U;
        uint32_t pos = seed % (FS_BENCH_SIZE - 32U);
        if (FsW25q_Lseek(&fil, pos) != FS_W25Q_OK)
            break;
        if (FsW25q_Read(&fil, s_Buf, 32U, &br) != FS_W25Q_OK)
            break;
    }
    t1 = osKernelGetTickCount();
    FsW25q_Close(&fil);
    Uart_Printf("rand rd 32B : %lu ms / %lu rd | %lu us/rd\r\n",
                t1 - t0, FS_BENCH_RAND_N, (t1 - t0) * 1000U / FS_BENCH_RAND_N);

    /* 6. 元数据查询 × 100000（纯 RAM 表遍历，衡量 Open/Stat 查表开销） */
    t0 = osKernelGetTickCount();
    for (uint32_t i = 0; i < FS_BENCH_STAT_N; i++)
    {
        if (FsW25q_Stat("bench", &info) != FS_W25Q_OK)
            break;
    }
    t1 = osKernelGetTickCount();
    Uart_Printf("stat x100k  : %lu ms | ~%lu ns/ea\r\n", t1 - t0, (t1 - t0) * 10U);

    /* 7. Verify 整文件读回 + 位算 CRC32 */
    uint32_t crc;
    t0 = osKernelGetTickCount();
    FsW25qErr_t vret = FsW25q_Verify("bench", &crc);
    t1 = osKernelGetTickCount();
    Uart_Printf("verify+crc  : %lu ms | %lu KB/s | %s crc=0x%08lX\r\n",
                t1 - t0, Uart_BenchRate(FS_BENCH_SIZE, t1 - t0),
                (vret == FS_W25Q_OK) ? "OK" : "BAD", crc);

    Uart_Printf("bench done: 'fs rm bench' to clean\r\n");
}

/* fs 命令入口：自动挂载。子命令：ls | stat <n> | rm <n> | rmall | mk <n> <size> | chk <n> | rd <n> <pos> [len] | bench */
static void Uart_CmdFs(const char *args)
{
    char tok[FS_W25Q_NAME_MAX];
    char num[16];
    uint32_t v1, v2;

    FsW25qErr_t ret = FsW25q_Mount();
    if (ret != FS_W25Q_OK)
    {
        Uart_FsRet("mount", NULL, ret);
        return;
    }

    const char *rest = Uart_NextArg(args, tok, sizeof(tok));
    if (rest == NULL)
    {
        Uart_Printf("usage: fs ls|stat|rm|rmall|mk|chk|rd|bench\r\n");
        return;
    }

    if (strcmp(tok, "ls") == 0)
    {
        Uart_FsLs();
    }
    else if (strcmp(tok, "bench") == 0)
    {
        Uart_FsBench();
    }
    else if (strcmp(tok, "rmall") == 0)
    {
        Uart_FsRmall();
    }
    else if (strcmp(tok, "stat") == 0)
    {
        if (Uart_NextArg(rest, tok, sizeof(tok)) == NULL)
        {
            Uart_Printf("usage: fs stat <name>\r\n");
        }
        else
        {
            FsW25qFilInfo_t info;
            ret = FsW25q_Stat(tok, &info);
            if (ret == FS_W25Q_OK)
                Uart_Printf("stat: %-15s off=0x%06lX len=%lu crc=0x%08lX\r\n",
                            info.fname, info.offset, info.fsize, info.crc32);
            else
                Uart_FsRet("stat", tok, ret);
        }
    }
    else if (strcmp(tok, "rm") == 0)
    {
        if (Uart_NextArg(rest, tok, sizeof(tok)) == NULL)
            Uart_Printf("usage: fs rm <name>\r\n");
        else
            Uart_FsRet("rm", tok, FsW25q_Remove(tok));
    }
    else if (strcmp(tok, "chk") == 0)
    {
        if (Uart_NextArg(rest, tok, sizeof(tok)) == NULL)
            Uart_Printf("usage: fs chk <name>\r\n");
        else
            Uart_FsChk(tok);
    }
    else if (strcmp(tok, "mk") == 0)
    {
        rest = Uart_NextArg(rest, tok, sizeof(tok));
        if (rest == NULL || Uart_NextArg(rest, num, sizeof(num)) == NULL ||
            !Uart_ParseU32(num, &v1) || v1 == 0)
        {
            Uart_Printf("usage: fs mk <name> <size>\r\n");
        }
        else
        {
            Uart_FsMk(tok, v1);
        }
    }
    else if (strcmp(tok, "rd") == 0)
    {
        rest = Uart_NextArg(rest, tok, sizeof(tok));
        if (rest != NULL)
            rest = Uart_NextArg(rest, num, sizeof(num));
        if (rest == NULL || !Uart_ParseU32(num, &v1))
        {
            Uart_Printf("usage: fs rd <name> <pos> [len]\r\n");
        }
        else
        {
            v2 = 32;
            if (Uart_NextArg(rest, num, sizeof(num)) != NULL && !Uart_ParseU32(num, &v2))
                v2 = 32;
            Uart_FsRd(tok, v1, v2);
        }
    }
    else
    {
        Uart_Printf("unknown: fs %s\r\n", tok);
    }
}

/* ========== 命令分发 ========== */

/* 命令分发：结行（尾部 \r 已由调用方剥离）精确匹配，未命中命令镜像回显
 * line: 已终止的命令行 */
static void Uart_CmdDispatch(const char *line)
{
    if (strncmp(line, "fs", 2) == 0 && (line[2] == '\0' || line[2] == ' '))
    {
        Uart_CmdFs(line + 2);
        return;
    }

    Uart_Printf("[Rx] %s\r\n", line);
}

/* RX 任务：等信号量→取帧→按 \n 组行→命令分发（未知命令镜像 [Rx] 回显） */
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
