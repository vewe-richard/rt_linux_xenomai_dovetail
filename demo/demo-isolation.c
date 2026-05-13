/*
 * demo-isolation.c — Xenomai vs Linux real-time isolation comparison
 *
 * Usage:
 *   ./demo-isolation xeno       Xenomai Cobalt RT (co-kernel isolation)
 *   ./demo-isolation linux      Linux SCHED_FIFO  (no co-kernel)
 *
 * What it does:
 *   - Creates one RT periodic thread (100ms) on CPU 0
 *   - Creates CPU-burner threads on ALL CPUs INCLUDING CPU 0
 *   - Prints timing jitter every 10 ticks
 *   - Ctrl-C to stop
 *
 * Expected result:
 *   xeno:  interval ~100,000,xxx ns, jitter < 100µs even under load
 *   linux: Linux scheduler must share CPU0 between RT thread and burner,
 *          causing massive jitter or starvation of the RT thread
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/timerfd.h>
#include <errno.h>

/* ── Xenomai-only headers ── */
#ifdef WITH_XENOMAI
#include <xenomai/init.h>
#include <cobalt/sys/cobalt.h>
#endif

/* ── Shared state ── */
static volatile int stop = 0;
static void handle_sig(int sig) { stop = 1; }

/* ──────────────────────────────────────────────────
 * CPU burner — spins on a specific CPU
 * ────────────────────────────────────────────────── */
static void *burner(void *arg)
{
    int cpu = (long)arg;
    cpu_set_t c;
    CPU_ZERO(&c); CPU_SET(cpu, &c);
    pthread_setaffinity_np(pthread_self(), sizeof(c), &c);
    printf("  [burner] pinned to CPU %d, spinning...\n", cpu);
    while (!stop) {
        for (volatile int i = 0; i < 10000000; i++);
    }
    return NULL;
}

/* ──────────────────────────────────────────────────
 * RT thread — 100ms period, jitter measurement
 * ────────────────────────────────────────────────── */
#ifdef WITH_XENOMAI

static void *rt_task(void *arg)
{
    struct timespec ts, prev, now;
    long long delta, min_d = (1LL << 60), max_d = 0;
    int count = 0;

    /* Enter Cobalt oob (primary) mode */
    cobalt_thread_harden();

    clock_gettime(CLOCK_MONOTONIC, &prev);
    ts.tv_sec = 0;
    ts.tv_nsec = 100 * 1000 * 1000;  /* 100ms */

    printf("[RT-XENO] oob hardened, CPU0, SCHED_FIFO prio=80, period=100ms\n");

    while (!stop) {
        /* clock_nanosleep is intercepted by libcobalt → Cobalt timer */
        clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
        clock_gettime(CLOCK_MONOTONIC, &now);

        delta = (now.tv_sec - prev.tv_sec) * 1000000000LL
              + (now.tv_nsec - prev.tv_nsec);
        prev = now;
        count++;

        if (delta < min_d) min_d = delta;
        if (delta > max_d) max_d = delta;

        if (count % 10 == 0)
            printf("[RT-XENO] tick=%d  jitter=%+lld ns  "
                   "(min=%lld  max=%lld)\n",
                   count, delta - 100000000LL, min_d - 100000000LL,
                   max_d - 100000000LL);
        fflush(stdout);
    }
    printf("[RT-XENO] stopped. jitter range: [%lld, %lld] ns\n",
           min_d - 100000000LL, max_d - 100000000LL);
    return NULL;
}

