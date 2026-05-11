# Xenomai Dovetail on Qualcomm RB3 Gen2 — Project Guide

## Network / Proxy

```bash
export http_proxy="http://127.0.0.1:12334"
export https_proxy="http://127.0.0.1:12334"
export HTTP_PROXY="http://127.0.0.1:12334"
export HTTPS_PROXY="http://127.0.0.1:12334"
```
Git/curl/bitbake 等工具需通过以上代理访问外网。`~/start-claude-with-proxy.sh` 中已配好。

## Project Context
- **Client**: Meyer Elektronik & IT (Switzerland), contact Jonathan
- **Project**: Port Xenomai 3.3.x with Dovetail onto Qualcomm RB3 Gen2 Vision Kit
- **Deliverable**: Yocto layer (meta-qcom-xenomai) with bitbake recipes, kernel patches, and configs
- **Reference**: `meta-qcom-realtime` (uses PREEMPT_RT, same pattern but we use Xenomai/Dovetail instead)

## Collaboration Preferences
- Richard prefers reading Chinese, but may write questions in English
- Claude should reply in Chinese by default; switch to English if Richard uses English
- 回答用中文，代码和命令保持英文

## OCR Screenshot Tool — 图片文字提取

DeepSeek 不支持多模态，需要读取图片中文字时按以下流程：

### 首次自动安装
1. 检查工具是否已安装：
   `test -x ~/deepseek-with-claude-code-skills/venv/bin/python && test -f ~/deepseek-with-claude-code-skills/ocr-slice.py`
2. 若未安装，询问用户是否自动安装（需代理），确认后执行：
   ```
   git clone https://github.com/vewe-richard/deepseek-with-claude-code-skills.git ~/deepseek-with-claude-code-skills
   python3 -m venv ~/deepseek-with-claude-code-skills/venv
   ~/deepseek-with-claude-code-skills/venv/bin/pip install easyocr numpy pillow
   ~/deepseek-with-claude-code-skills/venv/bin/python -c "import easyocr; easyocr.Reader(['ch_sim','en'], gpu=False)"
   ```
3. 安装完成后验证：
   `~/deepseek-with-claude-code-skills/venv/bin/python ~/deepseek-with-claude-code-skills/ocr-slice.py --quiet ~/deepseek-with-claude-code-skills/verify.jpg`
   确认输出包含 `[verify-ok]`

### 日常使用
当用户让你读截图/图片时，直接跑：
```
~/deepseek-with-claude-code-skills/venv/bin/python ~/deepseek-with-claude-code-skills/ocr-slice.py --quiet <图片路径>
```
拿到文字后分析内容。

## Key Repos (local clones)

| Repo | Directory | Version | Role |
|------|-----------|---------|------|
| linux-dovetail | `linux-dovetail/` | **v6.6.69** + Dovetail patches | Upstream Dovetail kernel (patch source) |
| Qualcomm downstream | `qcom/` | **6.6.119** QCLINUX 1.0 | Target kernel base (Qualcomm BSP) |
| Xenomai 3.3.x | `xenomai/` | stable/v3.3.x | Xenomai userspace + kernel cobalt driver |
| meta-qcom-realtime | `meta-qcom-realtime/` | QCLINUX 1.8 reference | Yocto layer template (PREEMPT_RT, not Xenomai) |

## Version Mismatch — the core problem

```
dovetail:  v6.6.69  (mainline base + dovetail patches)
qcom:      v6.6.119 (mainline base + Qualcomm BSP patches)
target:    6.6.119 + dovetail + Qualcomm BSP
```

**Strategy**: Take QCOM 6.6.119 as base, extract and apply Dovetail patches on top.
Why: Qualcomm BSP patches (thousands) are vendor-specific and fragile. Dovetail patches are
a smaller, well-structured series that touches generic kernel infrastructure (irq pipeline).

## Task Breakdown

### Phase 1: Extract Dovetail Patches from linux-dovetail

**Common ancestor (baseline)**: `a30cd70ab75a` — Linux 6.6.69 (verified identical in both trees)

