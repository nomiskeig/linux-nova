#pragma once
#ifndef __KERNEL__
#define _GNU_SOURCE
#include <sys/mman.h>
#endif
#ifndef __KERNEL__
#define TRACER_USERSPACE
#else
#define TRACER_KERNEL
#include <asm/tracer.h>
#define PKEY_DISABLE_ACCESS 0x1
#define PKEY_DISABLE_WRITE 0x2
#endif

#define TRACER_LOG_DEBUG_STARTER
#define TRACER_LOG_ERROR
#define DISABLE_DEBUG
#ifndef DISABLE_DEBUG
#define TRACER_LOG_DEBUG_STARTER
//#define TRACER_LOG_DEBUG_COLLECTOR
#define TRACER_LOG_DEBUG_PATCHER
#define TRACER_LOG_DEBUG_SIGNAL_HANDLER
#define TRACER_LOG_DEBUG_INJECT
#define TRACER_LOG_DEBUG_NOT_IMPLEMENTED
#define TRACER_LOG_DEBUG_SYSCALL_HANDLER
#define TRACER_LOG_DEBUG_KERNEL_TRACER
 #define TRACER_LOG_DEBUG_NORMAL_PRINT
// #define TRACER_LOG_DEBUG_PTRACE
//#define TRACER_LOG_DEBUG_PTRACE_SYSCALLS
//#define TRACER_LOG_PTRACE_ALL
#define TRACER_LOG_DEBUG_WRITER
//#define TRACER_LOG_DEBUG_INSTRUCTION
#define TRACER_LOG_TEST_PASSES
#define TRACER_LOG_DEBUG_TRAMPOLINES


#define TRACER_LOG_DEBUG_SIGNAL_HANDLER
#define TRACER_LOG_INFO
#define TRACER_LOG_DEBUG_STUB_FUNCTION
//#define TRACER_LOG_DEBUG_POST
#define TRACER_LOG_DEBUG_PRE
#define TRACER_LOG_DEBUG_REGISTERS

//#define TRACER_LOG_DEBUG_TRACE_ADDRESS
#endif
#define TRACER_LOG_DEBUG_NOVA

#define TRACER_LOG_DEBUG_NORMAL_PRINT
//#define TRACER_LOG_DEBUG_CONTEXT
// things that write to files
// #define TRACER_LOG_MMAPS_AND_PROT
// #define TRACER_LOG_PROTECTED_BUFFER

// #define TRACER_LOG_ADDRESS_SPACE

// #define TRACER_LOG_DEBUG_CONTEXT

// how many bytes to log, should be multipe of 8
#define TRACER_MAX_PROTECTED_LOG (1 << 12)

// #define TRACER_LOG_ACCESSES
//#define TRACER_LOG_TRAMPOLINE_LAYOUTS_TO_FILE

//#define TRACER_LOG_ADDRESS_SPACE_LAYOUT_ONCE
// #define TRACER_LOG_PKEY_PROTECTS

// currently only works in userspace
#define TRACER_USE_PTRACE
//#define TRACER_USE_HOOK_LIBC // needs prevent_libc

// #define TRACER_TRACE_NONE
#define TRACER_TRACE_WRITES
//#define TRACER_TRACE_ALL

// #define TRACER_USE_DUMP_ALLOCATOR
#define TRACER_USE_IMPROVED_ALLOCATOR

//#define TRACER_USE_WRITER
// this just handles the cases where we hit the subsequent instruction thats not
// on a trampoline but is an invalid instruction in the program
//#define TRACER_TRACE_SUBSEQUENT // enables the probe that allows us to decide wether we have to trace a subequent instruciton
//#define TRACER_COLLECT_TRAMPOLINE_STATISTICS
//#define TRACER_USE_TRAMPOLINES
//
#ifdef TRACER_USERSPACE
#define disable_pkey(X) pkey_set(X, 0)
#else
#define disable_pkey(X) disable_rw_prot(X)

