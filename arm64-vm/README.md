# QEMU ARM64: Dovetail + Xenomai 验证环境搭建

## 背景

在 QEMU ARM64 virt 机器上运行 QCOM 6.6.119 + Dovetail + Xenomai Cobalt 内核，
用于补丁验证和延迟测试，无需真实硬件 (RB3 Gen2)。

## 概览

```
宿主机 (x86_64)
  └─ QEMU system emulation (qemu-system-aarch64)
       └─ -M virt -cpu cortex-a57
            └─ Kernel: QCOM 6.6.119 + Dovetail + Xenomai (Image, 23MB)
                 └─ initramfs: busybox (Alpine musl) + 测试工具
```

---

## 1. 内核编译

### 1.1 源码目录

```
/home/richard/work/2026/xenomai/qcom/     # QCOM 6.6.119 + Dovetail patches
```

### 1.2 配置

```bash
cd /home/richard/work/2026/xenomai/qcom

# 关键 config (arch/arm64/configs/ 或手动 make menuconfig):
CONFIG_IRQ_PIPELINE=y        # Dovetail IRQ pipeline
CONFIG_DOVETAIL=y            # Dovetail alternate scheduling
CONFIG_XENOMAI=y             # Xenomai Cobalt co-kernel
CONFIG_PREEMPT=y             # Full preemption

# QEMU virt 不需要的 QCOM 驱动:
# CONFIG_ARCH_QCOM is not set (QEMU 用 GENERIC)
```

### 1.3 编译

```bash
cd /home/richard/work/2026/xenomai/qcom
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc) Image
# 产物: arch/arm64/boot/Image (~23MB)
```

---

## 2. initramfs 构建

### 2.1 获取 busybox (Alpine musl)

从预构的 Alpine ARM64 rootfs 中提取:

```bash
zcat alpine-minirootfs-3.21.0-aarch64.tar.gz | sudo tar -C /tmp/alpine-extracted -xpf -
```

或者直接复用已有的 busybox + musl 环境:

```bash
# 已有 initramfs 中包含 busybox (dynamically linked to musl)
zcat /tmp/diag-initramfs.cpio.gz | cpio -idm  # 解压出完整 rootfs skeleton
```

### 2.2 添加测试工具

- `timer-diag`: 6 项 POSIX timer 诊断测试 (alarm/pause, nanosleep, poll, clock_nanosleep)
- `latency`: standalone latency 测试 (POSIX clock_nanosleep)

编译:
```bash
aarch64-linux-gnu-gcc -static -o timer-diag timer-diag.c
aarch64-linux-gnu-gcc -static -o latency latency-standalone.c
cp timer-diag latency <initramfs-root>/
```

### 2.3 创建 init 脚本

```sh
#!/bin/sh
mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev
mount -t tmpfs none /tmp
echo "=== Dovetail + Xenomai $(cat /proc/xenomai/version 2>/dev/null) ==="
exec /bin/sh
```

### 2.4 打包

```bash
cd <initramfs-root>
find . -print0 | cpio --null -ov --format=newc | gzip -9 > /tmp/initramfs.cpio.gz
```

---

## 3. QEMU 启动

### 3.1 QEMU 二进制

```bash
# 系统包或手动路径:
/tmp/qemu-arm-extracted/usr/bin/qemu-system-aarch64
```

### 3.2 启动命令

```bash
qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a57 \
  -smp 1 \
  -m 1G \
  -kernel /path/to/qcom/arch/arm64/boot/Image \
  -initrd /tmp/initramfs.cpio.gz \
  -append "console=ttyAMA0" \
  -nographic \
  -no-reboot
```

### 3.3 便捷脚本

`qemu-verify.sh` (项目根目录):
```bash
./qemu-verify.sh   # 启动 QEMU，进入 busybox shell
```

---

## 4. Bug 修复 (3 处)

内核启动后 arch_timer PPI 计数为 0，所有 timer 操作挂死 (sleep, nanosleep, alarm 等)。

### 4.1 根因分析

完整链路:

```
硬件 PPI (arch_timer) 触发
  → handle_irq_pipelined()          [Dovetail 拦截]
    → handle_percpu_devid_irq()
      → get_flow_step()             [返回 IRQ_FLOW_PILEUP — 误判]
      → handle_oob_irq()            [OOB stage 不存在]
        → irq_post_stage(inband)    [defer 到 in-band log]
        → return false
      → !handled → chip->irq_mask() [PPI 被 MASK!]
  → synchronize_pipeline_on_irq()   [完成阶段]
    → sync_current_irq_stage()      [重放 deferred IRQ]
      → do_inband_irq()
        → arch_do_IRQ_pipelined()
          → handle_irq_desc()
            → is_hardirq(desc)      [返回 false! ← 主 bug]
                in_pipeline()=false, IRQS_DEFERRED=仍置位
            → handle_enforce_irqctx() [QCOM GIC 设置了这个]
            → return -EPERM         [IRQ 被丢弃!]
```

QCOM GIC 驱动对所有 IRQ 设置了 `irqd_set_handle_enforce_irqctx()`，这要求
`handle_irq_desc()` 只能在 hardirq 上下文中调用。重放时 `IRQS_DEFERRED` 仍在
`is_hardirq()` 检查前未清除，导致检查失败。

### 4.2 修复 1 — `kernel/irq/irqdesc.c` `is_hardirq()` 【主修复】

