# RTDM Demo 详解 (2026-05-13)

## 架构 — 数据流图

```
用户态 (oob primary mode)
┌─────────────────────────────────────────────────────┐
│  demo-rtdm-app                                      │
│                                                     │
│  cobalt_thread_harden()  ← 进入 oob 主模式          │
│  read(fd, &count, 8)     ← blocking read            │
│        │                                            │
│        │  libcobalt 拦截 → __cobalt_read()           │
│        │      │                                      │
└────────┼──────┼──────────────────────────────────────┘
         │      │
         │  ┌───┴────────────┐
         │  │ rtdm_event_wait│  ← 阻塞在 oob 域，不经过 Linux 调度器
         │  │ (&irq_event)   │
         │  └───┬────────────┘
         │      │ 被 pulse 唤醒
         │      ▼
         │  rtdm_safe_copy_to_user() → 返回 irq_count 到用户态
         │
═════════│═══════════════════════════════════════════════ IRQ Pipeline
         │
内核态   │
┌────────┼──────────────────────────────────────────────┐
│        │          demo-rtdm-irq.ko                    │
│        │                                              │
│   ┌────┴──────────────┐                               │
│   │  timer_callback() │  ← 10 Hz 定时器（模拟 IRQ）   │
│   │    irq_count++    │                               │
│   │    rtdm_event_    │                               │
│   │      pulse(event) │──→ 唤醒所有 wait 者            │
│   │    mod_timer()    │    重设定时器                  │
│   └───────────────────┘                               │
└──────────────────────────────────────────────────────┘
```

## 核心概念：三个 Xenomai RTDM 原语

### 1. `rtdm_event_t` — oob 域的信号量/事件

这就是整个 demo 的核心。它不是一个普通 Linux completion 或 waitqueue，而是 **在 oob 域里工作的等待/通知机制**。

```
线程 A (oob)                        ISR/定时器 (oob 或 in-band)
─────────────                       ──────────────────────────
rtdm_event_wait(&ev)  ──阻塞──→    rtdm_event_pulse(&ev)
  ↑                                   │
  └──── 直接唤醒，不经过调度器 ←────────┘
```

关键：`rtdm_event_pulse()` 会唤醒**所有**在这个 event 上等待的线程。对应的 `rtdm_event_signal()` 只唤醒一个。

### 2. `struct rtdm_driver` + `struct rtdm_device` — RTDM 设备注册

Xenomai 3.3.x 里，一个 RTDM 设备需要两个结构体：

```c
// rtdm_driver: 描述"怎么操作这个设备"
struct rtdm_driver demo_driver = {
    .device_flags = RTDM_NAMED_DEVICE | RTDM_EXCLUSIVE,  // 命名设备 + 独占访问
    .device_count = 1,                                    // 最多 1 个实例
    .ops = {
        .open    = demo_open,      // 用户 open() 时调用
        .close   = demo_close,     // 用户 close() 时调用
        .read_rt = demo_read_rt,   // oob 域 read()  —— 阻塞等待中断
        .read_nrt = demo_read_rt,  // nrt 域 read()  —— 同 oob 域
        .ioctl_rt = demo_ioctl_rt, // oob 域 ioctl()
    },
};

// rtdm_device: 描述"这个设备叫什么，挂到哪个 driver"
struct rtdm_device dev = {
    .driver = &demo_driver,
    .label  = "demo-irq",           // → 产生 /dev/rtdm/demo-irq
};
```

标志位含义：
- `RTDM_NAMED_DEVICE` — 在 `/dev/rtdm/` 创建设备节点
- `RTDM_EXCLUSIVE` — 同时只能有一个 fd 打开

### 3. `rtdm_safe_copy_to_user()` — oob 安全的 copy_to_user

普通 `copy_to_user()` 可能触发 in-band 缺页（page fault），这在 oob 域是致命的（会破坏实时性）。`rtdm_safe_copy_to_user()` 保证 oob 安全。

## 完整调用流程

