# Step 1: qcom x86_64 + Dovetail 练手 (2026-05-14)

## 为什么需要这一步

```
dovetail 6.6.69  ──→  QCOM 6.6.119  ──→  冲突 (ARM64 是重灾区)
```

直接上 ARM64 的话冲突量太大，容易陷入泥潭。Step 1 先在 x86_64 上打：
- x86 架构冲突极少（QCOM BSP 主要改 ARM64），大部分 patch 直接通过
- 纯练 git am 流程：`apply → conflict → resolve → continue`
- 熟悉冲突模式后，Step 2 ARM64 遇到类似问题心里有底

## 整体思路

```
qcom 树 (6.6.119, ARM64 SoC)
  ↓ x86_64_defconfig (忽略 ARM64 驱动)
  ↓ 打 171 个 dovetail 补丁
  ↓ 解决 x86 + 通用文件冲突
  ↓ 编译、QEMU 启动
  ↓ 遇到冲突 → 记录 → 对应到 Step 2 的 ARM64 场景
```

## 1. 准备

### 1.1 交叉编译器

```bash
# 确认已装
sudo apt install gcc-aarch64-linux-gnu

# 验证
aarch64-linux-gnu-gcc --version
```

### 1.2 创建工作分支

```bash
cd ~/work/2026/xenomai/qcom

# 确保起点干净
git am --abort 2>/dev/null
git checkout kernel.qclinux.1.0.r1-rel  # QCOM baseline 分支
git checkout -b dovetail-x86-practice
```

### 1.3 git 冲突配置

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
  │   ├─ x86/ 文件冲突      → 解决 (x86 目标必须修)
  │   ├─ arm/arm64/ 文件    → 可以不修 (x86 不需要)
  │   │     git checkout --theirs <file>   # 用 patch 的版本
  │   │     git add <file>
  │   ├─ 通用文件冲突       → 仔细分析两边改动
  │   │     kernel/irq/、include/linux/、kernel/sched/ 等
  │   ├─ QCOM 驱动冲突      → x86 不需要，直接禁用
  │   │     drivers/pinctrl/pinctrl-msm.c 等
  │   └─ include/ 宏冲突    → ifdef CONFIG_DOVETAIL 包起来或合并
  │
  ├─ 解决后:
  │   git add <resolved-files>
  │   git am --continue
  │
  └─ 真不行:
      git am --skip   # 跳过此 patch (记录下来)
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

### x86_64 不太会冲突的

x86 架构文件 QCOM BSP 基本不改，这些大概率 clean apply：
- `arch/x86/` 下的 dovetail 文件
- `kernel/dovetail.c` (新文件，QCOM 没有)
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

## 5. x86_64 编译

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
> 不需要处理它们的冲突。

## 6. QEMU 验证

### 6.1 prepare-kernel + Xenomai 用户态

```bash
cd ~/work/2026/xenomai

~/work/2026/xenomai/xenomai/scripts/prepare-kernel.sh \
  --linux=~/work/2026/xenomai/qcom \
  --arch=x86 \
  --verbose
```

### 6.2 重新编译内核（Cobalt 现在在内核树里了）

```bash
cd ~/work/2026/xenomai/qcom
make -j$(nproc)
```

### 6.3 安装模块到镜像

```bash
sudo mount -o loop,offset=$((2099200 * 512)) \
  /home/richard/work/images/ubuntu/server/image-8G.img /mnt
sudo make modules_install INSTALL_MOD_PATH=/mnt
sudo umount /mnt
```

### 6.4 QEMU 启动

```bash
qemu-system-x86_64 \
  -enable-kvm -nographic -m 8192 -smp 4 \
  -kernel /home/richard/work/2026/xenomai/qcom/arch/x86/boot/bzImage \
  -append "root=/dev/sda1 rw console=ttyS0" \
  -drive file=/home/richard/work/images/ubuntu/server/image-8G.img,format=raw \
  -netdev user,id=net0001,hostfwd=tcp::10003-:22 \
  -device e1000,netdev=net0001,mac=52:54:98:76:00:03
```

### 6.5 SSH 验证

```bash
ssh -p 10003 root@localhost

dmesg | grep -i "dovetail\|xenomai\|cobalt"
# 期望: IRQ pipeline: high-priority Xenomai stage added
#       [Xenomai] Cobalt v3.3.3

cat /proc/xenomai/latency
/usr/xenomai/bin/latency
```

## 7. 记录冲突经验

每个手工解决过的冲突都记录下来格式：

```
### 00XX-title.patch
文件: kernel/irq/manage.c
问题: QCOM 在 __setup_irq() 里改了 xxx，dovetail 在邻近行加了 irq_pipeline 相关
解决: 保留 QCOM 的改动，把 dovetail 的 irq_pipeline 块插在后面
```

这些记录是 Step 2 ARM64 的参考——同名文件同样的冲突模式大概率重现。

## 8. Step 2 衔接

Step 1 完成后：
- 知道哪些 patch 会在哪些文件冲突
- 知道冲突的解决模式（宏冲突 merge、结构体成员追加、函数内代码块插入）
- 同样的冲突处理流程直接搬到 ARM64 上

```bash
# Step 2 开头就是：
cd ~/work/2026/xenomai/qcom
git checkout kernel.qclinux.1.0.r1-rel
git checkout -b dovetail-arm64

# 同样 for 循环打补丁，冲突时用 Step 1 的经验
for p in $(ls ../patches-dovetail/0*.patch | sort); do
  git am --3way "$p" || break
done
```

区别是 ARM64 文件冲突不能用 `git checkout --theirs` 跳过——必须手工修。