**问题**: pipelined 模式下，deferred IRQ 重放时 `in_pipeline()` 为 false 且
`IRQS_DEFERRED` 仍置位，函数返回 false。但此时 `irq_enter()` 已被调用，
`in_hardirq()` 为 true。

**修复**: 在 pipelined 模式下也检查 `in_hardirq()`:

```diff
 static inline bool is_hardirq(struct irq_desc *desc)
 {
     if (!irqs_pipelined())
         return in_hardirq();

     if (in_pipeline() || !(desc->istate & IRQS_DEFERRED))
         return true;
+
+    /*
+     * During in-band sync replay via sync_current_irq_stage(),
+     * we are not in pipeline context but irq_enter() was called
+     * by arch_do_IRQ_pipelined(), so in_hardirq() is true even
+     * though IRQS_DEFERRED is still set.
+     */
+    if (in_hardirq())
+        return true;

     return false;
 }
```

### 4.3 修复 2 — `kernel/irq/chip.c` `get_flow_step()`

**问题**: 在 in-band sync 重放中 `hard_irqs_disabled()` 为 false (hardirq 已
local enable)，但 `in_pipeline()` 也为 false。原代码将 `!in_pipeline()` +
`!hard_irqs_disabled()` 返回 `IRQ_FLOW_REPLAY`，而 `in_pipeline()` +
任意条件返回 `IRQ_FLOW_PILEUP`。但 in-band 重放时两者均为 false，需明确定义。

**修复**: 用 `hard_irqs_disabled()` 区分硬件 IRQ 入口 (PILEUP) 和
in-band sync 重放 (REPLAY):

```diff
-    return in_pipeline() ? IRQ_FLOW_PILEUP : IRQ_FLOW_REPLAY;
+    /*
+     * During in-band sync replay via sync_current_irq_stage(),
+     * hardirqs are locally enabled, whereas a hardware IRQ
+     * entry has them disabled. Use this to distinguish.
+     */
+    if (in_pipeline() && hard_irqs_disabled())
+        return IRQ_FLOW_PILEUP;
+
+    return IRQ_FLOW_REPLAY;
```

### 4.4 修复 3 — `kernel/irq/chip.c` `handle_percpu_devid_irq()`

**问题**: 原代码用 `may_start_flow(flow)` 判断是否要向 pipeline 投喂。
这对 REPLAY 和 FORWARD 都返回 false，但不会清除 `IRQS_DEFERRED`。
对比 `handle_fasteoi_irq()` 用的是 `should_feed_pipeline()`，
后者在 REPLAY 时调用 `irq_clear_deferral()`。

**修复**: 替换为 `should_feed_pipeline()`:

```diff
-    if (may_start_flow(flow)) {
+    if (should_feed_pipeline(desc, flow)) {
```

---

## 5. 验证

### 5.1 启动后自动检查

```sh
dmesg | grep -i "IRQ pipeline"   # "IRQ pipeline enabled"
dmesg | grep -i cobalt           # "Cobalt v3.3.3"
cat /proc/xenomai/version        # 3.3.3
cat /proc/interrupts | grep arch_timer  # 计数值 > 0
cat /proc/interrupts | grep proxy       # proxy tick 计数值 > 0
```

### 5.2 Timer 诊断

```sh
/timer-diag   # 6 项测试: alarm, nanosleep, poll, clock_nanosleep
```

### 5.3 Latency 测试

```sh
latency 1000 5   # 5 次采样, 1000us 周期
```

### 5.4 测试结果 (QEMU TCG emulation)

| 测试项 | 结果 |
|--------|------|
| arch_timer PPI | 正常工作 (1000+ 中断) |
| proxy tick | 正常工作 |
| alarm / pause | 通过 |
| nanosleep | 通过 |
| poll | 通过 |
| clock_nanosleep (abs) | 通过 |
| latency (5 samples) | min=278µs, avg=323µs, 0 overruns |

> 注意: QEMU TCG 模拟的延迟远高于真实硬件。RB3 Gen2 实物预期 5-20µs。

---

## 6. 文件清单

| 路径 | 说明 |
|------|------|
| `qemu-verify.sh` | 一键启动 QEMU 交互 shell |
| `qcom/.config.dovetail-fix-working` | 工作内核配置 |
| `qcom/arch/arm64/boot/Image` | 编译好的内核 (23MB) |
| `qcom/kernel/irq/irqdesc.c` | 修复 1: is_hardirq() |
| `qcom/kernel/irq/chip.c` | 修复 2+3: get_flow_step() + handle_percpu_devid_irq() |
| `/tmp/test-fix-shell.cpio.gz` | 交互式 initramfs |
| `/tmp/qemu-arm-extracted/usr/bin/qemu-system-aarch64` | QEMU 二进制 |
| `latency-standalone.c` | standalone latency 测试 (POSIX 版本) |

---

## 7. 当前局限

- **latency 不是 Xenomai 原版**: initramfs 中的 `latency` 用 POSIX API
  (`clock_nanosleep`)，不是 Xenomai Alchemy API (`rt_task_set_periodic`)。
  测的是 Linux 调度延迟，不是 Xenomai OOB 实时延迟。
- **无 Xenomai userspace 库**: 未交叉编译 `libcobalt.so`、`libalchemy.so`。
  需要编译 Xenomai userspace 才能运行真正的 RT 测试。
- **QEMU TCG 延迟大**: ~300µs vs 实物预期 ~10µs。
- **单核**: `-smp 1`，未测试 SMP。
