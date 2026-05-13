/*
 * demo-rtdm-app.c — userspace test for the Xenomai RTDM IRQ demo
 *
 * Opens /dev/rtdm/demo-irq-0 and does a blocking read() loop.
 * Each read() returns the interrupt count, then blocks
 * until the next interrupt fires.
 *
 * Compile:
 *   gcc demo-rtdm-app.c -o demo-rtdm-app -lxenomai -lpthread -lrt
 *
 * Run:
 *   ./demo-rtdm-app
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <xenomai/init.h>
#include <cobalt/sys/cobalt.h>

#define DEV_PATH  "/dev/rtdm/demo-irq"

static volatile int stop = 0;
static void handle_sig(int sig) { stop = 1; }

int main(int argc, char *argv[])
{
    struct timespec now, prev;
    unsigned long irq_count, prev_count = 0;
    long long delta_ns;
    int fd, ret, tick = 0;
    int linux_mode = 0;

    xenomai_init(&argc, (char *const **)&argv);

    /* Parse --linux flag: skip harden, read() goes VFS not Cobalt */
    if (argc > 1 && strcmp(argv[1], "--linux") == 0)
        linux_mode = 1;

    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);

    fd = open(DEV_PATH, O_RDONLY);
    if (fd < 0) {
        perror("open " DEV_PATH);
        fprintf(stderr, "Is the kernel module loaded?\n");
        fprintf(stderr, "  insmod demo-rtdm-irq.ko\n");
        return 1;
    }

    printf("RTDM IRQ demo — reading " DEV_PATH "\n");
    if (linux_mode) {
        printf("Mode: SECONDARY (Linux VFS path — no harden)\n");
    } else {
        printf("Mode: PRIMARY (Cobalt RTDM path — harden)\n");
    }
    printf("Each read() blocks until next interrupt (10 Hz)\n\n");

    if (!linux_mode)
        cobalt_thread_harden();
    clock_gettime(CLOCK_MONOTONIC, &prev);

    while (!stop) {
        /*
         * Blocking read: waits in oob (primary) mode until the
         * RTDM driver's ISR pulses the event.
         */
        ret = read(fd, &irq_count, sizeof(irq_count));
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("read"); break;
        }

        clock_gettime(CLOCK_MONOTONIC, &now);
        delta_ns = (now.tv_sec - prev.tv_sec) * 1000000000LL
                 + (now.tv_nsec - prev.tv_nsec);
        prev = now;
        tick++;

        printf("[%2d] IRQ count=%lu  (new=%lu)  interval=%lld.%06lld ms\n",
               tick, irq_count,
               irq_count - prev_count,
               delta_ns / 1000000, delta_ns % 1000000);
        fflush(stdout);

        prev_count = irq_count;

        if (tick >= 50) {  /* Run for 50 interrupts (~5 sec) */
            printf("\n50 interrupts received. Stopping.\n");
            break;
        }
    }

    close(fd);
    return 0;
}
