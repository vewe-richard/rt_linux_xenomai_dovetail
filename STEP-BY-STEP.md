# Xenomai Dovetail on QCOM RB3 Gen2 — 操作手册

## 已准备的开发工具

### gtags (GNU Global)

两个内核树已生成 tags，用于快速查找符号定义和引用：

| 树 | 大小 | 生成时间 |
|---|------|---------|
| `qcom/GTAGS` + `GRTAGS` + `GPATH` | ~1.3 GB | 2026-05-09 |
| `linux-dovetail/GTAGS` + `GRTAGS` + `GPATH` | ~1.3 GB | 2026-05-09 |

```bash
# 在当前树查定义
cd ~/work/2026/xenomai/qcom && global -d symbol_name

# 查引用
global -r symbol_name

# Phase 2 解决冲突时对比两棵树:
global -d dovetail_init              # dovetail 树有，qcom 树没有 → dovetail 新增函数
```

### 补丁目录

`patches-dovetail/` 包含:
- `0001-*.patch` ~ `0171-*.patch` — 171 个 dovetail commit patch
- `merge-*.patch` — 8 个 merge commit diff (冲突解决参考)
- `0000-dovetail-combined.patch` — 全量 diff 参考 (900K, 31233 行)

---

## 总体路线

```
Step 0: 编译 linux-dovetail (x86_64) + Xenomai → QEMU 验证
  │     验证 dovetail + Xenomai 本身可工作
  │     产物: 可启动的 bzImage, Xenomai 用户态
  ▼
Step 1: 在 qcom 仓库上打 dovetail 补丁 (x86_64) → QEMU 验证
  │     练手打补丁流程, x86 冲突少
  │     产物: qcom 6.6.119 + dovetail (x86_64), 学习冲突处理经验
  ▼
Step 2: 在 qcom 仓库上打 dovetail 补丁 (ARM64) → 交叉编译
  │     正式 Phase 2
  │     产物: qcom 6.6.119 + dovetail (ARM64), 可交叉编译通过
  ▼
Step 3: 解决编译问题, 适配 QCOM-specific 冲突
  │     产物: 完整 patch 系列, 代码在 qcom 树 clean apply
  ▼
Step 4: Yocto layer 化
        产物: meta-qcom-xenomai layer, bitbake 可编译
```

---

## Step 0 — linux-dovetail x86_64 + Xenomai 基线验证

**目标**: 确认 dovetail + Xenomai 组合本身可编译启动，建立已知良好基线。

### 0.1 prepare-kernel

```bash
cd ~/work/2026/xenomai

~/work/2026/xenomai/xenomai/scripts/prepare-kernel.sh \
  --linux=~/work/2026/xenomai/linux-dovetail \
  --arch=x86 \
  --verbose
```

**做了什么**: 检测到 linux-dovetail 已有 dovetail → 跳过 pipeline patch；通过 symlink 把 Xenomai cobalt core 链入内核树；修改 init/Kconfig、arch/x86/Makefile 等，添加 CONFIG_XENOMAI 入口。

**验证**: `ls ~/work/2026/xenomai/linux-dovetail/kernel/xenomai/` — 应有 cobalt 源码 symlink。

### 0.2 配置内核

```bash
cd ~/work/2026/xenomai/linux-dovetail

make x86_64_defconfig

./scripts/config --enable CONFIG_IRQ_PIPELINE
./scripts/config --enable CONFIG_DOVETAIL
./scripts/config --enable CONFIG_XENOMAI

# 手动微调依赖
make menuconfig
```

**menuconfig 要确认的项**:
- General setup → [*] Xenomai/cobalt co-kernel
- 确保 PREEMPT=y (非 PREEMPT_RT, dovetail 要标准 preempt)

### 0.3 编译内核

```bash
cd ~/work/2026/xenomai/linux-dovetail
make -j$(nproc)
```

**产物**: `arch/x86/boot/bzImage` (压缩), `vmlinux` (GDB 用)。

### 0.4 编译 Xenomai 用户态

```bash
cd ~/work/2026/xenomai/xenomai

# 首次如果没有 configure:
./autogen.sh

./configure --with-core=cobalt --enable-pshared
make -j$(nproc)
sudo make install
```

**安装位置**: `/usr/xenomai/` (默认 prefix)。
**安装到 QEMU 镜像**: 可用于在 QEMU 验证时安装，或直接 mount image 复制文件。

### 0.5 QEMU 启动

```bash
qemu-system-x86_64 \
  -enable-kvm -nographic -m 8192 -smp 4 \
  -kernel /home/richard/work/2026/xenomai/linux-dovetail/arch/x86/boot/bzImage \
  -append "root=/dev/sda1 rw console=ttyS0" \
  -drive file=/home/richard/work/images/ubuntu/server/image-8G.img,format=raw \
  -netdev user,id=net0001,hostfwd=tcp::10003-:22 \
  -device e1000,netdev=net0001,mac=52:54:98:76:00:03
```

### 0.6 验证

```bash
ssh -p 10003 root@localhost

# 1. 内核确认
dmesg | grep -i "dovetail\|interrupt pipeline\|xenomai\|cobalt"

# 2. latency 测试
/usr/xenomai/bin/latency
```

