#include "instruction.h"
#define _GNU_SOURCE
#include "config.h"
#include "pre.h"
#ifdef TRACER_USERSPACE
#include "pthread.h"
#include "shared.h"
#include <err.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#ifdef TRACER_ENABLE_MEASUREMENTS
extern Measurements *measurements;
#endif
#else
#include <asm/io.h>
#include <linux/mm.h>
#include <linux/printk.h>
#include <linux/sched/debug.h>

#endif
#include "collector.h"
#include "logging.h"
#include "rdtsc.h"
#include "register.h"
#include "trampoline.h"
extern Tracebuffer *tracebuffer;
extern Valuebuffer *valuebuffer;
extern DisplacedInstructions *displaced_instructions;
extern int id_offset;
#ifdef TRACER_PREVENT_LIBC
extern int use_glibc[MAX_SUPPORTED_THREADS];
#endif
#ifdef TRACER_LOG_ACCESSES
extern FILE *trace_log;
#endif
extern TraceAddresses *trace_addresses;
long counter = 0;
extern XsaveAreas *xsave_areas;
// pthread_mutex_t address_lock;
long old;
// this function needs to handle the case where the stored address is that of
// the next instrution in the user space but it accessed in the kernel, the
// offset needs to be added. The offset has to be required at runtime.
Trace *get_next_trace(void) {
    long offset;
    asm("mov %1, %%rax\n\t"
        "mov $1, %%rbx\n\t"
        "lock xadd %%rax, %2\n\t" // add to next_trace_address, rax has to be
        // the register containing the size;
        "lock xadd %%rbx, %3\n\t" // add one to the amount, i think it is fine
        // to do it like this, we dont loose the
        // update to the amount, it may only be
        // scheduled after another addition to amount,
        // but in the end, it is fine (unless you want
        // to access a trace at runtime, but then
        // thats not my problem anyways )
        // rax now contains the value which we use for this trace
        "mov %%rax, %0\n\t"
        : "=m"(offset)
#ifdef TRACER_OVERWRITE_TRACES
        : "mri"(0x0), "m"(tracebuffer->offset),
#else
        : "mr"(sizeof(Trace)), "m"(tracebuffer->offset),
#endif
          "m"(tracebuffer->amount)
        : "rax", "rbx");
    /*//pthread_mutex_lock(&address_lock);
 void *address = tracebuffer->next_trace_address;
 tracebuffer->next_trace_address += sizeof(Trace);
 tracebuffer->amount += 1;
    //pthread_mutex_unlock(&address_lock);
    Trace* address = __atomic_fetch_add(&tracebuffer->next_trace_address,
 sizeof(Trace), __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&tracebuffer->amount, 1, __ATOMIC_SEQ_CST);
    */
    /*
if ((long)address < 0x1000) {
    TRACER_PRINT_ERROR("got the wrong address");
}
*/
    Trace *trace = (Trace *)(offset + (long)tracebuffer);
#ifdef TRACER_LOG_ERROR
#ifndef TRACER_OVERWRITE_TRACES
    if (tracebuffer->amount > (MAX_AMOUNT_TRACES - 4)) {
        TRACER_PRINT_ERROR("Tracer buffer ran full, aborting, got %li tracees",
                           tracebuffer->amount);
    }
#endif
#endif
#ifdef CLEAR_VALUE_BEFORE_USE

    ((Trace *)address)->rip_pre = 0l;
    ((Trace *)address)->rip_post = 0l;
    ((Trace *)address)->time_pre = 0l;
    ((Trace *)address)->time_post = 0l;
    ((Trace *)address)->value = 0l;
    ((Trace *)address)->virtual_address = 0l;
    ((Trace *)address)->thread_id = 0l;
    ((Trace *)address)->rflags_pre = 0l;
    ((Trace *)address)->rflags_post = 0l;

#endif
#ifdef TRACER_NOVA_SUPPORT
    // TODO: this is racy, but also like why does this crash????, coiuld be
    // becuase int and long types for id
    // This is -1 becuase we want zero-indexed ids
    trace->id = tracebuffer->amount - 1;
#endif
    TRACER_PRINT_DEBUG("next address is %lx",
                       (long)offset + (long)&tracebuffer);
    return trace;
}

