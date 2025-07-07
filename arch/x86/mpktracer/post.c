
#define _GNU_SOURCE
#include "post.h"
#include "config.h"
#ifdef TRACER_USERSPACE
#include "pthread.h"
#include <err.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#ifdef TRACER_ENABLE_MEASUREMENTS
#include "shared.h"
extern Measurements *measurements;
#endif
#endif
#include "collector.h"
#include "logging.h"
#include "rdtsc.h"
#include "register.h"
#include "shared.h"
#include "trampoline.h"

extern Tracebuffer *tracebuffer;
extern Valuebuffer *valuebuffer;
extern DisplacedInstructions *displaced_instructions;
extern int id_offset;
extern ThreadMappings *thread_mappings;
extern TraceAddresses *trace_addresses;
#ifdef TRACER_PREVENT_LIBC
extern int use_glibc[MAX_SUPPORTED_THREADS];
#endif

extern XsaveAreas *xsave_areas;

void tracing_probe(long expected) {
    //__builtin_ia32_xsave(xsave_areas->areas[thread_index].area, 7);
    /*asm("sub $400, %rsp\n\t"
        "push %rax\n\t"
        "push %rbx\n\t"
        "push %rcx\n\t"
        "push %rdx\n\t"
        "push %rdi\n\t"
        "push %rsi\n\t"
        "push %r8\n\t"
        "push %r9\n\t"
        "push %r10\n\t"
        "push %r11\n\t"
        "push %r12\n\t"
        "push %r13\n\t"
        "push %r14\n\t"
        "push %r15\n\t");*/
    // The follwoing trampoline never has a second instruction, and therefore we
    // can safely assume that when we get here and we have undecided it is
    // because we did not trace the instruction, there is no way that a
    // potential installed trampoline modifies thie following_info
    TRACER_PRINT_DEBUG_POST("is in post probe");
#ifdef TRACER_USERSPACE
    int index = get_tid(pthread_self()) - id_offset;
    TRACER_PRINT_DEBUG_POST("index in post is %i", index);
#else
    int index = 0;
#endif
    // search the index which has the correct info
    int correct_index = -1;
    for (int i = 0; i < MAX_SUPPORTED_THREADS; i++) {
        if (thread_mappings->mappings[i].following_info.expected_new_address ==
            expected) {
            correct_index = i;
            break;
        }
    }
#ifdef TRACER_LOG_ERROR
    if (correct_index == -1) {
        TRACER_PRINT_ERROR(
            "did not find an index in the post probe, expected %lx", expected);
    }
#endif
    TRACER_PRINT_DEBUG_POST(
        "is in tracing probe, probe_address is %px, length is %li",
        (void *)thread_mappings->mappings[correct_index]
            .following_info.probe_address,
        thread_mappings->mappings[correct_index].following_info.length);
    set_following_must_be_traced(thread_mappings->mappings[correct_index]
                                     .following_info.original_address,
                                 TRACER_DO_NOT_TRACE);
    // uninstall the probe

    *((int *)((thread_mappings->mappings[correct_index]
                   .following_info.probe_address))) =
        thread_mappings->mappings[correct_index].following_info.length;

    TRACER_PRINT_DEBUG_POST("is at end of tracing probe");
#ifndef TRACER_NO_HANDLER_SAVE_RESTORE
    __builtin_ia32_xrstor(xsave_areas->areas[index].area, 7);
#endif
    /* asm("pop %r15\n\t"
         "pop %r14\n\t"
         "pop %r13\n\t"
         "pop %r12\n\t"
         "pop %r11\n\t"
         "pop %r10\n\t"
         "pop %r9\n\t"
         "pop %r8\n\t"
         "pop %rsi\n\t"
         "pop %rdi\n\t"
         "pop %rdx\n\t"
         "pop %rcx\n\t"
         "pop %rbx\n\t"
         "pop %rax\n\t"
         "add $400, %rsp\n\t");*/
}