### 0.7 调试（如需要）

内核不带 `-s -S` 正常启动。如需 GDB 调试：

```bash
# 启动时加:
qemu-system-x86_64 ... -s -S

# 另一个终端:
gdb /home/richard/work/2026/xenomai/linux-dovetail/vmlinux
(gdb) target remote :1234
(gdb) continue
```

---

## Step 1 — qcom x86_64 + dovetail (练手)

**目标**: 在 qcom 仓库的 x86_64 上打 dovetail 补丁，熟悉 git am 流程和冲突处理，x86 冲突预期较少。

### 1.1 准备工作区

```bash
cd ~/work/2026/xenomai/qcom

# 从当前 HEAD 创建工作分支
git checkout -b dovetail-x86-practice

# 或者从你想用的基线创建
# git checkout -b dovetail-x86-practice <baseline-commit>
```

### 1.2 安装 git am 的 3-way merge helper

```bash
# 确认 git 配置
git config --global merge.conflictstyle diff3
```

### 1.3 逐个打补丁

```bash
cd ~/work/2026/xenomai/qcom

# patches 在 ../patches-dovetail/ ，按数字排序
for p in $(ls ../patches-dovetail/0*.patch | sort); do
  echo "Applying: $(basename $p)"
  git am --3way "$p" || {
    echo "CONFLICT at: $(basename $p)"
    break
  }
done
```

**关键认知 — 补丁打不上时的决策流程**：

```
git am --3way 失败
     │
     ├→ Step A: git status 看冲突文件
     │
     ├→ Step B: 对每个冲突文件问:
     │     ├→ 是 x86 文件的冲突? 解决。
     │     ├→ 是 ARM/ARM64 文件的冲突? x86 不需要，可以 patch -Np1 跳过对应文件。
     │     ├→ 是通用文件 (kernel/irq/, include/linux/) 的冲突? 仔细分析解决。
     │     └→ 是 merge 冲突 diff (已有 merge-*.patch 参考)? 参考合并。
     │
     ├→ Step C: 解决后继续
     │     git add <resolved-files>
     │     git am --continue
     │
     └→ 如果实在无法解决:
           git am --abort     # 回退本次 patch
           git am --skip      # 跳过此 patch (慎用，记录原因)
```

### 1.4 在 QEMU 上验证 x86_64

步骤同 0.1-0.6，但内核树换成 qcom，分支 `dovetail-x86-practice`。

**x86 特有注意**:
- qcom 树可能没有 x86_64_defconfig 的 QCOM 修改，用 make x86_64_defconfig 作为起点即可
- 不需要 QCOM 驱动（它们是 ARM64 的），x86 的通用驱动足够

### 1.5 记录经验

在 `patches-dovetail/README.md` 或本文件中记录：
- 哪些 patch 冲突了
- 冲突原因和解决方法
- 这些经验对 Step 2 (ARM64) 有指导意义

---

## Step 2 — qcom ARM64 + dovetail 补丁应用 (正式 Phase 2)

**目标**: 在 qcom 6.6.119 仓库的 ARM64 目标上成功应用所有 dovetail 补丁，做正确的冲突解决。

### 2.1 准备工作

```bash
cd ~/work/2026/xenomai/qcom

# 从当前 HEAD 创建 Phase 2 工作分支
git checkout -b dovetail-arm64

# 确认基线
git log --oneline -3
```

### 2.2 打补丁（和 Step 1 同样的 for 循环）

```bash
cd ~/work/2026/xenomai/qcom

for p in $(ls ../patches-dovetail/0*.patch | sort); do
  echo "Applying: $(basename $p)"
  git am --3way "$p" || {
    echo "CONFLICT at: $(basename $p)"
    break
  }
done
```

### 2.3 预知的最高风险文件 (来自 Phase 1 分析)

冲突时优先查看以下文件上下文：

| 优先级 | 文件 | QCOM BSP 可能的改动 |
|--------|------|--------------------|
| 🔴🔴🔴 | `arch/arm64/kernel/entry-common.c` | QCOM 的 syscall/trap 扩展 |
| 🔴🔴🔴 | `arch/arm64/kernel/fpsimd.c` | QCOM vendor FP/SIMD 改动 |
| 🔴🔴 | `arch/arm64/kernel/smp.c` | QCOM msm-pm / 电源管理 IPI |
| 🔴🔴 | `kernel/sched/core.c` | QCOM 调度器调优 |
| 🔴🔴 | `kernel/irq/manage.c` / `chip.c` | QCOM 中断扩展 |
| 🔴 | `drivers/pinctrl/pinctrl-msm.c` | 两边都改了 |
| 🔴 | `drivers/spmi/spmi-pmic-arb.c` | 两边都改了 |
| 🔴 | `kernel/printk/printk.c` | QCOM 日志扩展 |
| 🟡 | `include/linux/*` 头文件 | 宏/结构体冲突, 通常容易解决 |

### 2.4 特殊处理：merge commit 参考