```bash
cd linux-dovetail
# Step 1: Verify commit range — should be ~179 dovetail commits
git log --oneline a30cd70ab75a..HEAD | wc -l
git log --oneline a30cd70ab75a..HEAD | head -20   # spot-check: all dovetail?

# Step 2: Generate patch series
mkdir -p ../patches-dovetail
git format-patch -o ../patches-dovetail/ a30cd70ab75a..HEAD
# → produces 0179-*.patch files
```

**Result (2026-05-08)**: 179 patches generated in `patches-dovetail/`.

### Phase 2: Apply Dovetail to QCOM 6.6.119
```bash
cd qcom
# Apply dovetail patches one by one, fix conflicts
for p in ../patches-dovetail/*.patch; do
  git am --3way "$p" || break  # stop at conflict, resolve manually
done
```

Key conflict areas to expect:
- `kernel/irq/` — interrupt handling, both trees heavily modified
- `arch/arm64/Kconfig` — vendor additions vs dovetail select
- `kernel/sched/` — scheduling changes
- `include/linux/` — irq and signal headers
- Qualcomm's power management (msm-pm) and remote proc may conflict

### Phase 3: Configure and Build
- Set `CONFIG_DOVETAIL=y` (enables interrupt pipeline)
- Set `CONFIG_XENOMAI=y` (enables cobalt core)
- Set `CONFIG_PREEMPT=y` (Dovetail works best with voluntary preempt off, full preempt on)
- Qualcomm-specific RT fixes from the reference layer:
  - Disable `BCL` driver (battery current limiting conflicts with RT)
  - Disable `MSM_DISPLAY` or patch to compile with RT

### Phase 4: Xenomai Userspace Recipe
- `scripts/prepare-kernel.sh` prepares the kernel tree (copies cobalt driver, sets up Kconfig)
- Yocto recipe calls prepare-kernel after applying dovetail patches
- Then compiles kernel + xenomai userspace together

### Phase 5: Yocto Layer Structure (model after meta-qcom-realtime)
```
meta-qcom-xenomai/
├── conf/
│   └── layer.conf
├── recipes-kernel/
│   └── linux/
│       ├── linux-qcom-xenomai_6.6.bb       # main recipe
│       ├── linux-qcom-xenomai/
│       │   ├── dovetail-patches/           # extracted dovetail patches for 6.6.119
│       │   │   └── *.patch
│       │   ├── qcom_rt_fixes/              # Qualcomm-specific RT fix patches
│       │   │   └── *.patch
│       │   ├── xenomai.cfg                 # CONFIG_DOVETAIL=y, CONFIG_XENOMAI=y, etc.
│       │   └── dovetail-patch.inc          # SRC_URI for dovetail patches
│       └── xenomai/
│           └── xenomai_3.3.bb              # Xenomai userspace recipe
├── recipes-extended/
│   └── ...                                  # procps, irqbalance bbappends
├── recipes-core/
│   └── ...                                  # init scripts tweaks
└── README.md
```

## Reference: meta-qcom-realtime layer structure

The reference uses `linux-qcom-base_6.6.bb` as the base (from qcom-manifest),
adds `patch-6.6.119-rt67.patch.gz` via `SRC_URI`, and applies RT config fragments.

Key files we studied:
- `recipes-kernel/linux/linux-qcom-base-rt_6.6.bb` — requires base recipe, adds RT
- `recipes-kernel/linux/linux-qcom-base-rt/rt-patch.inc` — patch URL
- `recipes-kernel/linux/linux-qcom-base-rt/configs/qcom_rt.cfg` — CONFIG_PREEMPT_RT=y
- `recipes-kernel/linux/linux-qcom-custom-rt/` — additional QCOM-specific RT fixes

## Relevant Links (from client)

