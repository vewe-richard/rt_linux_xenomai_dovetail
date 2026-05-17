# Dovetail + Xenomai Patches for QCOM 6.6.119 (2026-05-17)

## Baseline

| Item | Value |
|------|-------|
| Base repo | `git.codelinaro.org/clo/la/kernel/qcom` |
| Base tag | `kernel.qclinux.1.0.r1-rel` |
| Base commit | QCOM QLI 1.8 — Linux 6.6.119 |
| Target branch | `dovetail-integration` |
| Patches count | 176 |

## Patch Breakdown

| Range | Count | Source |
|-------|-------|--------|
| 0001–0171 | 171 | Dovetail v6.6.y-dovetail (linux-dovetail) |
| 0172 | 1 | x86_64 build fix (irqflags.h, vector.c) |
| 0173 | 1 | QCOM BSP syntax fix (drm_of.h) |
| 0174 | 1 | Xenomai Cobalt integrate — x86_64 (prepare-kernel.sh) |
| 0175 | 1 | Xenomai Cobalt integrate — ARM64 + qcom_scm_trace.h fix |
| 0176 | 1 | IRQ pipeline fix — deferred IRQ replay + QCOM GIC enforce_irqctx |

## Manual Fixes Applied (included in patches above)

| Patch | File | Issue | Fix |
|-------|------|-------|-----|
| 0172 | arch/x86/include/asm/irqflags.h | Dovetail + QCOM irqflags conflict | Replaced with dovetail version |
| 0172 | arch/x86/kernel/apic/vector.c | DECLARE_X86_CLEANUP_WORKER API change | Use queue_cleanup_work() macro |
| 0173 | include/drm/drm_of.h | `;` instead of `)` in function declaration | Fix syntax |
| — | drivers/pci/pci.h | static function not inline → -Werror | Add `inline` keyword |
| 0175 | drivers/firmware/qcom_scm_trace.h | TRACE_INCLUDE_PATH=. resolves to wrong dir | Changed to `../../drivers/firmware` |
| 0176 | kernel/irq/irqdesc.c | deferred IRQ replay fails QCOM GIC enforce_irqctx | is_hardirq(): check in_hardirq() during replay |
| 0176 | kernel/irq/chip.c | get_flow_step() wrong PILEUP vs REPLAY | Use hard_irqs_disabled() to distinguish |
| 0176 | kernel/irq/chip.c | handle_percpu_devid_irq() misses irq_clear_deferral | Use should_feed_pipeline() instead of may_start_flow() |

## How to Apply

```bash
cd qcom-kernel                    # QCOM 6.6.119 tree
git checkout kernel.qclinux.1.0.r1-rel
git checkout -b dovetail-integration

for p in patches-deliverable/0517/*.patch; do
  git am --3way "$p" || { echo "CONFLICT: $p"; break; }
done
```

## After Applying Patches

1. **Run prepare-kernel.sh** (fixes symlinks for your local paths):
   ```bash
   ./scripts/prepare-kernel.sh --linux=. --arch=arm64 --verbose
   ```

2. **Configure and build**:
   ```bash
   make ARCH=arm64 defconfig
   ./scripts/config --enable CONFIG_IRQ_PIPELINE
   ./scripts/config --enable CONFIG_DOVETAIL
   ./scripts/config --enable CONFIG_XENOMAI
   make ARCH=arm64 olddefconfig
   make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc)
   ```

## Verification

| Platform | Status | Notes |
|----------|--------|-------|
| x86_64 QEMU | Verified | latency 3350ns idle, demo apps pass |
| ARM64 cross-compile | Verified | Compiles with dovetail/cobalt enabled |
| ARM64 QEMU virt | Verified | arch_timer PPI works, latency ~300µs (TCG) |
| ARM64 RB3 Gen2 | Pending | Needs board access |

## Previous Versions

- `patches-deliverable/0516/` — 175 patches, first ARM64 Cobalt integration
- `patches-deliverable/0515/` — 174 patches, x86_64 only