static int run_xeno(long nprocs)
{
    pthread_attr_ex_t attr_ex;
    struct sched_param_ex param_ex;
    pthread_t rt;

    /* Create Xenomai RT thread on CPU0, prio 80 */
    pthread_attr_init_ex(&attr_ex);
    pthread_attr_setinheritsched_ex(&attr_ex, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy_ex(&attr_ex, SCHED_FIFO);
    param_ex.sched_priority = 80;
    pthread_attr_setschedparam_ex(&attr_ex, &param_ex);

    int ret = pthread_create_ex(&rt, &attr_ex, rt_task, NULL);
    pthread_attr_destroy_ex(&attr_ex);
    if (ret) { fprintf(stderr, "pthread_create_ex: %s\n", strerror(ret)); return 1; }

    /* Start burners on ALL CPUs (including CPU0!) */
    printf("Starting %ld burners on ALL CPUs (including CPU0)...\n", nprocs);
    for (long i = 0; i < nprocs; i++) {
        pthread_t t;
        pthread_create(&t, NULL, burner, (void *)i);
    }

    printf("=== Running Xenomai Cobalt (Dovetail pipeline) ===\n");
    printf("CPU0: RT thread (oob) + burner (in-band) sharing same core\n");
    printf("Ctrl-C to stop\n\n");

    pause();
    pthread_join(rt, NULL);
    return 0;
}

#else /* !WITH_XENOMAI — plain Linux */

static void *rt_thread_linux(void *arg)
{
    struct timespec now, prev;
    struct itimerspec it;
    long long delta, min_d = (1LL << 60), max_d = 0;
    int tfd, count = 0;
    unsigned long long expirations;

    /* Pin to CPU0 */
    cpu_set_t c;
    CPU_ZERO(&c); CPU_SET(0, &c);
    pthread_setaffinity_np(pthread_self(), sizeof(c), &c);

    /* timerfd — same mechanism, but via Linux hrtimer */
    tfd = timerfd_create(CLOCK_MONOTONIC, 0);
    clock_gettime(CLOCK_MONOTONIC, &prev);

    it.it_value.tv_sec  = prev.tv_sec;
    it.it_value.tv_nsec = prev.tv_nsec + 100 * 1000 * 1000;
    if (it.it_value.tv_nsec >= 1000000000) {
        it.it_value.tv_sec++;
        it.it_value.tv_nsec -= 1000000000;
    }
    it.it_interval.tv_sec  = 0;
    it.it_interval.tv_nsec = 100 * 1000 * 1000;
    timerfd_settime(tfd, TFD_TIMER_ABSTIME, &it, NULL);

    printf("[RT-LINUX] SCHED_FIFO, CPU0, Linux hrtimer, period=100ms\n");

    while (!stop) {
        if (read(tfd, &expirations, sizeof(expirations)) != sizeof(expirations)) {
            if (errno == EINTR) continue;
            break;
        }
        clock_gettime(CLOCK_MONOTONIC, &now);

        delta = (now.tv_sec - prev.tv_sec) * 1000000000LL
              + (now.tv_nsec - prev.tv_nsec);
        prev = now;
        count++;

        if (delta < min_d) min_d = delta;
        if (delta > max_d) max_d = delta;

        if (count % 10 == 0)
            printf("[RT-LINUX] tick=%d  jitter=%+lld ns  "
                   "(min=%lld  max=%lld)\n",
                   count, delta - 100000000LL, min_d - 100000000LL,
                   max_d - 100000000LL);
        fflush(stdout);
    }
    close(tfd);
    printf("[RT-LINUX] stopped. jitter range: [%lld, %lld] ns\n",
           min_d - 100000000LL, max_d - 100000000LL);
    return NULL;
}

static int run_linux(long nprocs)
{
    pthread_attr_t attr;
    struct sched_param param;
    pthread_t rt;

    /* Linux SCHED_FIFO, priority 80 */
    pthread_attr_init(&attr);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    param.sched_priority = 80;
    pthread_attr_setschedparam(&attr, &param);

    if (pthread_create(&rt, &attr, rt_thread_linux, NULL)) {
        perror("pthread_create (linux)"); return 1;
    }
    pthread_attr_destroy(&attr);

    /* Start burners on ALL CPUs (including CPU0!) */
    printf("Starting %ld burners on ALL CPUs (including CPU0)...\n", nprocs);
    for (long i = 0; i < nprocs; i++) {
        pthread_t t;
        pthread_create(&t, NULL, burner, (void *)i);
    }

    printf("=== Running Linux SCHED_FIFO (No Xenomai) ===\n");
    printf("CPU0: RT thread + burner SHARING same CPU via Linux scheduler\n");
    printf("Ctrl-C to stop\n\n");

    pause();
    pthread_join(rt, NULL);
    return 0;
}

#endif /* WITH_XENOMAI */

/* ──────────────────────────────────────────────────
 * Main
 * ────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    long nprocs;

#ifdef WITH_XENOMAI
    xenomai_init(&argc, (char *const **)&argv);
#endif

    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);

    nprocs = sysconf(_SC_NPROCESSORS_ONLN);

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <xeno|linux>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "xeno") == 0) {
#ifdef WITH_XENOMAI
        return run_xeno(nprocs);
#else
        fprintf(stderr, "This binary was built WITHOUT Xenomai support.\n");
        fprintf(stderr, "Rebuild with: make demo-isolation\n");
        return 1;
#endif
    } else if (strcmp(argv[1], "linux") == 0) {
#ifdef WITH_XENOMAI
        fprintf(stderr, "This binary was built WITH Xenomai support.\n");
        fprintf(stderr, "Rebuild with: make demo-isolation-linux\n");
        return 1;
#else
        return run_linux(nprocs);
#endif
    }

    fprintf(stderr, "Unknown mode: %s (use 'xeno' or 'linux')\n", argv[1]);
    return 1;
}