| Resource | URL |
|----------|-----|
| Xenomai dovetail kernel | https://gitlab.com/Xenomai/linux-dovetail/-/tree/v6.6.y-dovetail |
| Xenomai stable/3.3.x | https://gitlab.com/Xenomai/xenomai3/xenomai/-/tree/stable/v3.3.x |
| QCOM downstream kernel | https://git.codelinaro.org/clo/la/kernel/qcom/-/tree/kernel.qclinux.1.0.r1-rel |
| QCOM manifest (quic-yocto) | https://github.com/quic-yocto/qcom-manifest/tree/qcom-linux-scarthgap |
| QCOM manifest mirror | https://github.com/qualcomm-linux/qcom-manifest/blob/qcom-linux-scarthgap/README.md |
| QCOM Linux build guide (doc 80-70029-254) | https://docs.qualcomm.com/doc/80-70029-254/topic/github_workflow_unregistered_users.html |
| meta-qcom-realtime | https://github.com/qualcomm-linux/meta-qcom-realtime |

## QCOM QLI 1.8 Build Context

### 官方 manifest 仓库 (quic-yocto)

Qualcomm 使用两个 GitHub org：
- `https://github.com/quic-yocto/qcom-manifest` — Qualcomm Innovation Center 主仓库
- `https://github.com/qualcomm-linux/qcom-manifest` — 公开镜像

QLI 1.8 内核版本为 **6.6.119**，已验证的 manifest 如下（来源 GitHub API）：

| Manifest | 说明 |
|----------|------|
| `qcom-6.6.119-QLI.1.8-Ver.1.0.xml` | 基础版本 |
| `qcom-6.6.119-QLI.1.8-Ver.1.1.xml` | **最新版本** |
| `qcom-6.6.119-QLI.1.8-Ver.1.1_realtime-linux-1.1.xml` | 含实时 Linux (PREEMPT_RT) |
| `qcom-6.6.119-QLI.1.8-Ver.1.1_qim-product-sdk-2.3.1.xml` | 含多媒体 SDK |
| `qcom-6.6.119-QLI.1.8-Ver.1.1_robotics-sdk-1.1.xml` | 含机器人 SDK |

### 标准构建流程 (Docker 方式)

参考文档 80-70029-254 和 Critical Link 验证的流程：

```bash
# === 步骤 1: 初始化 repo ===
repo init -u https://github.com/quic-yocto/qcom-manifest \
          -b qcom-linux-scarthgap \
          -m qcom-6.6.119-QLI.1.8-Ver.1.1.xml
repo sync

# === 步骤 2: 拉取 Docker 镜像 (Ubuntu 22.04 Yocto 环境) ===
docker pull crops/poky:ubuntu-22.04

# === 步骤 3: 启动 Docker 容器并构建 ===
docker run --rm -it -e LOCAL_USER_ID=`id -u $USER` \
           -v `pwd`:/work crops/poky:ubuntu-22.04 \
           --workdir=/work /bin/bash

# 在容器内：
export EXTRALAYERS="meta-qcom-realtime"  # 如需 RT 支持 (我们参考用)
MACHINE=qcs6490-rb3gen2-vision-kit \
  DISTRO=qcom-wayland \
  QCOM_SELECTED_BSP=custom \
  source setup-environment

bitbake qcom-multimedia-image
```

### 关键变量说明

| 变量 | 值 | 说明 |
|------|-----|------|
| `MACHINE` | `qcs6490-rb3gen2-vision-kit` | RB3 Gen2 Vision Kit, QCS6490 SoC, ARM64 |
| `DISTRO` | `qcom-wayland` | QCOM Wayland 发行版 |
| `QCOM_SELECTED_BSP` | `custom` | 使用 custom BSP (含多媒体/GPU)，默认即为此值 |
| `EXTRALAYERS` | 空格分隔的 layer 列表 | 构建前声明额外 Yocto 层 |

### 不使用 Docker 的替代方式

`setup-environment` 脚本位于 `layers/meta-qcom-hwe/scripts/`，也可以直接在宿主机运行：
```bash
MACHINE=qcs6490-rb3gen2-vision-kit DISTRO=qcom-wayland source setup-environment
bitbake qcom-multimedia-image
```
需提前安装 Yocto 所需的宿主机依赖包 (`gawk wget git diffstat unzip texinfo gcc build-essential chrpath socat cpio python3 ...`)。

### 硬件需求

