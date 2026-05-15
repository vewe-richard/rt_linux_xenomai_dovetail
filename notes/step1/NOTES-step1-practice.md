# Step 1: Dovetail 整合 — 主分支打补丁 + x86_64 验证 (2026-05-15)

## 为什么先做 x86_64

```
dovetail 6.6.69  ──→  QCOM 6.6.119  ──→  冲突 (ARM64 是重灾区)
```

Step 1 的目标是**在唯一一个分支上打完全部 171 个 dovetail 补丁，再用 x86_64 验证**。
- 先打全部补丁（x86 + ARM64 + 通用文件），所有冲突都认真解决
- 用 x86_64_defconfig 编译 + QEMU 验证 dovetail/cobalt 功能
- 此时 ARM64 代码已就位但未编译，Step 2 在**同一分支**继续做 ARM64 交叉编译和真机适配

> 原来的思路是 Step 1 当"训练场"（ARM64 冲突跳过，分支丢掉），Step 2 重新开始。
> 现在是**一个主分支从 6.6.119 一路走到最终交付**，不分叉不重做。

## 整体流程

```
qcom 树 (6.6.119, ARM64 SoC)
  ↓ 创建 dovetail-integration 分支
  ↓ 打 171 个 dovetail 补丁（全部！x86 + ARM64 + 通用）
  ↓ 解决所有冲突
  ↓ x86_64 编译 + QEMU 验证
  │
  │  [同一个分支]
  │
  ↓ Step 2: 交叉编译 ARM64 → 修编译错误 → 真机验证
```

## 1. 准备

### 1.1 创建主分支

```bash
cd ~/work/2026/xenomai/qcom

# 确保起点干净
git am --abort 2>/dev/null
git checkout kernel.qclinux.1.0.r1-rel  # QCOM baseline
git checkout -b dovetail-integration     # 唯一分支，一直用到交付
```

### 1.2 git 冲突配置

```bash
git config --global merge.conflictstyle diff3
```

`diff3` 在冲突时显示三方：OURS | BASE | THEIRS，方便判断两边各自改了什么。

## 2. 逐个打补丁

```bash
cd ~/work/2026/xenomai/qcom

for p in $(ls ../patches-dovetail/0*.patch | sort); do
  echo "=== $(basename $p) ==="
  git am --3way "$p" || {
    echo ">>> CONFLICT: $(basename $p) <<<"
    break
  }
done
```

`--3way`: 当 patch 的上下文不匹配时，git 会找 patch 的 base blob 做三方合并。大部分简单冲突（行号偏移、邻近行有改动）自动解决。

## 3. 冲突处理流程

```
git am --3way 失败
  │
  ├─ git status → 看冲突文件
  │
  ├─ git diff → 看冲突内容 (<<<<<<< = ours ======= ======= >>>>>>>)
  │
  ├─ 决策:
  │   ├─ 所有冲突都认真解决（不管 x86/arm64/通用）
  │   │    这个分支要同时支持 x86_64 和 ARM64
  │   │
  │   ├─ arch/arm64/ 文件冲突  → 认真合并（Step 2 要用）
  │   ├─ arch/arm/ 文件冲突    → 认真合并
  │   ├─ arch/x86/ 文件冲突    → 认真合并
  │   ├─ 通用文件冲突          → 仔细分析两边改动
  │   │     kernel/irq/、include/linux/、kernel/sched/ 等
  │   ├─ QCOM 驱动冲突         → 认真合并
  │   │     drivers/pinctrl/pinctrl-msm.c 等
  │   └─ include/ 宏冲突       → ifdef CONFIG_DOVETAIL 包起来或合并
  │
  ├─ 解决后:
  │   git add <resolved-files>
  │   git am --continue
  │
  └─ 真不行:
      git am --skip   # 跳过此 patch（记录下来，必须回头补）
```

### 冲突标记解读

```
<<<<<<< ours                    ← QCOM 树的代码 (我们)
    [...QCOM 的版本...]
||||||| base                    ← patch 基于的原始版本
    [...原始 Linux 6.6.69 代码...]
=======
    [...dovetail 的版本...]     ← patch 想改成这样
>>>>>>> theirs
```

`diff3` 模式多了 base 行，能看出 "原始代码 → QCOM 改了什么" 和 "原始代码 → dovetail 改了什么"，比两方对比好判断。

## 4. 预判冲突

### 不太会冲突的（新文件 QCOM 没有）

- `arch/x86/` 下的 dovetail 文件
- `kernel/dovetail.c` (新文件)
- `kernel/irq/pipeline.c` (新文件)
- 大部分 `include/dovetail/` 头文件 (新文件)

### 可能冲突的（通用文件两棵树都改了）

| 文件 | QCOM 可能改了什么 | 处理 |
|------|-------------------|------|
| `kernel/irq/manage.c` | QCOM 中断扩展 | 仔细合并 |
| `kernel/irq/chip.c` | 同上 | 仔细合并 |
| `kernel/sched/core.c` | QCOM 调度调优 | 仔细合并 |
| `kernel/printk/printk.c` | QCOM 日志扩展 | 通常容易 |
| `include/linux/irq.h` | 宏/结构体 | ifdef 或合并 |
| `include/linux/sched.h` | 结构体成员 | 合并 |

### ARM64 专用冲突（Step 1 虽然不编译但必须修）

| 文件 | 风险 |
|------|------|
| `arch/arm64/kernel/entry-common.c` | dovetail +274 行，QCOM BSP 可能改 syscall 路径 |
| `arch/arm64/kernel/fpsimd.c` | dovetail +216 行，QCOM 可能有 vendor fp 改动 |
| `arch/arm64/kernel/smp.c` | dovetail +108 行 IPI 扩展 |

