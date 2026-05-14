# Makefile for Xenomai hands-on demos
# Place in /root/xenomai/ on the VM

# === CONFIG ==========================================
# Auto-detect: VM has /usr/xenomai, host uses ../xenomai
XENO_PREFIX  ?= $(if $(wildcard /usr/xenomai/include/.),/usr/xenomai,../xenomai)
XENO_CFLAGS  := -I$(XENO_PREFIX)/include -I$(XENO_PREFIX)/include/cobalt
XENO_LDFLAGS := -L$(XENO_PREFIX)/lib -Wl,-rpath,$(XENO_PREFIX)/lib
XENO_LIBS    := -lcobalt -lpthread -lrt
XENO_WRAPS   := -Wl,--wrap=open -Wl,--wrap=close -Wl,--wrap=read -Wl,--wrap=write -Wl,--wrap=ioctl

KERNEL_SRC   ?= ../linux-dovetail

.PHONY: all clean kmod apps

all: apps kmod
apps: demo-isolation-xeno demo-isolation-linux demo-rtdm-app
kmod: demo-rtdm-irq.ko

# ─── demo-isolation: Xenomai version ──────────────────
demo-isolation-xeno: demo-isolation.c
	gcc -O2 $(XENO_CFLAGS) $(XENO_LDFLAGS) -DWITH_XENOMAI \
	    $< -o $@ $(XENO_LIBS) $(XENO_WRAPS)

# ─── demo-isolation: plain Linux version ──────────────
demo-isolation-linux: demo-isolation.c
	gcc -O2 -Wall -D_GNU_SOURCE $< -o $@ -lpthread -lrt

# ─── RTDM userspace test app ──────────────────────────
demo-rtdm-app: demo-rtdm-app.c
	gcc -O2 $(XENO_CFLAGS) $(XENO_LDFLAGS) $< -o $@ $(XENO_LIBS) $(XENO_WRAPS)

# ─── RTDM kernel module ───────────────────────────────
# Host: KERNEL_SRC defaults to ../linux-dovetail, XENO_PREFIX to ../xenomai
# VM:   make kmod KERNEL_SRC=/lib/modules/$(uname -r)/build XENO_PREFIX=/usr/xenomai
demo-rtdm-irq.ko: demo-rtdm-irq.c
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) \
	    ccflags-y="-I$(XENO_PREFIX)/include" \
	    modules

clean:
	rm -f demo-isolation-xeno demo-isolation-linux
	rm -f demo-rtdm-app
	rm -f demo-rtdm-irq.ko demo-rtdm-irq.o
	rm -f demo-rtdm-irq.mod.c demo-rtdm-irq.mod.o
	rm -f Module.symvers modules.order
	rm -f .demo-rtdm-irq.ko.cmd .demo-rtdm-irq.o.cmd
	rm -f .demo-rtdm-irq.mod.cmd .demo-rtdm-irq.mod.o.cmd