- 磁盘空间：~120 GB
- 构建时间：2-10 小时（取决于机器性能）
- 验证过的宿主机 OS：Ubuntu 24.04 / 22.04

## Dovetail vs PREEMPT_RT — Key Differences

| | PREEMPT_RT | Dovetail + Xenomai |
|--|-----------|--------------------|
| Approach | Make Linux itself real-time | Interrupt pipeline + co-kernel |
| Latency | ~20-50 µs | ~5-20 µs (better) |
| User API | Standard POSIX + RT threads | Xenomai skins (Alchemy, POSIX, VxWorks) |
| Kernel drivers | Normal Linux drivers need RT conversion | Dedicated RTDM (Real-Time Driver Model) |
| Complexity | Patches the kernel directly | Interrupt stage between hardware and Linux |

## Work Directory Layout

```
~/work/2026/xenomai/
├── CLAUDE.md              ← this file
├── linux-dovetail/         ← dovetail kernel tree (6.6.69 + dovetail)
├── qcom/                   ← Qualcomm downstream kernel (6.6.119)
├── xenomai/                ← Xenomai 3.3.x userspace + kernel cobalt
├── meta-qcom-realtime/     ← reference Yocto layer
├── patches-dovetail/       ← [to generate] extracted dovetail patches
├── build/                  ← [to create] yocto build output
└── meta-qcom-xenomai/      ← [to create] our deliverable Yocto layer
```

## My Role (Claude Code / Claude in this project)

- Linux kernel expert, experienced in cross-kernel merging, interrupt subsystems,
  SoC BSP porting (Rockchip RK3576), device tree, kernel debugging
- Familiar with Qualcomm-adjacent patterns (remoteproc, IPA, BCL, msm-pm)
- Have worked with Yocto layers and bitbake recipes
- This is a commercial engagement — we deliver working code, not research
- Speed matters: prioritize pragmatic solutions over perfection
- Richard 读中文回复，可能用英文提问；我用中文回答，代码和命令保持英文

## Phase 1 Analysis (2026-05-08) — Dovetail 补丁覆盖范围

### 数据概览

- 基线: `a30cd70ab75a` (v6.6.69) → HEAD: `8e9ce988b9b4`
- 171 个非 merge commit，**399 文件**，+16,169 / -1,483 行
- patches-dovetail/ 中: 171 个 commit patch + 8 个 merge ref + 1 个 combined diff

### Dovetail 的四层架构（由上到下）

```
Layer 4: 用户态 API (UAPI)
  └─ include/uapi/asm-generic/dovetail.h, include/uapi/linux/clocksource.h

Layer 3: Dovetail 交替调度 (oob 域入口/出口)
  └─ kernel/dovetail.c (+463 NEW), kernel/entry/common.c (+180)
  └─ include/linux/dovetail.h (+343 NEW)

Layer 2: IRQ Pipeline (两阶段中断管道：in-band / out-of-band)
  └─ kernel/irq/pipeline.c (+1909 NEW) + chip.c (+516)
  └─ include/linux/irq_pipeline.h + irqstage.h + spinlock_pipeline.h
  └─ kernel/locking/pipeline.c (+231 NEW) — pipeline-aware 锁

Layer 1: 架构适配 (ARM64/ARM/x86 的 syscall/trap/fpu 重路由)
  └─ arch/*/kernel/{entry-*, irq_pipeline.c, signal, smp, traps}
```

### 核心新文件 (32 个)

| 类别 | 文件 |
|------|------|
| 头文件 | `include/{linux/irqstage.h (+368), dovetail.h (+343), spinlock_pipeline.h (+387), irq_pipeline.h}` |
| | `include/dovetail/{irq,mm_info,net,netdevice,poll,spinlock,thread_info}.h` |
| | `arch/{arm,arm64,x86}/include/asm/{dovetail.h, irq_pipeline.h}` |
| 实现 | `kernel/dovetail.c` (+463), `kernel/irq/pipeline.c` (+1909), `kernel/time/tick-proxy.c` (+466) |
| | `kernel/locking/pipeline.c` (+231), `kernel/irq/irqptorture.c` (+326) |
| | `arch/{arm,arm64,x86}/kernel/irq_pipeline.c` |