有 8 个 `merge-*.patch` 文件在 `patches-dovetail/` 中。如果某些 patch 在 `include/linux/lockdep.h`、`drivers/base/regmap/regmap.c`、`net/packet/af_packet.c` 附近冲突，参考对应的 merge diff 看上游冲突是如何解决的。

---

## Step 3 — 编译验证和 QCOM 特有适配

**目标**: ARM64 交叉编译通过，修复 QCOM 特有的兼容性问题。

### 3.1 首次编译尝试

```bash
cd ~/work/2026/xenomai/qcom

# 使用 QCOM 的 ARM64 配置
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- menuconfig

# 启用 dovetail 相关
./scripts/config --enable CONFIG_IRQ_PIPELINE
./scripts/config --enable CONFIG_DOVETAIL
./scripts/config --enable CONFIG_XENOMAI

# 交叉编译
make -j$(nproc) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
```

### 3.2 预期编译错误类型

| 错误类型 | 原因 | 处理方法 |
|----------|------|----------|
| `implicit declaration` | dovetail 加了新函数调用, QCOM 驱动没 include 对 | 加 #include |
| `struct has no member` | dovetail 给 struct 加了成员, QCOM 改了个版本没这个成员 | 查看 QCOM 的对应 struct, 合并 |
| `redefinition` | dovetail 和 QCOM 定义了同名东西 | 通常加 #ifndef 或合并定义 |
| `BCL driver` | Battery Current Limiting 驱动和 RT 不兼容 | 参考 meta-qcom-realtime 的 fix: disable BCL |
| `MSM_DISPLAY` | 显示驱动可能和 RT 冲突 | 先 disable, 后处理 |

### 3.3 QCOM 特有驱动处理

参考 `meta-qcom-realtime/recipes-kernel/linux/linux-qcom-custom-rt/` 中的 RT fix patches。可能需要对 dovetail 做类似的适配：

- **BCL** — Battery Current Limiting 驱动和实时冲突，考虑 disable
- **MSM 显示** — 编译问题，可能是 kthread 用法不兼容
- **SPMI/PMIC** — pinctrl-msm 和 spmi-pmic-arb 的 pipeline 适配（已在 dovetail patch 0033/0034 中改动，可能和 QCOM 版本冲突）

### 3.4 编译通过后的功能验证

- 目标板（RB3 Gen2）上启动
- 或 QEMU ARM64 模拟（用 `qemu-system-aarch64 -M virt`）
- 跑 Xenomai latency 测试

---

## Step 4 — Yocto Layer 化

**目标**: 将 patch 系列和配置打包成 meta-qcom-xenomai layer。

### 4.1 Layer 结构

```
meta-qcom-xenomai/
├── conf/
│   └── layer.conf
├── recipes-kernel/
│   └── linux/
│       ├── linux-qcom-xenomai_6.6.bb
│       └── linux-qcom-xenomai/
│           ├── dovetail-patches/          # Step 2 最终产生的 clean patch 系列
│           │   └── *.patch
│           ├── dovetail-patch.inc         # SRC_URI for dovetail patches
│           ├── qcom_fixes/                # Step 3 产生的 QCOM 适配 patch
│           │   └── *.patch
│           └── xenomai.cfg                # CONFIG_DOVETAIL=y, CONFIG_XENOMAI=y 等
├── recipes-xenomai/
│   └── xenomai/
│       └── xenomai_3.3.bb                # Xenomai 用户态 recipe
├── recipes-extended/                      # procps, irqbalance bbappends (可选)
└── README.md
```

### 4.2 核心 recipe 设计

**linux-qcom-xenomai_6.6.bb**:
- `require linux-qcom-base_6.6.bb` (来自 QCOM BSP layer)
- `SRC_URI` 追加 dovetail patches + qcom fix patches + xenomai.cfg
- `do_prepare_kernel()` — 在 patch 之后调用 `prepare-kernel.sh`
- 或者用 `EXTERNALSRC` 指向已经处理好的 qcom 内核树

### 4.3 编译

```bash
# 在 Yocto 环境中
MACHINE=qcs6490-rb3gen2-core-kit \
  DISTRO=qcom-wayland \
  EXTRALAYERS="meta-qcom-xenomai" \
  source setup-environment

bitbake linux-qcom-xenomai
# 或完整镜像:
bitbake qcom-multimedia-image
```

---

## 参考：用到的工具脚本

| 脚本 | 路径 | 用途 |
|------|------|------|
| prepare-kernel.sh | `xenomai/scripts/prepare-kernel.sh` | 将 Xenomai cobalt 集成到内核树 |
| patch 系列 | `patches-dovetail/0*.patch` | 171 个 dovetail 补丁 |
| merge 参考 | `patches-dovetail/merge-*.patch` | 8 个 merge commit 的冲突解决参考 |
| 综合 diff | `patches-dovetail/0000-dovetail-combined.patch` | 全量 diff 参考 |

---

## 状态追踪

- [ ] Step 0: linux-dovetail x86_64 基线验证
- [ ] Step 1: qcom x86_64 dovetail 练手
- [ ] Step 2: qcom ARM64 dovetail 补丁应用
- [ ] Step 3: 编译通过 + QCOM 适配
- [ ] Step 4: Yocto layer 化
