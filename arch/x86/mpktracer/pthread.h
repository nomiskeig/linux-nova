#pragma once

#include "config.h"
#ifdef TRACER_USERSPACE
#include <sched.h>
#endif
#ifdef TRACER_USERSPACE
#define MAX_SUPPORTED_THREADS 300
#else
#define MAX_SUPPORTED_THREADS 1
#endif
typedef struct {
    long expected_new_address;
    long original_address;
    long offset_from_original_address; // basically the lenght of the first
                                      // instruction, so that we know what
                                      // exactly the address for a subsequent
                                      // trace is
        long probe_address;
    long length;
} FollowingInfo;
typedef struct {
    int fd;
    long buffer_address;
    long stack_address;
    FollowingInfo following_info;
} ThreadInfo;

typedef struct {
    ThreadInfo mappings[2 * MAX_SUPPORTED_THREADS];
} ThreadMappings;

// this is the pthread struct from nptl/descr.h
#ifdef TRACER_USERSPACE
typedef struct {
    char padding1[720];
    pid_t tid;

} Pthread;

#define get_tid(pthread_t)                                                     \
    ({                                                                         \
        Pthread *thread = (Pthread *)pthread_t;                                \
        thread->tid;                                                           \
    })
#endif
#ifdef TRACER_USERSPACE
#define thread_index get_tid(pthread_self()) - id_offset
#else
#define thread_index 0
#endif