### 为 Phase 2 准备：高冲突风险区域

在 QCOM 6.6.119 上打 dovetail 补丁时，以下文件最可能冲突：

| 优先级 | 文件 | 原因 |
|--------|------|------|
| 🔴🔴🔴 | `arch/arm64/kernel/entry-common.c` (+274) | 系统调用入口重路由，QCOM BSP 可能修改 syscall 逻辑 |
| 🔴🔴🔴 | `arch/arm64/kernel/fpsimd.c` (+216) | FP/SIMD 交替调度，QCOM 可能有 vendor fp 改动 |
| 🔴🔴 | `arch/arm64/kernel/smp.c` (+108) | SMP IPI 扩展，可能和 QCOM msm-pm 相关 |
| 🔴🔴 | `kernel/sched/core.c` (+326) | 调度器 oob awareness，两棵树都重度修改 |
| 🔴🔴 | `kernel/irq/chip.c` / `manage.c` | 中断子系统，冲突高发区 |
| 🔴 | `drivers/pinctrl/pinctrl-msm.c` | dovetail 已改（patch 0033），QCOM BSP 也有改动 |
| 🔴 | `drivers/spmi/spmi-pmic-arb.c` | QCOM PMIC 仲裁器（patch 0034），同样双向改动 |
| 🔴 | `kernel/printk/printk.c` | QCOM 可能有自己的 logging 扩展 |
| 🟡 | `include/linux/` 多个头文件 | 宏冲突，通常容易解决（加 `#ifdef CONFIG_DOVETAIL`） |

### 概念理解进度

- [x] IRQ pipeline — 两阶段中断管道，oob → in-band
- [x] Dovetail — 在 pipeline 上实现交替调度
- [x] OOB — 高优先级的"带外"执行域
- [x] RTnet vs dovetail oob 网络的两条路径
- [x] 改了什么，哪些文件高风险
- [ ] git am 冲突处理实战经验（Step 1 中获取）
- [ ] QCOM-specific 适配判断力（Step 2-3 中获取）

→ 操作指南见 **[STEP-BY-STEP.md](STEP-BY-STEP.md)**

---

## Current State (2026-05-11)

- [x] All repos cloned locally
- [x] Versions identified: dovetail 6.6.69, qcom 6.6.119
- [x] Reference layer (meta-qcom-realtime) analyzed
- [x] **Phase 1 done**: 提取 Dovetail 补丁 (171 patches + 8 merge refs + 1 combined diff)
- [x] **远程访问就绪**: RB3 Gen2 Vision Kit 远程 ADB 可用，见 [REMOTE-ACCESS.md](REMOTE-ACCESS.md)
- [x] **板子已确认**: Vision Kit (不是 Core Kit), QIMP SDK, 当前内核 6.6.28
- [ ] **Step 0**: linux-dovetail x86_64 + Xenomai 基线验证 (在 QEMU 跑 latency)
- [ ] **Step 1**: qcom x86_64 练手 (熟悉 git am 冲突处理)
- [ ] **Step 2**: qcom ARM64 正式打补丁
- [ ] **Step 3**: 编译通过 + QCOM 特有适配
- [ ] **Step 4**: Yocto layer 化

### 远程板子信息

| 项目 | 值 |
|------|-----|
| 型号 | RB3 Gen2 **Vision Kit** (带 Vision Mezzanine + 双摄像头) |
| SDK | **QIMP** (Qualcomm Intelligent Multimedia Product) |
| DISTRO | `qcom-wayland 1.0` |
| 当前内核 | `6.6.28-01890-g350dfd604d2f` (2024-06-19, PREEMPT) |
| MACHINE (Yocto) | `qcs6490-rb3gen2-vision-kit` |
| USB 访问 | 05c6:9135, 3 interfaces (DIAG + QMI + ADB), 无 CDC ACM 串口 |
| 串口 console | **暂缺**, 需物理 UART 线 → 向客户申请 |

→ 详细操作命令见 **[STEP-BY-STEP.md](STEP-BY-STEP.md)**
