# 感受 Dovetail + Xenomai co-kernel (2026-05-09)

## 热身：重新连上去

```bash
~/work/2026/xenomai/qemu-test.sh ~/work/2026/xenomai/linux-dovetail/arch/x86/boot/bzImage
ssh -p 10003 root@localhost
# 如果网络不通: ip link set enp0s3 up && dhclient enp0s3
```

## 实验 1 — 感受优先级墙：Linux 任务 vs Xenomai RT 任务

写两个程序，同时跑：

### 1a. 先写一个 Linux 轰炸机

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

### 1b. 再写一个 Xenomai RT 任务

```bash
# rt-sec.c — 每秒打印一行，用 Xenomai 时钟
cat > /tmp/rt-sec.c << 'EOF'
#include <alchemy/task.h>
#include <alchemy/timer.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

RT_TASK task;
int stop = 0;

void handler(int sig) { stop = 1; }

void demo(void *arg) {
    RTIME now;
    int count = 0;
    while (!stop) {
        rt_task_sleep(1000000000ULL);  // 1 秒 (纳秒)
        now = rt_timer_read();
        rt_printf("[%d] RT task tick, time=%llu ns\n", ++count, now);
    }
}

int main() {
    signal(SIGINT, handler);
    rt_task_create(&task, "demo", 0, 99, T_JOINABLE);
    rt_task_start(&task, &demo, NULL);
    pause();
    rt_task_join(&task);
    return 0;
}
EOF
gcc /tmp/rt-sec.c -o /tmp/rt-sec -I/usr/xenomai/include \
  -L/usr/xenomai/lib -lalchemy -lxenomai -lpthread -lrt
```

### 1c. 实验过程

```bash
# 终端 1: 先让 RT 任务跑起来
export LD_LIBRARY_PATH=/usr/xenomai/lib
/tmp/rt-sec
# 你应该看到每秒一行:
# [1] RT task tick, time=XXXX ns
# [2] RT task tick, time=XXXX ns
# ...

# 终端 2: SSH 再开一个，启动轰炸机
ssh -p 10003 root@localhost
# 看 CPU 核数
nproc
# 启动 N+1 个轰炸机占满所有核
for i in $(seq 1 5); do /tmp/bomb & done
```

**观察**：终端 1 里的 RT task 还在准时每秒打印吗？这就是 dovetail 的核心能力 — Xenomai RT 任务不受 Linux 负载影响，即使 Linux CPU 100% 满载。

Ctrl+C 停止 RT task，然后 `killall bomb`。

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
你的 RT task (/tmp/rt-sec)
    优先级: 99 (Xenomai 最高)
    运行在: oob 阶段
           ↑
    ─────── pipeline 界线 ───────
           ↓
    /tmp/bomb (Linux 死循环)
    优先级: 0-139 (Linux nice)
    运行在: in-band 阶段
    状态: CPU 100%，但对上面毫无影响
```

这就是 dovetail 存在的意义：**oob 域和 in-band 域完全隔离**。Linux 看到自己"满载"，Xenomai RT 任务不受影响。

## 如果还想继续深入

可以重建内核，开启 irqptorture：

```bash
cd ~/work/2026/xenomai/linux-dovetail
./scripts/config --enable CONFIG_IRQ_PIPELINE_TORTURE_TEST
make -j$(nproc)
# 然后用新 bzImage 启动，modprobe irqptorture
```

`irqptorture` 会注入虚拟 oob 中断，你可以在 `/proc/interrupts` 和 `/proc/xenomai/sched` 同时看到中断流过 pipeline。