// this is called from the trampoline
void collect_post_wrapper(tracer_regs_t regs, long address_of_instruction) {
#ifdef TRACER_MEASURE_TRAMPOLINES
    long start_ticks = rdtsc_fence();
#endif
#ifdef TRACER_MEASURE_HANDLERS_THREADS
    long start_ticks = rdtsc();
#endif
#ifdef TRACER_PREVENT_LIBC
    use_glibc[thread_index] = 0;
#endif
    // store the xmm registers
#ifdef TRACER_USERSPACE
    int index = get_tid(pthread_self()) - id_offset;
#else
    int index = 0;
#endif
#ifndef TRACER_NO_HANDLER_SAVE_RESTORE
    __builtin_ia32_xsave(xsave_areas->areas[index].area, 7);
#endif

    TRACER_PRINT_DEBUG_POST("is in collect post wrapper from instruction %lx\n",
                            address_of_instruction);
    // Get the instruciton
    // PERF: this could/should probably be cached
    /*int is_following;


    tracer_follow_type type;
    // i dont think we ever have the case where we hit a following
    // instruction and land on the trampoline, so the is_following parameter
    // for the following call does not matter
    long location_of_instruction = find_displaced_location(
        address_of_instruction, displaced_instructions, &is_following, &type);
    ZydisDisassembledInstruction instruction;
    ZyanStatus status = ZydisDisassembleIntel(
         ZYDIS_MACHINE_MODE_LONG_64,
         location_of_instruction,
         (void *)location_of_instruction,
         15,
         &instruction);

    if (!ZYAN_SUCCESS(status)) {
        TRACER_PRINT_ERROR(
            "Could not decode the instruction in pre collectior");
    }
    TRACER_PRINT_DEBUG_POST(
        "the disassembled instruction in the post collector is %s\n",
        instruction.text);
        */
#ifdef TRACER_LOG_DEBUG_CONTEXT
    TRACER_PRINT_DEBUG_PRE("is in post collector wrapper b/c of address 0x%lx",
                           address_of_instruction);
    TRACER_PRINT_DEBUG_CONTEXT("RAX: %016llx", regs[TRACER_REG_RAX]);
    TRACER_PRINT_DEBUG_CONTEXT("RCX: %016llx", regs[TRACER_REG_RCX]);
    TRACER_PRINT_DEBUG_CONTEXT("RDX: %016llx", regs[TRACER_REG_RDX]);
    TRACER_PRINT_DEBUG_CONTEXT("RBX: %016llx", regs[TRACER_REG_RBX]);
    TRACER_PRINT_DEBUG_CONTEXT("RSI: %016llx", regs[TRACER_REG_RSI]);
    TRACER_PRINT_DEBUG_CONTEXT("RDI: %016llx", regs[TRACER_REG_RDI]);
    TRACER_PRINT_DEBUG_CONTEXT("RSP: %016llx", regs[TRACER_REG_RSP]);
    TRACER_PRINT_DEBUG_CONTEXT("RBP: %016llx", regs[TRACER_REG_RBP]);
    TRACER_PRINT_DEBUG_CONTEXT("R8: %016llx", regs[TRACER_REG_R8]);
    TRACER_PRINT_DEBUG_CONTEXT("R9: %016llx", regs[TRACER_REG_R9]);
    TRACER_PRINT_DEBUG_CONTEXT("R10: %016llx", regs[TRACER_REG_R10]);
    TRACER_PRINT_DEBUG_CONTEXT("R11: %016llx", regs[TRACER_REG_R11]);
    TRACER_PRINT_DEBUG_CONTEXT("R12: %016llx", regs[TRACER_REG_R12]);
    TRACER_PRINT_DEBUG_CONTEXT("R13: %016llx", regs[TRACER_REG_R13]);
    TRACER_PRINT_DEBUG_CONTEXT("R14: %016llx", regs[TRACER_REG_R14]);
    TRACER_PRINT_DEBUG_CONTEXT("R15: %016llx", regs[TRACER_REG_R15]);
    TRACER_PRINT_DEBUG_CONTEXT("RIP: %016llx", regs[TRACER_REG_RIP_DO_NOT_USE]);
#endif
#ifdef TRACER_MEASURE_HANDLERS_THREADS
    long before_collect_ticks = rdtsc();
#endif
    collect_post(regs, 1);
#ifdef TRACER_MEASURE_HANDLERS_THREADS
    long after_collect_ticks = rdtsc();
#endif
    TRACER_PRINT_DEBUG_POST("Restoring regs after collect post");
#ifdef TRACER_USERSPACE
#ifndef TRACER_NO_HANDLER_SAVE_RESTORE
    __builtin_ia32_xrstor(xsave_areas->areas[index].area, 7);
#endif
#endif
#ifdef TRACER_PREVENT_LIBC
    use_glibc[thread_index] = 1;
#endif
#ifdef TRACER_MEASURE_HANDLERS_THREADS
    long end_ticks = rdtsc();
    measurements->post_handler_wrapper_threads[thread_index] +=
        end_ticks - start_ticks - (after_collect_ticks - before_collect_ticks);
#endif
#ifdef TRACER_MEASURE_TRAMPOLINES
    long end_ticks = rdtsc_fence();
    ;
    measurements->post_handler = end_ticks - start_ticks;
#endif
}

void collect_post(tracer_regs_t regs, int from_trampoline) {
#ifdef TRACER_MEASURE_HANDLERS_THREADS
    long start_ticks;
    start_ticks = rdtsc();
#endif
#ifdef TRACER_MEASURE_HANDLERS
    long start_ticks = rdtsc_fence();
#endif
    TRACER_PRINT_DEBUG_POST("Is in post handler");

    Trace *trace = trace_addresses->trace_address[thread_index];
#ifndef TRACER_COUNT_AMOUNT_TAKEN
#ifdef TRACER_USERSPACE
#ifdef TRACER_COLLECT_SECONDS
#ifndef TRACER_NO_HANDLER_SAVE_RESTORE
    trace->time_pre = ticks_to_absolute_seconds(rdtsc());
#endif;
#else
#ifndef TRACER_NO_HANDLER_SAVE_RESTORE
#ifdef TRACER_COLLECT_TIME_POST
    trace->time_post = rdtsc();
#endif
#endif

#endif
#endif
#ifdef TRACER_COLLECT_RFLAGS_POST
#ifndef TRACER_NOVA_SUPPORT
    trace->rflags_post = regs[TRACER_REG_FLAGS];
#endif
#endif
#ifdef TRACER_MEASURE_HANDLERS
    long end_ticks = rdtsc_fence();
    measurements->post_handler = end_ticks - start_ticks;

#endif
#endif
#ifdef TRACER_MEASURE_HANDLERS_THREADS
    long end_ticks = rdtsc();
    measurements->post_handler_threads[thread_index] += end_ticks - start_ticks;

#endif
}