#ifndef TRACER_COUNT_AMOUNT_TAKEN
static void collect_registers_pre(tracer_regs_t regs, long rip_of_address,
                                  Trace *trace) {
#ifdef TRACER_COLLECT_RIP_PRE
#ifndef TRACER_NOVA_SUPPORT
    trace->rip_pre = rip_of_address;
#endif
#endif
#ifdef TRACER_COLLECT_RFLAGS_PRE
#ifndef TRACER_NO_HANDLER_SAVE_RESTORE
#ifndef TRACER_NOVA_SUPPORT
    trace->rflags_pre = regs[TRACER_REG_FLAGS];
#endif
#endif
#endif
}
static void collect_thread_id(Trace *trace) {
#ifdef TRACER_USERSPACE
#ifndef TRACER_NOVA_SUPPORT
    trace->thread_id = pthread_self();
#endif
#else
#ifndef TRACER_NOVA_SUPPORT
    trace->thread_id = 0;
#endif
#endif
}

static void __attribute__((unused)) collect_time_pre(Trace *trace) {
#ifdef TRACER_USERSPACE
#ifdef TRACER_COLLECT_TIME_PRE
#ifdef TRACER_COLLECT_SECONDS
    trace->time_pre = ticks_to_absolute_seconds(rdtsc());
#else
    trace->time_pre = rdtsc();
#endif
#endif
#endif
}
void collect_address(tracer_regs_t regs,
                     ZydisDisassembledInstruction *instruction, Trace *trace) {
    // PERF: i think this could be optimzed by writing the instructions that
    // collect that info directly on the trampoline ... but at some point it
    // gets to complex and i have to write my own assembler... the traced
    // instruction has exacly one memory operand, we search it and then
    // calculate the address based on it

    long address;
    for (int i = 0; i < instruction->info.operand_count; i++) {
        ZydisDecodedOperand *op = &instruction->operands[i];
        if (op->type != ZYDIS_OPERAND_TYPE_MEMORY) {
            continue;
        }
        TracerRegister base = get_offset_of_reg(op->mem.base);
        address = regs[tracer_reg_to_specific_reg_index(base)];
        if (op->mem.disp.size > 0) {
            address += op->mem.disp.value;
        }
        if (op->mem.index != ZYDIS_REGISTER_NONE) {
            TracerRegister index = get_offset_of_reg(op->mem.index);
            address +=
                op->mem.scale * regs[tracer_reg_to_specific_reg_index(index)];
        }
        // TRACER_PRINT_DEBUG(
        //     "next print should be the address of virtual address");
        // TRACER_PRINT_DEBUG("trace->virtual_address is %p, and trace is %p",
        //                   (void *)&trace->virtual_address, (void *)trace);
        //
    }
#ifndef TRACER_NOVA_SUPPORT
    trace->virtual_address = address;
#else
    // we need to calculate the physiacal address and subtract from that the
    // beginnig of the pyhsical mapping since vinter expects addresses
    // starting at 0
#ifndef TRACER_USERSPACE
    // PERF: this is probably slow and the offset should be stored somewhere and
    // not calculated each time
    // trace->address =
    //   (page_to_phys(virt_to_page((void *)address))  - (0x1l << 34)) |
    //   (address & 0xFFF); // trace->address =
#ifndef TRACER_VINTER_INVESTIGATE
//#define PMEM_START 134217728 //this is the pmem0 mapped at 128mb with a size of 5 mb
#define PMEM_START 0x540000000
    trace->address = (page_to_phys(virt_to_page((void *)address)) - PMEM_START) |
                     (address & 0xFFF); // trace->address =

#endif
    // (page_to_phys(vmalloc_to_page((void *) address))) ;//| (address & 0xFFF);
    // trace->address = address;

#else
#ifndef TRACER_VINTER_INVESTIGATE
    trace->address = address;
#endif
#endif
#endif
    // TRACER_PRINT_DEBUG("set virtual address of instruction");
}
#endif
// This is called from the trampoline, we also need the register
long collect_pre_wrapper(tracer_regs_t regs, long address_of_instruction,
                         long original_address
#ifdef TRACER_ENCODE_INDEX_ON_TRAMPOLINE
                         ,
                         int displaced_loc_index
#endif
) {
// safe_print_to_file(measurements->trampoline_used_file, "%lx\n",
// address_of_instruction);
#ifdef TRACER_MEASURE_TRAMPOLINES
    long start_ticks = rdtsc();
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

    TRACER_PRINT_DEBUG_PRE("is in collect pre wrapper from instruction %lx, "
                           "saved to %lx, original is %lx\n",
                           address_of_instruction,
                           (long)xsave_areas->areas[index].area,
                           original_address);
    // Get the instruciton
    // PERF: this could/should probably be cached
    // i dont think we ever have the case where we hit a following instruction
    // and land on the trampoline, so the is_following parameter for the
    // following call does not matter
#if defined(TRACER_DECOMPILE_INSTRUCTIONS)
    ZydisDisassembledInstruction instruction;
    int is_following;
    tracer_follow_type type;
    long location_of_instruction = find_displaced_location(
        address_of_instruction, displaced_instructions, &is_following, &type);
    ZyanStatus status = ZydisDisassembleIntel(
        /* machine_mode:    */ ZYDIS_MACHINE_MODE_LONG_64,
        /* runtime_address: */ location_of_instruction,
        /* buffer:          */ (void *)location_of_instruction,
        /* length:          */ 15,
        /* instruction:     */ &instruction);

    if (!ZYAN_SUCCESS(status)) {
        TRACER_PRINT_ERROR(
            "Could not decode the instruction in pre collectior");
    }
#ifdef TRACER_LOG_ACCESSES
    fprintf(trace_log, "Thread: %i, Instr: %s\n", gettid(), instruction.text);
#endif

#elif defined(TRACER_CACHE_INSTRUCTIONS)
    DisplacedInstructionLocation *displaced_location =
        get_displaced_location_info(address_of_instruction,
                                    displaced_instructions);
    /*long location_of_instruction = (long)&displaced_location->instruction +
                                   address_of_instruction -
                                   displaced_location->orig_address;*/
    ZydisDisassembledInstruction *instruction;
    for (int i = 0; i < 5; i++) {
        if (address_of_instruction == displaced_location->orig_addresses[i]) {
            instruction = &displaced_location->disassembled_instructions[i];
        }
    }
#ifdef TRACER_LOG_ACCESSES
    fprintf(trace_log, "Thread: %i, Instr: %s\n", gettid(), instruction->text);
#endif

#elif defined(TRACER_ENCODE_INDEX_ON_TRAMPOLINE)
    DisplacedInstructionLocation *displaced_location =
        &displaced_instructions->instructions[displaced_loc_index];
    TRACER_PRINT_DEBUG_PRE("index of the diplacced instruction is %i",
                           displaced_loc_index);
    ZydisDisassembledInstruction *instruction;
    for (int i = 0; i < 5; i++) {
        if (address_of_instruction == displaced_location->orig_addresses[i]) {
            instruction = &displaced_location->disassembled_instructions[i];
        }
    }
#endif
#ifdef TRACER_PREVENT_LIBC
    use_glibc[thread_index] = 1;
#endif
    TRACER_PRINT_DEBUG_PRE(
        "the disassembled instruction in the pre colletor is %s\n",
        instruction->text);
#ifdef TRACER_MEASURE_HANDLERS_THREADS
    long before_collect_ticks = rdtsc();
#endif
#if defined(TRACER_DECOMPILE_INSTRUCTIONS)
    long address = collect_pre(regs, &instruction, 1, address_of_instruction);
#elif defined(TRACER_CACHE_INSTRUCTIONS)
    long address = collect_pre(regs, instruction, 1, address_of_instruction);
#elif defined(TRACER_ENCODE_INDEX_ON_TRAMPOLINE)
    long address = collect_pre(regs, instruction, 1, address_of_instruction);
#endif
#ifdef TRACER_MEASURE_HANDLERS_THREADS
    long after_collect_ticks = rdtsc();
#endif
// this crashes in the kernel with a general protection fault, idk why
#ifdef TRACER_USERSPACE

#ifndef TRACER_NO_HANDLER_SAVE_RESTORE
    __builtin_ia32_xrstor(xsave_areas->areas[thread_index].area, 7);
#endif
#ifdef TRACER_MEASURE_HANDLERS_THREADS
    long end_ticks = rdtsc();
    measurements->pre_handler_wrapper_threads[thread_index] +=
        end_ticks - start_ticks - (after_collect_ticks - before_collect_ticks);
#endif
#ifdef TRACER_MEASURE_TRAMPOLINES
    long end_ticks = rdtsc();
    ;
    measurements->pre_handler = end_ticks - start_ticks;
#endif

#endif
    return address;
}

