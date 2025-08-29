#pragma once

#include "trampoline.h"
#ifdef TRACER_USERSPACE
#include <immintrin.h>
#endif

void rdtsc_calibrate(void);
// #define rdtsc() ({_rdtsc();})
static __inline__ long rdtsc_fence(void) {
    unsigned long hi, lo;
    __asm__ __volatile__("mfence\n\t"
                         "lfence\n\t"
                         "rdtsc\n\t"
			 "lfence\n\t"
                         : "=a"(lo), "=d"(hi));

    return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}
#ifdef TRACER_USERSPACE
static __inline__ long rdtsc_self(void) {
    unsigned long hi, lo;
    __asm__ __volatile__(
                         "rdtsc"
                         : "=a"(lo), "=d"(hi));
    return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}
#endif

double ticks_to_absolute_seconds(long ticks);
double duration_to_seconds(long start_tick, long end_tick);
