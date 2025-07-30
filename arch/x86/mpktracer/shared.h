#pragma once

#include "pthread.h"
#ifdef TRACER_USERSPACE
#include <signal.h>
#else
void log_protect(long address);
#endif
enum EntryType {
    TYPE_WRITE,
    TYPE_FENCE,
    TYPE_FLUSH,
    TYPE_READ,
    TYPE_HYPERCALL

};
typedef struct {
    int area[1024 / sizeof(int)];
} XsaveArea;
typedef struct {

    XsaveArea areas[MAX_SUPPORTED_THREADS];
} XsaveAreas;
typedef enum _RepSize {
    REP_SIZE_8,
    REP_SIZE_16,
    REP_SIZE_32,
    REP_SIZE_64
} RepSize;
typedef enum _RepType { REP_TYPE_STOS = 0x1 } RepType;
#ifdef TRACER_COUNT_AMOUNT_TAKEN
typedef struct {
    long value_size_and_location;
    long long value;
    long address;
    long time_pre;
} Trace;
#else
#ifndef TRACER_NOVA_SUPPORT

typedef struct {
    long value_size_and_location;
    long long value;
    long thread_id;
    long virtual_address;
    long rip_pre;
	long type; // make stuff compatible with nova
	long mnemonic; // make stuff compatible with nova
    long rflags_pre;
    // long time_pre; // time since programm start in seconds
    long rip_post;
    long rflags_post;
    // long time_post;
    // long unused_padding[6];
    int flags; // used for storing information about rep prefixes and su
               // third lowest bit set => rep prefix
               // two lowest bits => encode size
               // size stores value from rcs, i.e. the amount of repeats, so the
               // total size
               // is size * amount
               // higher bits encode the type of rep
} Trace;
#else
typedef struct {
    int type;     // the values are defined by the auto generated encode/decode
                  // functions in the vinter repo, target directory.
                  // 0 -> write
                  // 1 -> fence
                  // 2 -> flush
                  // 3 -> read
                  // 4 -> hypercall
    int mnemonic; // use use numbers and parse them in rust
    int id;
    int non_temporal;
    long value_size_and_location;
    long value;
    long address;
    long flags;
} Trace;
#endif
#endif
typedef struct {
    Trace *trace_address[MAX_SUPPORTED_THREADS];
} TraceAddresses;
typedef

#ifdef TRACER_USERSPACE
// THIS needs to be changed back sas well as the other value below
#define MAX_AMOUNT_TRACES ((1l << 18) / sizeof(Trace))
#else
#define MAX_AMOUNT_TRACES ((1l << 18) / sizeof(Trace))
#endif

    struct {
    // Trace *next_trace_address;

    //  offset from the beginngin of the buffer to the address that can be used
    //  for the next trace, then we need to set the base_trace_buffer_address
    //  each time we inject the tracer and in the kernel, but those values have
    //  to be userspace/kernel local
    long offset;
    long amount;
    long padding[6];
    Trace __attribute__((aligned(64))) traces[MAX_AMOUNT_TRACES];
} Tracebuffer;
typedef struct {

    long next_offset; // this has to be the first entry, since we use the zero
                      // offset for the cmpxchg in
                      // get_and_set_next_value_address (or w/e its called)
#ifdef TRACER_USERSPACE

    char __attribute__((aligned(16))) data[0x1l << 18];

#else
    char data[0x1 << 18];
#endif
} Valuebuffer;

enum SyscallType { MMAP, SIGPROCMASK, SIGACTION };

typedef struct {
    // use updated to make sure that we get a new value
    // on write, we check that updated == 0 and afterwards set it to 1
    //  on read, we check that updated == 1 and afterwards set it to 0
    int updated;
    void *address;
    unsigned long length;
    int prot;
    int flags;
    int isInstalled;
    int signum;
    struct sigaction *action;
    enum SyscallType type;
    int can_receive_mmaps;
    int pending_sigprocmask;

} MmapInfo;

typedef struct {
    void *orig_args;
    void *(*start_routine)(void *);
} PreStartArgs;
typedef struct {
    PreStartArgs allArgs[MAX_SUPPORTED_THREADS];
} AllPreStartArgs;

enum AccessType {
    TRACER_TYPE_LOAD,
    TRACER_TYPE_WRITE,
    TRACER_TYPE_UNKNOWN

};
enum ImplementationState {
    TRACER_STATE_NOT_IMPLEMENTED,
    TRACER_STATE_IMPLEMENTED
};

typedef struct {
    enum AccessType type;
    enum ImplementationState implemented;

} CollectionInfo;
typedef struct {
    long signal_kernel_post;
    long signal_kernel_pre;
    long total_kernel;
    long total_runtime;
    long total_signal_handler;
    long pre_handler;
    long post_handler;
    long invalid_signal_handler[MAX_SUPPORTED_THREADS];
    long fault_signal_handler[MAX_SUPPORTED_THREADS];
    long install_trampoline[MAX_SUPPORTED_THREADS];
    long pre_handler_threads[MAX_SUPPORTED_THREADS];
    long pre_handler_wrapper_threads[MAX_SUPPORTED_THREADS];
    long post_handler_wrapper_threads[MAX_SUPPORTED_THREADS];
    long post_handler_threads[MAX_SUPPORTED_THREADS];
    long amount_success_trampolines[MAX_SUPPORTED_THREADS];
    long amount_read_tramps[MAX_SUPPORTED_THREADS];
    long amount_write_tramps[MAX_SUPPORTED_THREADS];
    long amount_fail_trampolines[MAX_SUPPORTED_THREADS];
    long amount_reads[MAX_SUPPORTED_THREADS];
    long amount_writes[MAX_SUPPORTED_THREADS];
    int trampoline_used_file;
    int layouts_file;
    int tramp_addresses_indices[MAX_SUPPORTED_THREADS];
    long tramp_addresses[MAX_SUPPORTED_THREADS][700];
    long tramp_instr_amount[MAX_SUPPORTED_THREADS][2000];
    long tramp_first_length[MAX_SUPPORTED_THREADS][2000];
    long tramp_install_addresses[MAX_SUPPORTED_THREADS][700];
    int mem_timings_file;
    int used_tramp_pages[MAX_SUPPORTED_THREADS];
} Measurements;

typedef struct {
    long rip;
    int pkey;
    char first_byte;
    int is_tracing_following;
    long original_address;

} TrampInstallArguments;
void set_length(Trace *trace, long length);
void set_intern(Trace *trace);
void set_extern(Trace *trace);
int is_intern(Trace *trace);
long get_size(Trace *trace);

void setRep(Trace *trace);
int isRep(Trace *trace);
void setRepSize(Trace *trace, RepSize size);
RepSize getRepSize(Trace *trace);
void setRepType(Trace *trace, RepType type);
RepType getRepType(Trace *trace);
