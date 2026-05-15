# Dovetail + Xenomai Integration Patches — QCOM 6.6.119

**Date**: 2026-05-15
**Branch**: `dovetail-integration`
**Source**: QCOM 6.6.119 + 171 Dovetail patches + 3 build fixes + Xenomai Cobalt prepare-kernel

## Baseline

| Item | Value |
|------|-------|
| Base kernel | QCOM `kernel.qclinux.1.0.r1-rel` (v6.6.119) |
| QCOM repo | https://git.codelinaro.org/clo/la/kernel/qcom.git |
| Baseline commit | `380a343250d3` (branch `kernel.qclinux.1.0.r1-rel`) |
| Dovetail source | linux-dovetail v6.6.69 (https://gitlab.com/Xenomai/linux-dovetail/-/tree/v6.6.y-dovetail) |
| Xenomai source | stable/v3.3.x (https://gitlab.com/Xenomai/xenomai3/xenomai) |

## Patch Series Overview

| # | Description |
|---|-------------|
| 0001-0005 | Raw console channel (printk, ARM, tty) |
| 0006-0019 | IRQ pipeline core (genirq, locking, time) |
| 0020-0053 | IRQ pipeline — arch: ARM |
| 0054-0076 | Dovetail alternate scheduling core + ARM/ARM64 arch |
| 0077-0086 | IRQ pipeline + Dovetail — arch: x86 |
| 0087-0092 | DMA engine + SPI — oob support |
| 0093-0094 | OOB open mode + OOB networking |
| 0095-0131 | IRQ pipeline fixes, networking, KVM, syscall handling |
| 0132-0150 | HV guest, u64_stats, BPF, sched, networking oob I/O |
| 0151-0171 | Networking cleanup, page_pool, clocksource, fixups |
| 0172-0173 | Build fixes for x86_64 CONFIG_IRQ_PIPELINE=y + DRM |
| 0174 | Xenomai Cobalt core (prepare-kernel.sh output) |

## Prerequisites

```bash
# 1. Clone QCOM downstream kernel
git clone https://git.codelinaro.org/clo/la/kernel/qcom.git
cd qcom

# 2. Checkout the exact baseline
git checkout -b dovetail-integration kernel.qclinux.1.0.r1-rel

# 3. Configure git for 3-way merge (helps auto-resolve conflicts)
git config merge.conflictstyle diff3
```

## Apply Patches

```bash
cd qcom

# Apply all patches in order
git am --3way /path/to/0515/*.patch

# → Expect all 174 patches to apply cleanly.
#   If any fail, see "Troubleshooting" below.
```

## Post-Apply: Xenomai Cobalt

The last patch (0174) contains `prepare-kernel.sh` output — symlinks to Xenomai source.
To finalize the Xenomai integration:

```bash
# Point to your Xenomai 3.3.x source tree
/path/to/xenomai/scripts/prepare-kernel.sh \
    --linux=$(pwd) \
    --arch=arm64

# This resolves the symlinks and finalizes Kconfig integration.
```

## Build — x86_64 (QEMU Verification)

```bash
cd qcom
make x86_64_defconfig
./scripts/config --enable CONFIG_IRQ_PIPELINE
./scripts/config --enable CONFIG_DOVETAIL
./scripts/config --enable CONFIG_XENOMAI
make olddefconfig
make -j$(nproc)
```

## Build — ARM64 (RB3 Gen2 Target)

```bash
cd qcom
# Use QCOM's defconfig for the RB3 Gen2 Vision Kit
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- qcom_defconfig
# Or use the default: make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig

# Enable Dovetail + Xenomai
./scripts/config --enable CONFIG_IRQ_PIPELINE
./scripts/config --enable CONFIG_DOVETAIL
./scripts/config --enable CONFIG_XENOMAI
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig

# Build
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc)
```

## Verify at Runtime

```bash
dmesg | grep -i "dovetail\|xenomai\|cobalt"
# Expected output:
#   IRQ pipeline: high-priority Xenomai stage added
#   [Xenomai] Cobalt v3.3.x

cat /proc/xenomai/latency
/usr/xenomai/bin/latency
```

## Troubleshooting

If `git am` fails on a patch:

```bash
# See the failing diff
git am --show-current-patch=diff

# Manual resolution:
# 1. Edit conflicting files
# 2. git add <resolved-files>
# 3. git am --continue

# Or skip (NOT recommended — must revisit):
# git am --skip
```

## File Sizes

- Patches (uncompressed): ~2.0 MB (174 files)
- Compressed archive: ~400 KB
- Full QCOM repo `.git/`: ~2.8 GB (NOT included — the QCOM history comes from codelinaro.org)

## Contact

Meyer Elektronik & IT (Switzerland) — Jonathan
Project: Xenomai/Dovetail port to Qualcomm RB3 Gen2 Vision Kit (QCS6490)
