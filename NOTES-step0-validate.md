# Step 0 验证笔记 (2026-05-09)

## 编译流程

```bash
# 0.1 — integrate Xenomai cobalt into kernel tree
cd ~/work/2026/xenomai
~/work/2026/xenomai/xenomai/scripts/prepare-kernel.sh \
  --linux=~/work/2026/xenomai/linux-dovetail \
  --arch=x86 \
  --verbose

# 0.2 — configure with dovetail options
cd ~/work/2026/xenomai/linux-dovetail
make x86_64_defconfig
./scripts/config --enable CONFIG_IRQ_PIPELINE
./scripts/config --enable CONFIG_DOVETAIL
./scripts/config --enable CONFIG_XENOMAI
make olddefconfig

# 0.3 — build
make -j$(nproc)
```

## 装内核模块到镜像

```bash
# 挂载镜像 (GPT 分区1 起始扇区 2099200)
sudo mount -o loop,offset=$((2099200 * 512)) \
  /home/richard/work/images/ubuntu/server/image-8G.img /mnt

# 装模块
cd ~/work/2026/xenomai/linux-dovetail
sudo make modules_install INSTALL_MOD_PATH=/mnt
sudo umount /mnt
```

## QEMU 启动

```bash
~/work/2026/xenomai/qemu-test.sh ~/work/2026/xenomai/linux-dovetail/arch/x86/boot/bzImage
```

> 注意：QEMU user-mode 网络接口 enp0s3 启动后可能为 DOWN，需在 VM 内手动：
> `ip link set enp0s3 up && dhclient enp0s3`

## SSH 验证

```bash
ssh -p 10003 root@localhost

# 1. 内核版本
uname -r
# → 6.6.69-g8e9ce988b9b4-dirty

# 2. Xenomai 初始化
dmesg | grep -i "xenomai\|cobalt"
# → [Xenomai] scheduling class idle registered.
# → [Xenomai] scheduling class rt registered.
# → IRQ pipeline: high-priority Xenomai stage added.
# → [Xenomai] Cobalt v3.3.3 [LTRACE]

# 3. /proc/xenomai/ 可用文件
ls /proc/xenomai/
# → affinity clock faults heap latency registry sched timer version

# 4. 延迟
cat /proc/xenomai/latency
# → 3350 (ns)  空闲状态

# 5. 版本
cat /proc/xenomai/version
# → 3.3.3

# 6. 已装模块
ls /lib/modules/$(uname -r)/
```

## E1000 网卡注意

dovetail 内核 CONFIG_E1000=y (built-in)，不是模块，无需 e1000.ko。
接口名 enp0s3，mac 52:54:98:76:00:03。

## 验证结果 ✅

| 检查项 | 状态 | 值 |
|--------|------|-----|
| 内核版本 | OK | 6.6.69-g8e9ce988b9b4-dirty |
| IRQ pipeline | OK | high-priority Xenomai stage added |
| Xenomai Cobalt | OK | v3.3.3 [LTRACE] |
| 调度类 | OK | idle + rt registered |
| /proc/xenomai | OK | 8 个 proc 文件 |
| latency | 3350 ns | 空闲状态 |

## Xenomai 用户态编译 & 安装

```bash
cd ~/work/2026/xenomai/xenomai

# 1. 生成 configure (注意: 是 scripts/bootstrap, 不是 autogen.sh)
./scripts/bootstrap

# 2. 配置
./configure --with-core=cobalt --enable-pshared

# 3. 编译
make -j$(nproc)

# 4. 挂载镜像并安装
sudo mount -o loop,offset=$((2099200 * 512)) \
  /home/richard/work/images/ubuntu/server/image-8G.img /mnt
sudo make install DESTDIR=/mnt
sudo umount /mnt
```

装完后路径:
- 库: `/usr/xenomai/lib/`
- 工具: `/usr/xenomai/bin/` (latency, xeno-test 等)
- 头文件: `/usr/xenomai/include/`

## VM 内运行 latency 测试

```bash
ssh -p 10003 root@localhost
/usr/xenomai/bin/latency
# 如果 lib 找不到: ldconfig
```

## 用户态 API 验证结果 (2026-05-11)

