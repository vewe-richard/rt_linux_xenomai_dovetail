#!/bin/bash
# QEMU test helper for Xenomai/Dovetail kernel testing
# Usage: ./qemu-test.sh [kernel-path]
#   Default kernel: /home/richard/prepare/kernel/linux-6.10.2/vmlinux
#
# Step 0 baseline test:
#   ./qemu-test.sh /home/richard/work/2026/xenomai/linux-dovetail/arch/x86/boot/bzImage

KERNEL="${1:-/home/richard/prepare/kernel/linux-6.10.2/vmlinux}"
IMAGE="/home/richard/work/images/ubuntu/server/image-8G.img"

echo "Starting QEMU with kernel: $KERNEL"
echo "SSH: ssh -p 10003 root@localhost"

sudo qemu-system-x86_64 \
  -enable-kvm -nographic -m 8192 -smp 4 \
  -kernel "$KERNEL" \
  -append "root=/dev/sda1 rw console=ttyS0" \
  -drive "file=$IMAGE,format=raw" \
  -netdev "user,id=net0001,hostfwd=tcp::10003-:22,hostfwd=tcp::10004-:80,hostfwd=tcp::10005-:443,hostfwd=tcp::10006-:8000" \
  -device "e1000,netdev=net0001,mac=52:54:98:76:00:03"