#endif
#ifdef TRACER_TRACE_NONE
#define enable_pkey(X)
#endif
#ifdef TRACER_TRACE_WRITES
#ifdef TRACER_USERSPACE
#define enable_pkey(X) pkey_set(X, PKEY_DISABLE_WRITE)
#else
#define enable_pkey(X) enable_rw_prot(X, PKEY_DISABLE_WRITE)
#endif
#endif
#ifdef TRACER_TRACE_ALL
#ifdef TRACER_USERSPACE
#define enable_pkey(X) pkey_set(X, PKEY_DISABLE_ACCESS)
#else
#define enable_pkey(X) enable_rw_prot(X, PKEY_DISABLE_ACCESS)
#endif
#endif
//#define CLEAR_VALUE_BEFORE_USE
#ifdef TRACER_USERSPACE
#define enable_pkey_w(X) pkey_set(X, PKEY_DISABLE_WRITE);
#define enable_pkey_rw(X) pkey_set(X, PKEY_DISABLE_ACCESS);
#endif
#define TRACER_PRINT_TOTAL_AMOUNT_TRACES
#define TRACER_PREVENT_LIBC 

#define TRACER_USE_POST_HANDLER
#define TRACER_COLLECT_VALUE
#define TRACER_COLLECT_THREAD_ID
#define TRACER_COLLECT_VIRTUAL_ADDRESS
#define TRACER_COLLECT_RIP_PRE
#define TRACER_COLLECT_RFLAGS_PRE
//#define TRACER_COLLECT_TIME_PRE
#define TRACER_COLLECT_RIP_POST
#define TRACER_COLLECT_RFLAGS_POST
//#define TRACER_COLLECT_TIME_POST

// #define TRACER_COLLECT_SECONDS // with this setting, the timestamp is
// a double that in seconds provides the time since program start, without it,
// the current timestamp from rdtsc is recorded
//  #define TRACER_LOG_TO_CONSOLE
//
//
#define TRACER_TRACE_KERNEL
#define TRACER_NOVA_SUPPORT
////
///
///OPTIMIZATIONS
///
//#define TRACER_DECOMPILE_INSTRUCTIONS
//#define TRACER_CACHE_INSTRUCTIONS // prevents the usage of zydis in the pre and post handler
#define TRACER_ENCODE_INDEX_ON_TRAMPOLINE // encodes the index of the displaced locaiton on the trampoline so it does not need o(n) but O(1) instead


//#define TRACER_USE_SEPERATE_THREAD // starts a thread that installs the trampoline

//#define TRACER_PRE_POPULATE_DISPLACED_INSTRUCTIONS // pre populates the displaced instructions to prevent the page fault handler call
//#define TRACER_PRE_POPULATE_TRACES // pre populates the trace buffer
//#define TRACER_PRE_POPULATE_VALUES // pre populates the value buffer to prevent the page fault handler call
//#define TRACER_USE_SPINLOCK_FOR_ADDRESS // uses a pthread spin lock instead of spining when getting the next address
//#define TRACER_USE_MUTEX_FOR_ADDRESS // uses a pthread mutex instead of spining when getting the next address
//#define TRACER_ALLIGN_ALL // instead of calculating the address for the next value in a way that just works, this will just always increase the address by 256 bytes
//#define TRACER_NO_HANDLER_SAVE_RESTORE
//
//
//
// MEASUREMENTS
//#define TRACER_ENABLE_MEASUREMENTS
//#define TRACER_MEASURE_KERNEL_TIME
//#define TRACER_MEASURE_HANDLERS
//#define TRACER_MEASURE_TRAMPOLINES
//#define TRACER_MEASURE_HANDLERS_THREADS
//#define TRACER_MEASURE_SIGNAL_HANDLER
//#define TRACER_MEASURE_TRAMP_INSTALL
//#define TRACER_OVERWRITE_TRACES
//#define TRACER_COUNT_AMOUNT_TRAMPOLINES
//#define TRACER_COLLECT_TRAMP_INST_INFO
//#define TRACER_COUNT_AMOUNT_TAKEN
//#define TRACER_PROTECT_HEAP
//#define TRACER_USE_DECREASING
//#define TRACER_MEASURE_OVERHEAD
//#define TRACER_PRINT_MEM_TRAMPOLINES

//#define TRACER_COLLECT_TRAMP_INSTR
//#define TRACER_DO_NOT_USE_SIG_HANDLER
