# Physical Laptop Boot — Dovetail + Xenomai Cobalt (2026-05-16)

## Kernel Info

- **Image**: `/home/richard/work/2026/xenomai/qcom/arch/x86/boot/bzImage` (13 MB)
- **Version**: `6.6.119-06374-g08cea05b41c8-dirty`
- **Config**: `CONFIG_DOVETAIL=y`, `CONFIG_XENOMAI=y`, `CONFIG_IRQ_PIPELINE=y`
- **Key drivers** (all built-in): SATA_AHCI, BLK_DEV_SD, EXT4_FS, R8169

## Boot Steps (run before reboot)

```bash
# 1. Install modules
sudo make -C /home/richard/work/2026/xenomai/qcom modules_install

# 2. Copy kernel + System.map + config
sudo cp /home/richard/work/2026/xenomai/qcom/arch/x86/boot/bzImage /boot/vmlinuz-6.6.119-dovetail
sudo cp /home/richard/work/2026/xenomai/qcom/System.map /boot/System.map-6.6.119-dovetail
sudo cp /home/richard/work/2026/xenomai/qcom/.config /boot/config-6.6.119-dovetail

# 3. Verify modules directory name
ls /lib/modules/ | grep 6.6
# → use the exact name for the next step

# 4. initramfs + GRUB
sudo update-initramfs -c -k $(ls /lib/modules/ | grep 6.6.119)
sudo update-grub

# 5. Reboot
sudo reboot
```

## After Reboot — Verify

```bash
uname -r
# → should show 6.6.119-...-dirty

dmesg | grep -i "dovetail\|xenomai\|cobalt"
# Expect: IRQ pipeline: high-priority Xenomai stage added
#         [Xenomai] Cobalt v3.3.x

cat /proc/xenomai/latency
```

## Latency Test (needs Xenomai userspace)

```bash
cd ~/work/2026/xenomai/xenomai
./configure --with-core=cobalt --enable-smp
make -j$(nproc)
sudo make install
sudo ldconfig

sudo /usr/xenomai/bin/latency
```

## Recovery — If Boot Fails

1. Reboot (Ctrl+Alt+Del or hold power)
2. Hold **Shift** (or **ESC**) at boot to enter GRUB menu
3. Select `Advanced options for Ubuntu` → `Ubuntu, with Linux 6.11.0-24-generic`
4. System boots normally with old kernel
5. To remove the broken entry (optional):
   ```bash
   sudo rm /boot/vmlinuz-6.6.119-dovetail
   sudo rm /boot/System.map-6.6.119-dovetail
   sudo rm /boot/config-6.6.119-dovetail
   sudo rm -rf /lib/modules/6.6.119-*
   sudo update-grub
   ```

## Current Session State (to resume after boot)

- **Branch**: `dovetail-integration` in `/home/richard/work/2026/xenomai/qcom`
- **Status**: Step 1 complete, x86_64 + DOVETAIL + XENOMAI compiles
- **Next**: Step 2 ARM64 cross-compile for RB3 Gen2
- **Patches deliverable**: `/home/richard/work/2026/xenomai/patches-deliverable/0515/` (174 patches + README)
- **Conflict log**: `/home/richard/work/2026/xenomai/notes/step1/conflict-log.md`

## Xenomai Repo (notes + project management)

```bash
cd ~/work/2026/xenomai
git log --oneline -3
# 9d5e673 doc: update Step 1 notes...
# 1989c26 plan for step1
# ...
```

## QCOM Kernel Repo

```bash
cd ~/work/2026/xenomai/qcom
git log --oneline -5
# 6b09f53 xenomai: add Cobalt core via prepare-kernel.sh
# 08cea05 drm: of: fix syntax error...
# a89ef6f x86: dovetail: fix build errors...
# 766eb82 arm64: dovetail: fix some fault exit paths
# aa68072 dovetail: net: rename oob_data...
```