```
1. insmod demo-rtdm-irq.ko
   └─ module_init(demo_init)
        ├─ rtdm_event_init(&ctx.irq_event, 0)     ← 初始化事件
        └─ rtdm_dev_register(&dev)                 ← 注册设备 → /dev/rtdm/demo-irq

2. ./demo-rtdm-app
   └─ main()
        ├─ xenomai_init()           ← 初始化 libcobalt
        ├─ open("/dev/rtdm/demo-irq")
        │    └─ demo_open() (内核)
        │         ├─ rtdm_event_clear(&ctx.irq_event)
        │         ├─ irq_count = 0
        │         └─ 启动 10Hz 定时器
        ├─ cobalt_thread_harden()  ← 线程进入 oob 主模式
        └─ loop:
             └─ read(fd, buf, 8)
                  └─ demo_read_rt() (内核, oob 域)
                       ├─ rtdm_event_wait(&ctx.irq_event)  ← 阻塞
                       │    ║ 定时器触发
                       │    ║ timer_callback()
                       │    ║   irq_count++
                       │    ║   rtdm_event_pulse(&irq_event)  ← 唤醒!
                       │    ║
                       ├─ 被唤醒，继续执行
                       └─ rtdm_safe_copy_to_user(buf, &irq_count)
                            └─ 用户态拿到 irq_count，打印 interval
```

## 如果换成真实硬件 IRQ

用 `xnintr` 结构体替代 `timer_list`：

```c
// 替换方案（demo 里用的是 timer，真实硬件用 xnintr）
struct demo_context {
    rtdm_event_t      irq_event;
    unsigned long     irq_count;
    struct xnintr     intr;         // ← Cobalt 中断对象
    unsigned int      irq_num;      // ← 硬件 IRQ 号
};

// ISR 在 oob 域执行，硬件中断直接进 pipeline → Cobalt → 你的 ISR
static int my_isr(struct xnintr *intr)
{
    struct demo_context *c = intr->cookie;
    c->irq_count++;
    rtdm_event_pulse(&c->irq_event);   // 唤醒用户态
    return XN_IRQ_HANDLED;             // "这个中断我处理了"
}

// init 时
xnintr_init(&ctx.intr, "my-irq", irq_num, my_isr, NULL, 0);
xnintr_attach(&ctx.intr, &ctx, &IRQ_AFFINITY_MASK);
//                                           ↑
//                     libcpup 生成的 CPU 亲和性掩码
```

真实硬件的 IRQ 流：

```
硬件 GPIO/IRQ 引脚
      │
      ▼
GIC (中断控制器)
      │
      ▼
IRQ Pipeline ─┬─→ oob (Cobalt xnintr → my_isr → rtdm_event_pulse)
              │       ↑ 你的 RTDM 驱动在这里
              └─→ in-band (Linux 看到的 /proc/interrupts)
```

## VM 里的实际输出

```
[1]  IRQ count=1   interval=103.511521 ms   ← 第一次中断，~104ms
[2]  IRQ count=2   interval=103.980464 ms   ← 稳定 104ms
...
[50] IRQ count=50  interval=104.044800 ms   ← 全部在 ~104ms ± 0.5ms
```

interval 是 104ms 而不是 100ms，因为：
- 定时器用 `jiffies + HZ/10`，HZ=250 → jiffies 精度是 4ms
- QEMU KVM 时钟虚拟化额外的抖动
- 如果在真机上用硬件 IRQ，精度会好得多

## 三点关键认知

1. **`read()` 阻塞在 oob 域** — 线程调用了 `cobalt_thread_harden()`，之后所有系统调用（包括 `read`）被 libcobalt 拦截，走 Cobalt 路径，不经过 Linux 调度器。

2. **`rtdm_event_pulse()` 是 oob→oob 直接唤醒** — 没有 Linux 调度器参与，所以即使 Linux 满载，这个唤醒延迟也不受影响。

3. **RTDM 是 Xenomai 驱动模型的基础** — 所有 Xenomai 下的实时驱动（CAN, GPIO, UART, SPI）都是这个模式：`xnintr` 处理硬件中断 → `rtdm_event` 通知用户态 → `read_rt`/`write_rt`/`ioctl_rt` 做数据交换。

---

# read() 路径验证 (2026-05-13)

## libcobalt 如何拦截 read()

