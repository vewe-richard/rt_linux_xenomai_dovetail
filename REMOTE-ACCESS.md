# RB3 Gen2 Remote Access Guide

## Host

SSH to: `rb3@rb3dev` (remote Linux machine with RB3 Gen2 connected via USB)
ssh rb3@45.80.139.198 -p 5522  #pwd: rb3

## Power Control (Relay Cards)

```bash
# Power ON
curl "http://10.0.198.19/relay/1?turn=on"

# Power OFF
curl "http://10.0.198.19/relay/1?turn=off"
```

## F_DL Button (Force Download / QDL Mode)

```bash
# Press F_DL
curl "http://10.0.198.20/relay/0?turn=on"

# Release F_DL
curl "http://10.0.198.20/relay/0?turn=off"
```

## Verify USB Connection After Power On

Board appears on Bus 5 as `05c6:9135` with 3 interfaces (DIAG / QMI / ADB):

```bash
lsusb -t
lsusb | grep Qualcomm
```

Expected: `Bus 005 Device XXX: ID 05c6:9135 Qualcomm, Inc. QCM6490_5e90b593`

## ADB Access

The USB gadget composition does NOT include CDC ACM serial, so no `/dev/ttyUSB*`.
Use ADB instead:

### Setup (one-time)

```bash
sudo apt install adb

sudo tee /etc/udev/rules.d/51-android.rules << 'EOF'
SUBSYSTEM=="usb", ATTR{idVendor}=="05c6", ATTR{idProduct}=="9135", MODE="0666", GROUP="plugdev"
EOF
sudo udevadm control --reload-rules
```

### Connect (every session)

```bash
# Optional: USB power cycle if device not showing
echo 0 | sudo tee /sys/bus/usb/devices/5-1/authorized
sleep 2
echo 1 | sudo tee /sys/bus/usb/devices/5-1/authorized

# Restart adb daemon (fixes "no permissions" after re-plug)
adb kill-server
sudo adb start-server

# Verify & connect
adb devices          # Should show: 5e90b593   device
adb shell            # Enter board shell
```

## Board Info (verified 2026-05-11)

| Item | Value |
|------|-------|
| Kit type | **Vision Kit** (not Core Kit) |
| SDK | **QIMP** (Qualcomm Intelligent Multimedia Product) |
| SoC | QCS6490 |
| Machine (Yocto) | `qcs6490-rb3gen2-vision-kit` |
| DISTRO | `qcom-wayland 1.0` |
| Current kernel | `6.6.28-01890-g350dfd604d2f` (2024-06-19, PREEMPT) |
| USB ID | `05c6:9135` |
| USB interfaces | DIAG (ff/30) + QMI (ff/70) + ADB (42/01) — no CDC ACM serial |
| Serial console | **Missing**, need physical UART cable |

To identify on a new board:

```bash
adb shell cat /proc/device-tree/model
# → "Qualcomm Technologies, Inc. Robotics RB3gen2 addons vision mezz platform"

adb shell cat /etc/build | grep qim
# → meta-qcom-qim-product-sdk ... (confirms QIMP, not QIRP)

adb shell uname -a
# Check kernel version
```

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| Bus 5 empty | RB3 not powered | Power ON relay |
| Device visible but Driver=[none] | Normal, no kernel driver needed | Use ADB, not a bug |
| `adb devices` shows "no permissions" | Missing udev rules or stale daemon | Add udev rules + `adb kill-server && sudo adb start-server` |

## USB Port Power Cycle (Last Resort)

If the board is stuck and won't come out of QDL mode:

```bash
curl "http://10.0.198.20/relay/0?turn=off"   # Release F_DL
curl "http://10.0.198.19/relay/1?turn=off"   # Power off
echo 0 | sudo tee /sys/bus/usb/devices/5-1/authorized   # Disable USB port
echo 1 | sudo tee /sys/bus/usb/devices/5-1/authorized   # Re-enable USB port
```
