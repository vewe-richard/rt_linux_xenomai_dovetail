# 感受 Dovetail + Xenomai co-kernel (2026-05-09)

## 热身：重新连上去

```bash
~/work/2026/xenomai/qemu-test.sh ~/work/2026/xenomai/linux-dovetail/arch/x86/boot/bzImage
ssh -p 10003 root@localhost
# 如果网络不通: ip link set enp0s3 up && dhclient enp0s3
```

## ⚠️ 重要发现 (2026-05-11)

**Alchemy API 不可用** (copperplate 初始化 bug): `rt_task_create()` 在 libcopperplate
初始化时 segfault，原因追踪到 `--enable-pshared` 导致的共享堆初始化路径有 NULL 指针
访问 (`add_free_range` 函数)。即使去掉 `--enable-pshared`，copperplate init 仍然崩溃。

**解决方案**: 使用 **Cobalt 原生扩展 POSIX API**（`pthread_create_ex()` + `clock_nanosleep()`）。
`/usr/xenomai/bin/latency` 用的就是这条路。Cobalt 原生 API 绕过 copperplate 和 alchemy，
直接与 libcobalt 通信。

参考 API:
- 线程创建: `pthread_attr_init_ex()` / `pthread_create_ex()`
- 睡眠: `clock_nanosleep()` (Cobalt 自动拦截)
- 主模式切换: `cobalt_thread_harden()`
- 初始化: `xenomai_init(&argc, &argv)`

## 实验 1 — 感受优先级墙：Linux 任务 vs Xenomai RT 任务

### 1a. Linux 轰炸机

```bash
# bomb.c — 纯 Linux 死循环，占满 CPU
cat > /tmp/bomb.c << 'EOF'
#include <stdio.h>
int main() {
    volatile unsigned long i = 0;
    while (1) { i++; }
}
EOF
gcc /tmp/bomb.c -o /tmp/bomb
```

### 1b. Xenomai RT 任务 (Cobalt 原生 API, timerfd 方式)

**重要**: 必须用 `timerfd` 做周期性唤醒（跟 `latency` 一样），不要用 `clock_nanosleep`。
后者在 Cobalt oob 模式下配合 printf 的模式切换可能导致线程永久阻塞。

```bash
# rt-demo.c — 每秒打印一行，用 timerfd 做周期性唤醒
cat > /tmp/rt-demo.c << 'EOF'
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/timerfd.h>
#include <xenomai/init.h>
#include <cobalt/sys/cobalt.h>

static volatile int stop = 0;
static void handle_sig(int sig) { stop = 1; }

static void *rt_thread(void *arg)
{
    struct timespec now;
    struct itimerspec it;
    int tfd, count = 0;
    unsigned long long overruns;

    cobalt_thread_harden();  /* oob 主模式 */

    /* 创建单调 timerfd */
    tfd = timerfd_create(CLOCK_MONOTONIC, 0);

    /* 第一次 1 秒后触发，之后每 1 秒 */
    clock_gettime(CLOCK_MONOTONIC, &now);
    it.it_value.tv_sec  = now.tv_sec + 1;
    it.it_value.tv_nsec = now.tv_nsec;
    it.it_interval.tv_sec  = 1;
    it.it_interval.tv_nsec = 0;
    timerfd_settime(tfd, TFD_TIMER_ABSTIME, &it, NULL);

    while (!stop) {
        /* 阻塞直到 timer 触发 */
        if (read(tfd, &overruns, sizeof(overruns)) != sizeof(overruns))
            break;

        clock_gettime(CLOCK_MONOTONIC, &now);
        count++;
        printf("[%d] RT tick, time=%lld.%09ld%s\n",
               count, (long long)now.tv_sec, now.tv_nsec,
               overruns ? " OVERRUN" : "");
        fflush(stdout);
    }

    close(tfd);
    return NULL;
}

int main(int argc, char *argv[])
{
    pthread_attr_ex_t attr_ex;
    struct sched_param_ex param_ex;
    pthread_t thread;

    xenomai_init(&argc, &argv);
    printf("Xenomai Cobalt initialized\n");

    pthread_attr_init_ex(&attr_ex);
    pthread_attr_setinheritsched_ex(&attr_ex, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy_ex(&attr_ex, SCHED_FIFO);
    param_ex.sched_priority = 80;
    pthread_attr_setschedparam_ex(&attr_ex, &param_ex);

    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);

    pthread_create_ex(&thread, &attr_ex, rt_thread, NULL);
    pthread_attr_destroy_ex(&attr_ex);

    printf("RT thread created, Ctrl-C to stop\n");
    pthread_join(thread, NULL);
    printf("Done.\n");
    return 0;
}
EOF
gcc /tmp/rt-demo.c -o /tmp/rt-demo \
  -I/usr/xenomai/include/cobalt -I/usr/xenomai/include \
  -L/usr/xenomai/lib -Wl,-rpath,/usr/xenomai/lib \
  -lcobalt -lpthread -lrt
```

**编译库说明**: 只有 `-lcobalt -lpthread -lrt`，不需要 `-lalchemy -lcopperplate`。

### 1c. RT + 负载隔离测试 (完整版)