```
你的代码:    read(fd, buf, len)
                │
                ▼  -Wl,--wrap=read 重定向 (编译时从 Makefile 传入)
         __wrap_read(fd, buf, len)    ← libcobalt.so 提供
                │
         ┌──────┴──────┐
         │ 线程在 oob?  │  (cobalt_thread_harden() 之后)
         └──────┬──────┘
         是 /        \ 否
          ▼            ▼
   Cobalt 内核路径   __real_read()
   (rtdm_fd_read)    (glibc → Linux sys_read → VFS)
```

libcobalt.so 符号表验证：
```
__wrap_read       ← 链接器把 read() 调到这里
__real_read       ← 调真正 glibc read() → Linux sys_read
read@GLIBC_2.2.5  ← 从 glibc 导入的原始 read (UND)
```

## 切换方式

| 操作 | 线程模式 | read() 走向 |
|------|---------|-------------|
| `cobalt_thread_harden()` | oob primary | Cobalt 内核 `rtdm_fd_read()` |
| `cobalt_thread_relax()` | nrt secondary | Linux `vfs_read()` |
| 从不调用 harden | 始终 secondary | Linux `vfs_read()` |

## 为什么不能oob走VFS

VFS 内部有：
1. **可能睡眠的锁**（mutex, rwsem）— oob 域休眠破坏实时性甚至死锁
2. **page fault** — 缺页处理走 Linux mm，会把线程切到 in-band
3. **调度决策** — 阻塞时由 Linux 调度器选线程，oob 域不该被 Linux 调度

## 内核代码修改 (验证用)

### 位置 1: Linux VFS 路径
`linux-dovetail/fs/read_write.c:453`

```c
ssize_t vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
    /* Xenomai hands-on: trace RTDM device access via Linux VFS */
    if (file->f_path.dentry &&
        strcmp(file->f_path.dentry->d_name.name, "demo-irq") == 0)
        printk(KERN_WARNING ">>> VFS_READ: demo-irq pid=%d comm=%s\n",
               current->pid, current->comm);
    // ... 原有逻辑
}
```

### 位置 2: Cobalt RTDM 路径
`xenomai/kernel/cobalt/rtdm/fd.c:558`

```c
ssize_t rtdm_fd_read(int ufd, void __user *buf, size_t size)
{
    // ...
    /* Xenomai hands-on: trace Cobalt RTDM read path */
    printk(KERN_WARNING ">>> COBALT_RTDM_READ: ufd=%d comm=%s pid=%d\n",
           ufd, current->comm, current->pid);

    if (is_secondary_domain())
        ret = fd->ops->read_nrt(fd, buf, size);
    else
        ret = fd->ops->read_rt(fd, buf, size);
    // ...
}
```

### 验证方法

```bash
# Cobalt 模式 → dmesg 只看到 COBALT_RTDM_READ
./demo-rtdm-app

# Linux 模式 → dmesg 只看到 VFS_READ（需改 demo-rtdm-app 跳过 harden）
./demo-rtdm-app --linux

dmesg | grep '>>>'
```

## 编译工作流

内核模块需要内核 build tree，但 VM 里 `/lib/modules/.../build` 指向 host 路径（VM 内不可访问）。

**当前方案**：
```
Host: 编译 demo-rtdm-irq.ko  ──scp──→  VM: /root/xenomai/
VM:   编译 demo-rtdm-app     (需要 libcobalt, 在 VM 内)
```

### Host 编译模块

```bash
cd /home/richard/work/2026/xenomai/demo
# Kbuild 文件 (obj-m := demo-rtdm-irq.o) 避免 Makefile 循环调用冲突
make -C /home/richard/work/2026/xenomai/linux-dovetail M=$(pwd) \
    ccflags-y="-I/home/richard/work/2026/xenomai/xenomai/include" modules
scp -P 10003 demo-rtdm-irq.ko root@localhost:/root/xenomai/
```

### VM 编译用户态

```bash
ssh -p 10003 root@localhost
cd /root/xenomai && make apps
```

### TODO (下次继续)

- [ ] demo-rtdm-app 加 `--linux` flag 支持两种模式切换
- [ ] 跑验证：确认 oob 模式读走 Cobalt，secondary 模式读走 VFS
- [ ] 在 /proc/xenomai/sched/stat 中观察 XSC (Xenomai System Calls) 计数器