> 这些冲突修完后在 Step 1 无法验证（x86_64 QEMU 不走 ARM64 代码）。
> 代码正确性靠审查保证，运行时验证在 Step 2 交叉编译 + 真机完成。

## 5. x86_64 编译 + 验证

### 5.1 编译

```bash
cd ~/work/2026/xenomai/qcom

make x86_64_defconfig

# 启用 dovetail
./scripts/config --enable CONFIG_IRQ_PIPELINE
./scripts/config --enable CONFIG_DOVETAIL
./scripts/config --enable CONFIG_XENOMAI
make olddefconfig

# 编译
make -j$(nproc)
```

> QCOM 驱动 (drivers/soc/qcom/、drivers/clk/qcom/ 等) 在 x86_64_defconfig 下不会编译，
> 因为由 `depends on ARCH_QCOM` 保护。

### 5.2 prepare-kernel（在 dovetail 基础上加 cobalt）

```bash
cd ~/work/2026/xenomai

~/work/2026/xenomai/xenomai/scripts/prepare-kernel.sh \
  --linux=~/work/2026/xenomai/qcom \
  --arch=x86 \
  --verbose
```

### 5.3 重新编译内核（cobalt 现在在内核树里了）

```bash
cd ~/work/2026/xenomai/qcom
make -j$(nproc)
```

### 5.4 安装模块到镜像

```bash
sudo mount -o loop,offset=$((2099200 * 512)) \
  /home/richard/work/images/ubuntu/server/image-8G.img /mnt
sudo make modules_install INSTALL_MOD_PATH=/mnt
sudo umount /mnt
```

### 5.5 QEMU 启动

```bash
qemu-system-x86_64 \
  -enable-kvm -nographic -m 8192 -smp 4 \
  -kernel /home/richard/work/2026/xenomai/qcom/arch/x86/boot/bzImage \
  -append "root=/dev/sda1 rw console=ttyS0" \
  -drive file=/home/richard/work/images/ubuntu/server/image-8G.img,format=raw \
  -netdev user,id=net0001,hostfwd=tcp::10003-:22 \
  -device e1000,netdev=net0001,mac=52:54:98:76:00:03
```

### 5.6 SSH 验证

```bash
ssh -p 10003 root@localhost

dmesg | grep -i "dovetail\|xenomai\|cobalt"
# 期望: IRQ pipeline: high-priority Xenomai stage added
#       [Xenomai] Cobalt v3.3.3

cat /proc/xenomai/latency
/usr/xenomai/bin/latency
```

## 6. 记录冲突经验

每个手工解决过的冲突都记录到 `notes/step1/conflict-log.md`：

```
### 00XX-title.patch
文件: kernel/irq/manage.c
问题: QCOM 在 __setup_irq() 里改了 xxx，dovetail 在邻近行加了 irq_pipeline 相关
解决: 保留 QCOM 的改动，把 dovetail 的 irq_pipeline 块插在后面
```

Step 2 遇到同名文件的冲突时可以参考这些记录。

## 7. Step 2 衔接

Step 1 完成于 2026-05-15，**同一分支** `dovetail-integration`已经是：
- [x] 全部 171 dovetail 补丁已打（3 个需手动修复，详情见 `conflict-log.md`）
- [x] x86_64 编译通过 — include/drm 有一处语法错误，也修了
- [x] Xenomai Cobalt prepare-kernel.sh 已执行 + CONFIG_XENOMAI=y 编译通过
- [ ] QEMU 验证 dovetail/cobalt — 需 sudo (modules_install)，后续在可 sudo 环境做
- [x] ARM64 代码已就位但未经编译

## x86_64 编译修复记录

内核源码有两处错误需要手动修复（dovetail 补丁 + QCOM 代码兼容问题）：

### 1. `arch/x86/include/asm/irqflags.h`
- **问题**: dovetail 补丁引入了 `irq_pipeline.h` 定义的 `arch_local_*` 函数，但 QCOM 的 irqflags.h 仍保持旧定义，导致重复定义冲突
- **修复**: 仿照 linux-dovetail 树，移除冲突的 `arch_local_save_flags/diable/enable` 定义，改为 include `irq_pipeline.h`；补充缺失的 `native_save_flags/irq_sync/irq_save/irqs_disabled_flags/irq_restore/irqs_disabled` 函数

### 2. `arch/x86/kernel/apic/vector.c`
- **问题**: 结构体改用 `DECLARE_X86_CLEANUP_WORKER`（timer → irq_work），但 `__vector_schedule_cleanup` 仍直接访问 `cl->timer`
- **修复**: 替换为 `queue_cleanup_work(cl, cpu)` 宏

### 3. `include/drm/drm_of.h`
- **问题**: QCOM BSP 的语法错误：`drm_of_get_lane_mapping` 声明用 `;` 而不是 `)` 结束参数列表
- **修复**: 将 `;` 改为 `)`

### 4. `drivers/pci/pci.h`
- **问题**: `of_pci_setup_wake_irq` / `of_pci_teardown_wake_irq` 作为 static 但未 inline，触发 -Werror=unused-function
- **修复**: 加 `inline` 关键字

Step 2 将在**同一分支**继续：

```bash
# 不需要 checkout 新分支！直接在当前分支继续
cd ~/work/2026/xenomai/qcom
git branch   # 应该在 dovetail-integration

# ARM64 交叉编译
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
# ... 启用 dovetail/cobalt ...
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc)
```
