#!/bin/bash
# Launch QEMU ARM64 with Dovetail + Xenomai kernel, drop to interactive shell
# Kernel: QCOM 6.6.119 + Dovetail + Xenomai Cobalt
#
# Dependencies (not in git - too large):
#   QEMU binary:  /tmp/qemu-arm-extracted/usr/bin/qemu-system-aarch64
#   Kernel Image: ../qcom/arch/arm64/boot/Image
#   Initramfs:    ../tmp/test-fix-shell.cpio.gz
#
# In-guest tools: timer-diag, latency
#
# Test artifacts: ../tmp/logs/       (QEMU output logs)
#                 ../tmp/test-init/  (historic init scripts)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

QEMU="/tmp/qemu-arm-extracted/usr/bin/qemu-system-aarch64"
KERNEL="$PROJECT_DIR/qcom/arch/arm64/boot/Image"
INITRD="$PROJECT_DIR/tmp/test-fix-shell.cpio.gz"

echo "=== Launching QEMU ARM64 (Dovetail + Xenomai) ==="
echo "Kernel: $KERNEL ($(stat -c%s "$KERNEL") bytes)"
echo "Initrd: $INITRD ($(stat -c%s "$INITRD") bytes)"
echo ""
echo "Boot will drop to shell. Kill QEMU with Ctrl-A X or close terminal."
echo ""

exec "$QEMU" \
  -M virt \
  -cpu cortex-a57 \
  -smp 1 \
  -m 1G \
  -kernel "$KERNEL" \
  -initrd "$INITRD" \
  -append "console=ttyAMA0" \
  -nographic \
  -no-reboot
