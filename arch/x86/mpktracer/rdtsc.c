// taken and changed from https://github.com/fordsfords/rdtsc

#include "config.h"
#ifdef TRACER_USERSPACE
#include <inttypes.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#else
#include "linux/delay.h"
#include <linux/ktime.h>
#endif
#include "rdtsc.h"
// long rdtsc_nano_secs_per_tick = 0;
long rdtsc_ticks_per_sec;
long rdtsc_ticks_init;
void rdtsc_calibrate(void) {
#ifdef TRACER_USERSPACE
    struct timespec start_ts;
    struct timespec end_ts;
    uint64_t start_ns, end_ns, duration_ns;
    uint64_t start_ticks, end_ticks, duration_ticks;

    /* We will calibrate with an approx 2 ms sleep. But usleep() is
     * not very accurate, so we will use clock_gettime() to measure
     * precisely how long the sleep was.
     */
    clock_gettime(CLOCK_MONOTONIC, &start_ts);
    start_ticks = rdtsc();
    rdtsc_ticks_init = start_ticks;
    usleep(2000); /* ~2 ms. */
    end_ticks = rdtsc();
    clock_gettime(CLOCK_MONOTONIC, &end_ts);

    start_ns = (uint64_t)start_ts.tv_sec * UINT64_C(1000000000) +
               (uint64_t)start_ts.tv_nsec;
    end_ns = (uint64_t)end_ts.tv_sec * UINT64_C(1000000000) +
             (uint64_t)end_ts.tv_nsec;
    duration_ns = end_ns - start_ns;
    duration_ticks = end_ticks - start_ticks;
    /* sec * ns/sec * ticks/ns = ticks. */
    rdtsc_ticks_per_sec = (UINT64_C(1000000000) * duration_ticks) / duration_ns;
    //printf("we have %li ticks per sec", rdtsc_ticks_per_sec);
    // duration_ns;
#else
    u64 start_ns, end_ns, duration_ns;
    u64 start_ticks, end_ticks, duration_ticks;
    start_ticks = rdtsc();
    start_ns = ktime_get_ns();
    msleep(2);
    end_ticks = rdtsc();
    end_ns = ktime_get_ns();
    duration_ns = end_ns - start_ns;
    duration_ticks = end_ticks - start_ticks;
    rdtsc_ticks_per_sec = 1000000000 * duration_ticks / duration_ns;

#endif
} /* rdtsc_calibrate */

double ticks_to_absolute_seconds(long ticks) {
    return (ticks - rdtsc_ticks_init) / (double)rdtsc_ticks_per_sec;
}
double duration_to_seconds(long start_tick, long end_tick) {
    return (end_tick - start_tick) / (double)rdtsc_ticks_per_sec;
}
int main () {
	rdtsc_calibrate();
}