```bash
# rt-load.c — RT 100ms 周期任务 + 全核 Linux CPU 烧机
# 代码在 /root/xenomai/rt-load.c，编译:
cd /root/xenomai && ./build.sh
```

### 1d. 实验过程

```bash
# 终端 1: 跑 RT load 测试 (RT periodic + CPU burners)
/root/xenomai/rt-load
# 应该看到:
#   RT task started (CPU0, prio=80, period=100ms)
#   Starting 4 CPU burners...
#     CPU 0: reserved for RT
#     CPU 1: burner started
#     CPU 2: burner started
#     CPU 3: burner started
#   Running... Press Ctrl-C to stop
#   [RT] count=10, last interval=100000123 ns (target 100000000 ns)
#   [RT] count=20, last interval=100000042 ns (target 100000000 ns)

# 终端 2: 同時看延迟
ssh -p 10003 root@localhost
cat /proc/xenomai/latency
# 在满载下 latency 应该仍然稳定
```

**观察**：RT 任务的 interval 始终接近 100ms，不受 Linux CPU 100% 影响。
误差在 ~100ns 级别（虚拟化环境）。这就是 dovetail 的核心能力 — oob 域和 in-band
域完全隔离。Linux 看到自己"满载"，Xenomai RT 任务不受影响。

Ctrl+C 停止。

## 实验 2 — 看 /proc/xenomai/ 里有什么

RT task 跑着的时候，观察 Xenomai 内部：

```bash
# 1. 调度统计
cat /proc/xenomai/sched
# → 看到你的 "demo" 任务了吗？

# 2. 时钟
cat /proc/xenomai/clock/coreclk
# → gravity= 是 timer 校准值 (ns)

# 3. 中断统计（对照普通 /proc/interrupts）
cat /proc/xenomai/faults
# → 实时任务缺页异常计数

# 4. 延迟（后台一直在记录）
cat /proc/xenomai/latency
# → 这是系统启动以来核内定时器的最大抖动 (ns)
```

## 实验 3 — stress-ng 压力下测 latency

```bash
# 终端 1: 跑 latency
export LD_LIBRARY_PATH=/usr/xenomai/lib
/usr/xenomai/bin/latency -T 60   # 跑 60 秒

# 终端 2: 装 stress-ng 然后轰炸
apt-get update && apt-get install -y stress-ng
stress-ng --cpu 0 --io 2 --vm 2 --timeout 60s

# 终端 3 (可选): 同时看中断对比
watch -n1 'cat /proc/interrupts | head -20'
```

**观察 latency 输出的 max 列** — 在 Linux 满载 + IO 压力下，RT latency 能稳定在多少？在 KVM 虚拟化下通常 ~100-400µs，真机上应该 <20µs。

## 实验 4 — 看看 xenomai 自带的 demos

```bash
# xenomai/demo/ 里有很多示例，可以编译了玩
cd ~/work/2026/xenomai/xenomai/demo
ls
# posix/  — POSIX skin 示例
# alchemy/ — Alchemy skin 示例（我们上面的实验用的就是 Alchemy）
```

## 核心认知回顾

```
你的 RT task (rt-load / rt-demo)
    优先级: 80 (SCHED_FIFO, Xenomai oob)
    运行在: oob 阶段 (cobalt_thread_harden 后)
    API: pthread_create_ex() + clock_nanosleep()
           ↑
    ─────── pipeline 界线 ───────
           ↓
    /tmp/bomb (Linux 死循环)
    优先级: 0-139 (Linux nice)
    运行在: in-band 阶段
    状态: CPU 100%，但对上面毫无影响
```

这就是 dovetail 存在的意义：**oob 域和 in-band 域完全隔离**。Linux 看到自己"满载"，Xenomai RT 任务不受影响。

## API 选择备忘

| API 层 | 头文件 | 线程创建 | 睡眠 | 状态 |
|--------|--------|----------|------|------|
| **Cobalt 扩展 POSIX** | `<cobalt/sys/cobalt.h>` (pthread.h/time.h 自动拦截) | `pthread_create_ex()` | `timerfd_create()` + `timerfd_settime(TFD_TIMER_ABSTIME)` + `read()` | ✅ 可用 (latency 同款) |
| **Alchemy** | `<alchemy/task.h>` | `rt_task_create()` | `rt_task_sleep()` | ❌ copperplate init bug |
| **Cobalt 裸 API** | `<xenomai/init.h>` | 手动 syscall | 手动 syscall | ✅ 可用但不建议 |

### 编译需要的库 (Cobalt 原生)
```
-lcobalt -lpthread -lrt
```
NOT: `-lalchemy -lcopperplate` (这些会触发 copperplate init bug)

## 如果还想继续深入

可以重建内核，开启 irqptorture：

```bash
cd ~/work/2026/xenomai/linux-dovetail
./scripts/config --enable CONFIG_IRQ_PIPELINE_TORTURE_TEST
make -j$(nproc)
# 然后用新 bzImage 启动，modprobe irqptorture
```

`irqptorture` 会注入虚拟 oob 中断，你可以在 `/proc/interrupts` 和 `/proc/xenomai/sched` 同时看到中断流过 pipeline。
