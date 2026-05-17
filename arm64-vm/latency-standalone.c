/*
 * Standalone ARM64 latency test for Xenomai Cobalt
 * Uses clock_nanosleep() to measure worst-case scheduling latency
 * No Xenomai libraries needed - relies on kernel IRQ pipeline + Cobalt
 *
 * Method: sleep until absolute time, measure difference on wakeup.
 * This is the same method used by Xenomai's built-in latency test.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

static volatile int running = 1;

static void sigint_handler(int sig) { running = 0; }

#define HIST_SIZE 11
static unsigned long lat_hist[HIST_SIZE] = {0};
static const unsigned long hist_bounds[HIST_SIZE] = {
	1000, 2000, 5000, 10000, 20000, 50000,
	100000, 200000, 500000, 1000000, ~0UL
};

static void print_histogram(void)
{
	unsigned long sum = 0;
	int i;

	for (i = 0; i < HIST_SIZE; i++)
		sum += lat_hist[i];
	if (sum == 0) sum = 1;

	printf("\n=== Latency Histogram ===\n");
	printf("%-15s %-10s %-6s\n", "Range (ns)", "Count", "Pct");
	printf("----------------------------------\n");
	for (i = 0; i < HIST_SIZE; i++) {
		char buf[32];
		if (i == HIST_SIZE - 1)
			snprintf(buf, sizeof(buf), ">= %lu", hist_bounds[HIST_SIZE - 1]);
		else if (i == 0)
			snprintf(buf, sizeof(buf), "< %lu", hist_bounds[0]);
		else
			snprintf(buf, sizeof(buf), "%lu - %lu",
				 hist_bounds[i - 1], hist_bounds[i] - 1);
		printf("%-15s %-10lu %5.1f%%\n",
		       buf, lat_hist[i], 100.0 * lat_hist[i] / sum);
	}
}

static inline long long ts_diff_ns(struct timespec *a, struct timespec *b)
{
	return (a->tv_sec - b->tv_sec) * 1000000000LL +
	       (a->tv_nsec - b->tv_nsec);
}

int main(int argc, char *argv[])
{
	struct timespec target, now, interval;
	long long latency_ns, min_ns = 999999999LL, max_ns = 0, total_ns = 0;
	unsigned long count = 0, overruns = 0;
	unsigned long max_samples = 1000;
	int interval_us = 100; /* default 100us period */

	if (argc > 1)
		interval_us = atoi(argv[1]);
	if (argc > 2)
		max_samples = atoi(argv[2]);
	if (interval_us < 1 || interval_us > 10000000) {
		fprintf(stderr, "usage: %s [interval_us] [samples]\n", argv[0]);
		return 1;
	}

	interval.tv_sec  = interval_us / 1000000;
	interval.tv_nsec = (interval_us % 1000000) * 1000;
	interval.tv_sec  = 0;
	interval.tv_nsec = interval_us * 1000;
	while (interval.tv_nsec >= 1000000000) {
		interval.tv_sec++;
		interval.tv_nsec -= 1000000000;
	}

	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);

	clock_gettime(CLOCK_MONOTONIC, &target);

	printf("=== Xenomai Cobalt ARM64 Latency Test ===\n");
	printf("Period: %d us, press Ctrl+C to stop\n\n", interval_us);
	printf("%-10s %-12s %-12s %-12s %-8s\n",
	       "Sample", "Lat(ns)", "Min(ns)", "Max(ns)", "Overrun");

	while (running) {
		/* Advance target by interval */
		target.tv_sec  += interval.tv_sec;
		target.tv_nsec += interval.tv_nsec;
		while (target.tv_nsec >= 1000000000) {
			target.tv_sec++;
			target.tv_nsec -= 1000000000;
		}

		/* Sleep until target time */
		int ret;
		do {
			ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
					      &target, NULL);
		} while (ret == EINTR);

		/* Measure actual wakeup time */
		clock_gettime(CLOCK_MONOTONIC, &now);

		latency_ns = ts_diff_ns(&now, &target);

		if (latency_ns > 0) {
			/* Latency: woke up later than target */
			if (latency_ns < min_ns) min_ns = latency_ns;
			if (latency_ns > max_ns) max_ns = latency_ns;
			total_ns += latency_ns;

			int i;
			for (i = 0; i < HIST_SIZE; i++) {
				if (latency_ns < (long)hist_bounds[i]) {
					lat_hist[i]++;
					break;
				}
			}
			if (i == HIST_SIZE)
				lat_hist[HIST_SIZE - 1]++;
		} else {
			/* Woke up early or on time - zero or negative latency */
			if (latency_ns < 0) {
				/* Clock skew/overshoot - skip this sample timing */
			}
		}

		/* Detect overruns: if we're too far behind, skip to catch up */
		{
			struct timespec tmp;
			clock_gettime(CLOCK_MONOTONIC, &tmp);
			long long behind = ts_diff_ns(&tmp, &target);
			if (behind > (interval.tv_sec * 1000000000LL + interval.tv_nsec)) {
				overruns++;
				target = tmp;
			}
		}

		count++;
		if (count % 5 == 0) {
			printf("%-10lu %-12lld %-12lld %-12lld %-8lu\n",
			       count, latency_ns, min_ns, max_ns, overruns);
			fflush(stdout);
		}

		if (count >= max_samples) running = 0;
	}

	if (count > 0) {
		printf("\n=== Final Results (%lu samples, %lu overruns) ===\n",
		       count, overruns);
		printf("Min   latency: %lld ns\n", min_ns);
		printf("Max   latency: %lld ns\n", max_ns);
		printf("Avg   latency: %lld ns\n", total_ns / count);
		print_histogram();
	}

	/* Print Xenomai info */
	printf("\n=== Xenomai Info ===\n");
	system("cat /proc/xenomai/version 2>/dev/null || echo 'N/A'");
	system("cat /proc/xenomai/stat 2>/dev/null || echo 'N/A'");

	return 0;
}
