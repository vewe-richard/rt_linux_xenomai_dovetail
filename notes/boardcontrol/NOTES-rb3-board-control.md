# RB3 Gen2 Board Control Notes (2026-05-12)

## 连接信息

```bash
ssh rb3@45.80.139.198 -p 5522  # pwd: rb3
sudo密码: rb3
```

## 板子 USB 模式

| PID | 模式 | 协议 | 说明 |
|-----|------|------|------|
| `05c6:9135` | Normal | ADB | 正常启动后，可用 adb shell |
| `05c6:9008` | QDL/EDL | Sahara | 强制下载模式，qdl 需要此模式烧录 |
| `05c6:900e` | QDLOAD | Firehose | Firehose programmer 加载后的模式 |

## 串口 Console

```bash
# 设备路径、波特率、权限
/dev/ttyACM0  115200
sudo chmod 666 /dev/ttyACM0   # 修复权限

# 读取串口（CR→LF 转换很重要！）
stty -F /dev/ttyACM0 115200
cat /dev/ttyACM0 | tr "\r" "\n"

# 持续监控
timeout 60 cat /dev/ttyACM0 | tr "\r" "\n"
```

**重要发现**: 板子在 QDL 模式 (9008) 时，串口**无任何输出**（0 字节）。
只有板子退出 QDL 开始启动流程后，串口才会输出 XBL / UEFI / Linux 日志。

## Relay 控制（电源 + F_DL 按钮）

```bash
# 电源开/关
curl "http://10.0.198.19/relay/1?turn=on"
curl "http://10.0.198.19/relay/1?turn=off"

# F_DL 按下/松开（进 QDL 模式需按下后上电）
curl "http://10.0.198.20/relay/0?turn=on"
curl "http://10.0.198.20/relay/0?turn=off"
```

**验证方法**: 断电后串口输出停止，上电后恢复。串口时间戳会出现明显跳跃。

## QDL 操作流程

### qdl 工具

```bash
# qdl 需要 9008 模式
cd /home/rb3/jiang/target/qcs6490-rb3gen2-core-kit/qcom-multimedia-image/

# 完整命令（必须含所有 rawprogram + patch XML）
sudo qdl --debug --storage ufs prog_firehose_ddr.elf \
  rawprogram0.xml rawprogram1.xml rawprogram2.xml rawprogram3.xml rawprogram4.xml rawprogram5.xml \
  patch0.xml patch1.xml patch2.xml patch3.xml patch4.xml patch5.xml
```

### 运行 qdl 前必须解绑 qcserial 驱动

qdl 需要独占 USB 设备。板子在 9008 模式时，`qcserial` 驱动会自动绑定到接口上，必须解绑：

```bash
# 检查驱动绑定
ls -la /sys/bus/usb/devices/5-1:1.0/driver
# → 如果是 qcserial，需要解绑

# 解绑接口驱动（注意：是接口级别 5-1:1.0，不是设备级别 5-1）
echo "rb3" | sudo -S sh -c "echo -n '5-1:1.0' > /sys/bus/usb/drivers/qcserial/unbind"
```

**教训**: 先解绑驱动再运行 qdl，让 qdl 自己处理 USB 设备访问。不要手动用 pyusb 操作 USB 设备，容易让设备进入异常状态。

### 添加 udev 规则（可选，但建议）

```bash
# 让 plugdev 组可以访问 QDL 设备
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="05c6", ATTR{idProduct}=="9008", MODE="0666", GROUP="plugdev"' \
  | sudo tee /etc/udev/rules.d/52-qdl.rules
sudo udevadm control --reload-rules
```

## 关键工具：usbreset

```bash
# 重置指定的 USB 设备（按 VID:PID）
sudo usbreset 05c6:9008
sudo usbreset 05c6:900e
```

**重大发现**: `usbreset` 可以改变板子的 USB 模式！
- 第一次 `usbreset 05c6:9008` → 设备变为 `05c6:900e`
- 第二次 `usbreset 05c6:900e` → 设备从 USB bus **完全消失**（板子进入 boot loop）

**警告**: usbreset 在 900e 模式使用可能导致板子 USB PHY 异常，此后 USB 枚举持续 EPROTO (-71)。
此时即使 relay 断电 3 分钟也无法恢复，需要物理重插 USB 线。

## 板子状态快速检查

```bash
# 1. USB 状态
lsusb | grep 05c6
lsusb -t | grep -B1 -A1 "5-"

# 2. 串口输出（判断是否在 boot loop）
timeout 5 cat /dev/ttyACM0 | tr "\r" "\n" | head -3

# 3. Host dmesg（USB 枚举错误）
sudo dmesg | grep "usb 5-1" | tail -5
```

## 当前问题（2026-05-12 未解决）

板子处于 boot loop 状态：串口循环输出 `B - ... - usb: SUPER, 0x900e`
Host USB 枚举失败：`device not accepting address XX, error -71 (EPROTO)`

可能原因：usbreset 导致 USB PHY 进入异常状态。
可能修复：板子物理断电（拔 USB 线或拔电源），或重启 Host 机器。

## 有用的诊断命令备忘

```bash
# USB 设备 sysfs 接口状态
ls -d /sys/bus/usb/devices/5-1:1.*/
cat /sys/bus/usb/devices/5-1/bConfigurationValue

# USB 总线扫描
ls /sys/bus/usb/devices/5-1/

# 检查哪个驱动绑定到接口
ls -la /sys/bus/usb/devices/5-1:1.0/driver

# Host 侧 xhci 重置（极端手段）
echo "rb3" | sudo -S sh -c "echo -n '0000:00:14.0' > /sys/bus/pci/drivers/xhci_hcd/unbind"
echo "rb3" | sudo -S sh -c "echo -n '0000:00:14.0' > /sys/bus/pci/drivers/xhci_hcd/bind"
```