long collect_pre(tracer_regs_t regs, ZydisDisassembledInstruction *instruction,
                 int from_trampoline, long rip_of_instruction) {
#ifdef TRACER_MEASURE_HANDLERS_THREADS
    long start_ticks;
    start_ticks = rdtsc();
#endif

#ifdef TRACER_MEASURE_HANDLERS
    long start_ticks = rdtsc_fence();
#endif
#ifndef TRACER_USERSPACE
    // pr_info("getting trace for instruction %s", instruction->text);
#endif
    Trace *trace = get_next_trace();
    /* if (trace->id == 295557) {
         pr_info("is failed with instruction %s", instruction->text);
     }
     */
#ifdef TRACER_PRINT_MEM_TRAMPOLINES
    if (from_trampoline == 1) {
        char text[2];
        text[0] = '1';
        text[1] = '\n';
        int res = write(measurements->mem_timings_file, text, 2);
        if (res == 1) {
            TRACER_PRINT_ERROR("could not write file");
        }
    }

#endif

#ifdef TRACER_COUNT_AMOUNT_TAKEN
    if (from_trampoline == 1) {
        trace->address = 0l;
        trace->address = rip_of_instruction;
    } else {
        trace->address = 0l;
    }
#endif
#ifdef TRACER_VINTER_INVESTIGATE
    trace->timestamp = rdtsc_self();
    trace->origin_address = rip_of_instruction;
#endif
    if (instruction == NULL) {
        TRACER_PRINT_ERROR("instruction pointer is NULL");
    }
    if (instruction->info.mnemonic == ZYDIS_MNEMONIC_MOVNTI) {
        trace->non_temporal = 1;
    } else {
        trace->non_temporal = 0;
    }
    if (instruction->info.attributes & ZYDIS_ATTRIB_HAS_REP) {

        switch (instruction->info.mnemonic) {
        case ZYDIS_MNEMONIC_STOSB: {
            setRep(trace);
            setRepSize(trace, REP_SIZE_8);
            TRACER_PRINT_DEBUG_PRE("value of rcx: %lx", regs[TRACER_REG_RCX]);
            set_length(trace, regs[TRACER_REG_RCX]);
            TRACER_PRINT_DEBUG_PRE("value of rax: %lx", regs[TRACER_REG_RAX]);
            trace->value = regs[TRACER_REG_RAX] & 0xFF;
            break;
        }
        case ZYDIS_MNEMONIC_STOSQ: {
				pr_info("found rep stosq");
            setRep(trace);
            setRepSize(trace, REP_SIZE_64);
            TRACER_PRINT_DEBUG_PRE("value of rcx: %lx", regs[TRACER_REG_RCX]);
            set_length(trace, regs[TRACER_REG_RCX]);
            TRACER_PRINT_DEBUG_PRE("value of rax: %lx", regs[TRACER_REG_RAX]);
            trace->value = regs[TRACER_REG_RAX];
            break;
        }
        default:
            TRACER_PRINT_ERROR(
                "unsupported rep prefix found in pre collection: %s",
                instruction->text);
        }
    }
#ifndef TRACER_COUNT_AMOUNT_TAKEN
#ifdef TRACER_COLLECT_TIME_PRE
    collect_time_pre(trace);
#endif
    collect_registers_pre(regs, rip_of_instruction, trace);
#ifdef TRACER_COLLECT_THREAD_ID
    collect_thread_id(trace);
#endif
#ifdef TRACER_COLLECT_VIRTUAL_ADDRESS
    collect_address(regs, instruction, trace);
#endif
#ifdef TRACER_USE_POST_HANDLER
    trace_addresses->trace_address[thread_index] = trace;
#endif
#endif

#ifdef TRACER_LOG_DEBUG_CONTEXT
    TRACER_PRINT_DEBUG_PRE("is in pre collector b/c of address 0x%lx",
                           instruction->runtime_address);
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
#ifdef TRACER_NOVA_SUPPORT
#ifdef TRACER_USERSPACE
    trace->in_kernel = 0;

#else
    trace->in_kernel = 1;
#endif

    trace->origin_address = rip_of_instruction;
    if (is_write(instruction)) {

#ifdef TRACER_TRACE_KERNEL
#endif
        trace->type = TYPE_WRITE;
    } else {
        trace->type = TYPE_READ;
    }
#endif

    long res = get_and_set_value_address(instruction, trace, valuebuffer);
#ifdef TRACER_MEASURE_HANDLERS
    long end_ticks = rdtsc_fence();
    /*	printf("adding %li ticks, end is %li and start is %li\n",
       end_ticks- start_ticks, end_ticks, start_ticks);

            counter += 1;
            if (end_ticks-start_ticks > 1000)  {
                    printf("got a large tick at numbre %li\n", counter);
            }*/
    measurements->pre_handler = end_ticks - start_ticks;

#endif
#ifdef TRACER_MEASURE_HANDLERS_THREADS

    long end_ticks = rdtsc();
    measurements->pre_handler_threads[thread_index] += end_ticks - start_ticks;

#endif
    return res;
}