| 检查项 | 状态 | 说明 |
|--------|------|------|
| Cobalt 扩展 POSIX API | ✅ | `pthread_create_ex()` + `clock_nanosleep()` 正常 |
| latency 工具 | ✅ | 可用，直接用 Cobalt 原生 API |
| Alchemy API (rt_task_create) | ❌ | copperplate 初始化 segfault |
| copperplate init | ❌ | `add_free_range()` NULL 指针，`--enable-pshared` 相关 |
| 编译库 (Cobalt 原生) | `-lcobalt -lpthread -lrt` | 不需要 `-lalchemy -lcopperplate` |

**结论**: 在本教程中所有实验使用 Cobalt 扩展 POSIX API（与 latency 相同路径）。
后续 Phase 2-4 需要追查 copperplate init bug 或改用 Cobalt 原生路径。

## Demo 编译 & 部署 (2026-05-14)

```
host:  ~/work/2026/xenomai/demo/    (源码, git)
VM:    /root/xenomai/               (scp 同步)
```

### Makefile 自动检测

```makefile
# VM 有 /usr/xenomai → 用 /usr/xenomai
# host 没有 → 用 ../xenomai (源码树)
XENO_PREFIX ?= $(if $(wildcard /usr/xenomai/include/.),/usr/xenomai,../xenomai)
```

### host — make kmod

```bash
cd ~/work/2026/xenomai/linux-dovetail
# 确保内核已编译 (Module.symvers 存在)
rm -f vmlinux Module.symvers && make -j$(nproc)

cd ~/work/2026/xenomai/demo
make kmod
# → demo-rtdm-irq.ko
```

> `make kmod` 需要内核 Module.symvers（modpost 用它解析 rtdm_event_* 等符号）。
> 之前失败是因为 `rm vmlinux Module.symvers && make -j$(nproc)` 没跑，
> .o 文件都在导致 make 跳过，Module.symvers 没重新生成。
> 删掉 vmlinux 再 make 会重新链接 → 生成 Module.symvers → kmod 通过。

### VM — scp 同步 & make apps

```bash
# 同步 demo 到 VM
scp -P 10003 demo/*.c demo/Makefile demo/Kbuild root@localhost:/root/xenomai/

# SSH 进 VM
ssh -p 10003 root@localhost
cd /root/xenomai

# 编译用户态程序
make apps
# → demo-isolation-xeno  demo-isolation-linux  demo-rtdm-app

# 加载内核模块
insmod demo-rtdm-irq.ko

# 运行
./demo-rtdm-app
./demo-isolation-xeno
./demo-isolation-linux
```

### VM demo 运行结果

| Demo | 状态 | 说明 |
|------|------|------|
| `demo-rtdm-app` | ✅ | 10Hz 模拟 IRQ，read() 阻塞等待，interval ~100ms |
| `demo-isolation-xeno` | ✅ | Cobalt oob, jitter < 100µs 即使 CPU0 有 burner |
| `demo-isolation-linux` | ✅ | SCHED_FIFO, burner 共享 CPU0 时 jitter 很大 |
| edu device | ❌ 删除 | 无法从 host qtest 触发 edu IRQ，放弃 |

### 经验教训

1. **prepare-kernel.sh** — 在 linux-dovetail 里创建 symlink，把 xenomai/kernel/cobalt/ 链进内核树，不是"patch"。之后 `make x86_64_defconfig; ./scripts/config --enable CONFIG_XENOMAI; make olddefconfig; make -j$(nproc)` 才能把 Cobalt 编译进 vmlinux。
2. **Module.symvers 必须存在且非空** — 外部模块编译靠它解析内核导出符号。内核已编译但 Module.symvers 为空 → make 跳过重链 → modpost 失败。解决：`rm vmlinux Module.symvers && make`。
3. **edu 设备无法用** — QEMU `-device edu` 创建 PCI edu 设备，但 host 侧 qtest 触发 IRQ 从未成功。`-device edu` 和 qtest socket 从 qemu-test.sh 移除，全部 edu demo 删除。
4. **Alchemy API 不能用** — copperplate init segfault，本教程只用 Cobalt 原生 API。
