# Update Kernel on RB3 Gen2 Without Full Reflash

## Where the Kernel Actually Lives

```
sda7 (512MB, FAT32, label "efi") — EFI System Partition (ESP)
├── EFI/
│   ├── Linux/uki.efi           ← ⭐ 内核在这里 (UKI: kernel + systemd-stub + DTB)
│   └── BOOT/bootaa64.efi       ← systemd-boot
├── dtb/
│   └── qcs6490-rb3gen2-vision-mezz.dtb (325KB)
├── loader/
│   └── loader.conf             ← 空文件，systemd-boot 自动发现 /EFI/Linux/*.efi
└── ...

⚠️  UEFI 不从 /boot/Image 加载内核！
    /rootfs /boot/ 中的文件只是构建遗留，boot 流程不使用它。
```

## UKI Format

`uki.efi` 是一个 PE/COFF executable，包含多个 section：
- `.text` / `.stub` — systemd-stub (EFI stub loader)
- `.linux` — kernel Image (34MB)
- `.dtb` — device tree blob
- `.cmdline` — kernel command line

ARM64 kernel Image 自带 EFI stub (MZ + PE header)，可以作为独立 EFI 应用运行。

## Replace Kernel

### Pre-requisite: Build kernel with these configs

```
CONFIG_EFI_STUB=y           # already enabled in 6.6.28
CONFIG_EFI=y
CONFIG_ARM64_APPENDED_DTB=y  # optional: embeds DTB in Image
```

### Method 1: Replace the entire ESP partition image

On the build server, mount `efi.bin`, replace contents, then `dd` to board:

```bash
# On rb3dev server:
mkdir -p /tmp/esp
mount -o loop /home/rb3/jiang/target/qcs6490-rb3gen2-core-kit/qcom-multimedia-image/efi.bin /tmp/esp

# Replace kernel Image
cp new-Image /tmp/esp/EFI/Linux/linux-dovetail.efi

# Or rebuild uki.efi with ukify (when available)
# ukify build --linux=new-Image --dtb=new-dtb --cmdline=@cmdline.txt -o uki.efi

umount /tmp/esp
```

Then flash to board:
```bash
adb push efi.bin /var/volatile/
adb shell "dd if=/var/volatile/efi.bin of=/dev/sda7 bs=4M"
adb reboot
```

### Method 2: Mount ESP on board and replace files

```bash
adb shell

# Mount ESP read-write
mkdir -p /tmp/esp
mount -o remount,rw /dev/sda7 /tmp/esp 2>/dev/null || mount /dev/sda7 /tmp/esp

# Option A: Replace uki.efi with new kernel Image directly
cp /var/volatile/new-Image /tmp/esp/EFI/Linux/uki.efi

# Option B: Add as separate entry (systemd-boot auto-discovers)
cp /var/volatile/new-Image /tmp/esp/EFI/Linux/linux-new.efi

# Replace DTB if needed
cp /var/volatile/new-dtb /tmp/esp/dtb/qcs6490-rb3gen2-vision-mezz.dtb

umount /tmp/esp
sync
reboot
```

### Method 3: Build proper UKI (future, needs systemd-ukify)

```bash
ukify build \
  --stub=/usr/lib/systemd/boot/efi/linuxaa64.efi.stub \
  --linux=Image \
  --dtb=qcs6490-rb3gen2-vision-mezz.dtb \
  --cmdline=@kernel-cmdline.txt \
  -o uki.efi
```

### Method 4: Append DTB to kernel Image (no ukify needed)

```bash
# Build kernel with CONFIG_ARM64_APPENDED_DTB=y, or append manually:
cat Image qcs6490-rb3gen2-vision-mezz.dtb > Image+dtb
# Copy to ESP and boot
```

## Kernel Modules

```bash
adb push kernel-modules.tgz /var/volatile/
adb shell "tar xzf /var/volatile/kernel-modules.tgz -C /lib/modules/"
```

## Recovery

- Replace `/boot/Image` on rootfs: **does nothing** (UEFI ignores it)
- Power cycle via relay: restores to known-good if ESP wasn't touched
- If ESP was modified and fails: power off + F_DL → QDL mode → reflash original firmware
- Last resort: USB port power cycle

## Reference: Current Board

| Item | Value |
|------|-------|
| Kit | Vision Kit |
| ESP partition | sda7 (512MB FAT32) |
| UKI path | `/EFI/Linux/uki.efi` (41MB) |
| Kernel version inside UKI | `6.6.28-01890-g350dfd604d2f` |
| Kernel offset in ESP | ~27MB (inside uki.efi) |
| DTB path | `/dtb/qcs6490-rb3gen2-vision-mezz.dtb` (325KB) |
| systemd-boot | `/EFI/BOOT/bootaa64.efi` |
| loader.conf | empty (auto-discover) |
| Build ESP image | `/home/rb3/jiang/target/qcs6490-rb3gen2-core-kit/qcom-multimedia-image/efi.bin` (512MB) |
