/*
 * demo-rtdm-irq.c — Xenomai RTDM interrupt handling kernel module
 *
 * Demonstrates the canonical Xenomai 3.3.x RTDM IRQ handling pattern:
 *   1. Define struct rtdm_driver (holds ops, flags, context_size)
 *   2. Define struct rtdm_device (holds driver ptr, label, minor)
 *   3. In the ISR: pulse an rtdm_event → wakes blocked userspace
 *   4. Userspace read() on /dev/rtdm/demo-irq blocks until next IRQ
 *
 * Since QEMU has no physical GPIO/IRQ hardware, this example uses
 * a periodic kernel timer at 10 Hz as the "interrupt source".
 *
 * TO ADAPT FOR REAL HARDWARE:
 *   Replace timer_setup()/mod_timer()  → xnintr_init()/xnintr_attach(irq_num)
 *   Replace timer_callback()           → your_isr(struct xnintr *intr)
 *   Keep the rtdm_event_pulse() wakeup identical.
 *
 * Build & test:
 *   make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
 *   insmod demo-rtdm-irq.ko
 *   ./demo-rtdm-app
 *   rmmod demo-rtdm-irq
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <rtdm/driver.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Xenomai Hands-on");
MODULE_DESCRIPTION("RTDM interrupt handling demo");

/* ── Simulated interrupt rate: 10 Hz ───────────────── */
#define IRQ_PERIOD_HZ  10

/* ── Per-device context ──────────────────────────────
 *
 * In a real driver, replace the timer with:
 *   struct xnintr intr;       // Cobalt interrupt object
 *   unsigned int irq_num;     // hardware IRQ line
 */
struct demo_context {
    rtdm_event_t      irq_event;    /* signals userspace on each IRQ */
    unsigned long     irq_count;    /* incremented by ISR */
    struct timer_list sim_timer;    /* periodic "fake interrupt" source */
};

static struct demo_context ctx;

/* ── "Interrupt Service Routine" ─────────────────────
 *
 * Called by the timer callback (or xnintr in real hardware).
 * Pulses the event to wake all blocked readers.
 *
 * Real hardware ISR pattern:
 *
 *   static int my_isr(struct xnintr *intr)
 *   {
 *       struct demo_context *c = intr->cookie;
 *       c->irq_count++;
 *       rtdm_event_pulse(&c->irq_event);
 *       return XN_IRQ_HANDLED;
 *   }
 *
 *   // In init:
 *   xnintr_init(&ctx.intr, "demo-irq", irq_num, my_isr, NULL, 0);
 *   xnintr_attach(&ctx.intr, &ctx, &IRQ_AFFINITY_MASK);
 */
static void timer_callback(struct timer_list *t)
{
    ctx.irq_count++;
    rtdm_event_pulse(&ctx.irq_event);  /* wake ALL waiters */
    mod_timer(&ctx.sim_timer, jiffies + HZ / IRQ_PERIOD_HZ);
}

/* ── RTDM device operations ────────────────────────── */

/*
 * Called when userspace opens /dev/rtdm/demo-irq.
 * In a real driver, xnintr_attach() would go here.
 */
static int demo_open(struct rtdm_fd *fd, int oflags)
{
    rtdm_event_clear(&ctx.irq_event);
    ctx.irq_count = 0;

    /* Start simulated interrupt source */
    timer_setup(&ctx.sim_timer, timer_callback, 0);
    mod_timer(&ctx.sim_timer, jiffies + HZ / IRQ_PERIOD_HZ);

    printk(KERN_INFO "demo-irq: device opened\n");
    return 0;
}

/* Called when userspace closes the device. */
static void demo_close(struct rtdm_fd *fd)
{
    del_timer_sync(&ctx.sim_timer);
    printk(KERN_INFO "demo-irq: device closed\n");
}

/*
 * Blocking read: wait for next interrupt, return irq_count.
 *
 * Userspace calls read(fd, &count, sizeof(count)) and blocks in oob
 * (primary) mode until the ISR pulses the event.  No Linux scheduler
 * involvement — the thread is woken directly from the oob domain.
 */
static ssize_t demo_read_rt(struct rtdm_fd *fd, void __user *buf, size_t nbyte)
{
    int ret;

    if (nbyte < sizeof(unsigned long))
        return -EINVAL;

    /*
     * rtdm_event_wait() blocks in real-time context.
     * When the ISR pulses the event, we return immediately.
     */
    ret = rtdm_event_wait(&ctx.irq_event);
    if (ret)
        return ret;  /* -EINTR, -EIDRM, etc. */

    ret = rtdm_safe_copy_to_user(fd, buf, &ctx.irq_count,
                                  sizeof(unsigned long));
    return ret ? ret : sizeof(unsigned long);
}

/* Non-blocking ioctl: get current irq_count without waiting. */
static int demo_ioctl_rt(struct rtdm_fd *fd,
                          unsigned int request, void __user *arg)
{
    switch (request) {
    case 0: /* GET_COUNT */
        return rtdm_safe_copy_to_user(fd, arg, &ctx.irq_count,
                                       sizeof(unsigned long));
    default:
        return -ENOTTY;
    }
}

/* ── RTDM driver descriptor ──────────────────────────
 *
 * In Xenomai 3.3.x:
 *   - struct rtdm_driver  holds ops, flags, context_size, device_count
 *   - struct rtdm_device  holds driver ptr, label, minor
 *   - device_flags goes in rtdm_driver, NOT in rtdm_device
 */

static struct rtdm_driver demo_driver = {
    .profile_info   = RTDM_PROFILE_INFO(demo_irq,
                                         RTDM_CLASS_TESTING,
                                         RTDM_SUBCLASS_GENERIC,
                                         1),
    .device_flags   = RTDM_NAMED_DEVICE | RTDM_EXCLUSIVE,
    .device_count   = 1,
    .context_size   = 0,      /* no per-fd private data */
    .ops = {
        .open       = demo_open,
        .close      = demo_close,
        .read_rt    = demo_read_rt,
        .read_nrt   = demo_read_rt,
        .ioctl_rt   = demo_ioctl_rt,
        .ioctl_nrt  = demo_ioctl_rt,
    },
};

static struct rtdm_device dev = {
    .driver = &demo_driver,
    .label  = "demo-irq",
};

/* ── Module init/exit ──────────────────────────────── */
static int __init demo_init(void)
{
    int ret;

    rtdm_event_init(&ctx.irq_event, 0);

    ret = rtdm_dev_register(&dev);
    if (ret) {
        rtdm_event_destroy(&ctx.irq_event);
        printk(KERN_ERR "demo-irq: rtdm_dev_register failed: %d\n", ret);
        return ret;
    }

    printk(KERN_INFO "demo-irq: ready at /dev/rtdm/demo-irq\n");
    return 0;
}

static void __exit demo_exit(void)
{
    del_timer_sync(&ctx.sim_timer);
    rtdm_dev_unregister(&dev);
    rtdm_event_destroy(&ctx.irq_event);
    printk(KERN_INFO "demo-irq: removed\n");
}

module_init(demo_init);
module_exit(demo_exit);
