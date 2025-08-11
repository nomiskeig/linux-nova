#define _GNU_SOURCE
#ifndef __KERNEL__
#include "../../shared/collector.h"
#include "../../shared/context.h"
#include "../../shared/instruction.h"
#include "../../shared/logging.h"
#include "../../shared/patcher.h"
#include "../../shared/post.h"
#include "../../shared/pre.h"
#include "../../shared/rdtsc.h"
#include "../../shared/regs.h"
#include "../../shared/shared.h"
#include "../../shared/trampoline.h"
#include "inject.h"
#include "pthread.h"
#include <Zydis/Zydis.h>
#include <err.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>end
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#else
#include "config.h"
#include "context.h"
#include "instruction.h"
#include "logging.h"
#include "patcher.h"
#include "post.h"
#include "pre.h"
#include "pthread.h"
#include "register.h"
#include "regs.h"
#include "trampoline.h"
#include <Zydis.h>
#include <linux/slab.h>
extern long base_patch_address;
extern long alternate_stack_address;
#include <asm/io.h>
#include <linux/mm.h>

#endif
extern ThreadMappings *thread_mappings;
extern Tracebuffer *tracebuffer;
extern Valuebuffer *valuebuffer;
DisplacedInstructions *displaced_instructions;
extern ZydisDisassembledInstruction *disassembled_instruction;
extern int id_offset;
int hook_pthread_create;
int print_next = 0;
#ifdef TRACER_ENABLE_MEASUREMENTS
extern Measurements *measurements;
#endif
#ifdef TRACER_PREVENT_LIBC
extern int use_glibc[MAX_SUPPORTED_THREADS];
#endif
// see https://github.com/zyantific/zydis/issues/76 for zydis header files issue
//
#ifdef TRACER_LOG_PROTECTED_BUFFER
extern MmapInfo *mmap_info;
#endif
#ifdef TRACER_LOG_ACCESSES
extern FILE *trace_log;
#endif
#ifndef TRACER_USERSPACE
int offset = 0; // we put this up here to force the compiler to not use an
                // register for it, becuase otherwise its value is destroyed
                // accros the stub_function callkjkj
#endif
// TODO: maybe this should return a trace
// TODO: this has to be synchronized if there are multiple threads writing
//
//
//
//
#ifdef TRACER_LOG_ADDRESS_SPACE
void log_address_space() {
    int c;
    FILE *file = fopen("/proc/self/maps", "r");
    if (file) {
        while ((c = getc(file)) != EOF)
            putchar(c);
    }
    fclose(file);
}
#endif

#ifdef TRACER_LOG_PROTECTED_BUFFER
void log_buffer() {
    char *address = mmap_info->address;
    w printf("Address: %p", mmap_info->address);
    int size = mmap_info->length;
    int current = 0;
    while (current < TRACER_MAX_PROTECTED_LOG && current < size) {
        if (current % 8 == 0) {
            printf("\n%p: ", address + current);
        }
        printf("%02hhx ", address[current]);
        current += 1;
    }
    printf("\n");
}
#endif
#ifdef TRACER_USERSPACE
static void *install_trampoline_wrapper(void *args) {
    TrampInstallArguments *a = (TrampInstallArguments *)args;
    install_trampoline(a->rip, a->pkey, a->first_byte, a->is_tracing_following,
                       a->original_address);

    pthread_detach(pthread_self());
    return NULL;
}
#endif
int tracer_can_handle(long address) {
    // preveent crahs if we try to crash an invalid instruction before the
    // tracer is initialized like we do at the beginning of nova to
    // mount/initialize it

    TRACER_PRINT_DEBUG("is at beginning of tracer_can_handle");
    if (!displaced_instructions) {
        return 0;
    }
#ifdef TRACER_USERSPACE
    // make the page readable so we do not crash if we trace an invalid
    // instruction in userspace without it being a trampoline
    void *location_to_pick_page =
        (void *)((long)address - ((long)address % 4096));
    // TODO: we have to restore the original protections
    mprotect(location_to_pick_page, 4096 * 2,
             PROT_READ | PROT_WRITE | PROT_EXEC);
#endif
    TRACER_PRINT_DEBUG("before in tracer can handle\n");
    if (*(unsigned char *)address == 0xD5) {
        TRACER_PRINT_DEBUG("inside in tracer can handle\n");
        return 1;
    }
    TRACER_PRINT_DEBUG("after in tracer can handle\n");
    return get_displaced_location_info(address, displaced_instructions) != 0x0
               ? 1
               : 0;
}
void invalid_instr_signal_handler(int number, siginfo_t *info, void *ucontext) {
#ifdef TRACER_MEASURE_SIGNAL_HANDLER
    long ticks_handler_start = rdtsc();
#endif
#ifdef TRACER_PREVENT_LIBC
    use_glibc[thread_index] = 0;
#endif
    ucontext_t *uc = (ucontext_t *)ucontext;
    tracer_regs_t tracer_regs = uc->uc_mcontext.gregs;
    TRACER_PRINT_DEBUG("is in invalid handler\n");
    TRACER_PRINT_DEBUG_SIGNAL_HANDLER(
        "Is in invalid instruction signal handler from rip %lx and with sp %lx",
        (long)tracer_regs[TRACER_REG_RIP_DO_NOT_USE],
        (long)tracer_regs[TRACER_REG_RSP]);
    // search the list of displaced instructions and set rip
    // 0xEA means a thread is installing a trampoline, do not install a
    // trampoline and do also not run the normal signal handler, just spin
    // because the displaced value is not yet written; 0xD6 means that another
    // thread is installing the trampoline, and has already written the
    // displaced instruction, so just continue

    // we have to disable the pkey because it is set in the handler by default
    // and that means that we crash if we try to read the value (this does not
    // seem to be consistent when we do not do it, but i dont know why)
    // TODO: set the correct key
#ifdef TRACER_SUPPORT_CUSTOM_INVALID

    unsigned char *addres =
        (unsigned char *)tracer_regs[TRACER_REG_RIP_DO_NOT_USE];
    if (*(addres) == 0xD5) {
#ifndef TRACER_USERSPACE
        // pr_info("getting trace for fence, flush or hypercall");
#endif
        Trace *trace = get_next_trace();
        // fence
        if (*(addres + 1) == 0xEA) {
            // for now we can assume that the only fence we find is an sfence
            trace->type = TYPE_FENCE;
            trace->mnemonic = 1;
            TRACER_PRINT_DEBUG_NOVA("found fence");
        } else if (*(addres + 1) == 0xD6) {
            trace->type = TYPE_FLUSH;
            trace->mnemonic = 1;
            TRACER_PRINT_DEBUG_NOVA("found clwb");
            // we know that htis is followed by a clwb isntrution so we
            // disasseble it to get the address;
            ZyanUSize longest_length = 15;
            ZydisDisassembledInstruction instruction;
            ZyanStatus status = ZydisDisassembleIntel(
                /* machine_mode:    */ ZYDIS_MACHINE_MODE_LONG_64,
                /* runtime_address: */ tracer_regs[TRACER_REG_RIP_DO_NOT_USE] +
                    2,
                /* buffer:          */ (void *)addres + 2,
                /* length:          */ longest_length,
                /* instruction:     */ &instruction);

            if (instruction.info.mnemonic != ZYDIS_MNEMONIC_CLWB) {
                TRACER_PRINT_ERROR("Did not decode a clwb instruction");
            }
            if (!ZYAN_SUCCESS(status)) {
                TRACER_PRINT_ERROR(
                    "Could not decode the instruction in the signal handler");
            }
            collect_address(tracer_regs, &instruction, trace);
        } else if (*(addres + 1) == 0x06) {
            trace->type = TYPE_HYPERCALL;
            trace->value = 0l;
            trace->value = (long)tracer_regs[TRACER_REG_RBX];
            TRACER_PRINT_DEBUG("new value is %lx, %lx\n, ", trace->value,
                               tracer_regs[TRACER_REG_RBX]);
            TRACER_PRINT_DEBUG_NOVA("found hypercall");
        }
        tracer_regs[TRACER_REG_RIP_DO_NOT_USE] += 2;
        TRACER_PRINT_DEBUG("returning to program");
        return;
    }
#endif
#ifdef TRACER_USERSPACE
    int used_key = 1; // atoi(getenv("TRACER_PKEY"));
#else
    // int used_key = 1;
#endif
    // disable_pkey(used_key);
    unsigned char volatile value =
        *(char *)tracer_regs[TRACER_REG_RIP_DO_NOT_USE];
    TRACER_PRINT_DEBUG_SIGNAL_HANDLER("Found value of %hhx at the rip", value);
#ifdef TRACER_DO_NOT_USE_SIG_HANDLER
    if (value == 0xEA || value == 0xD6) {
        return;
    }

#endif
    while (value == 0xEA) {
        value = *(volatile char *)tracer_regs[TRACER_REG_RIP_DO_NOT_USE];
        /*TRACER_PRINT_DEBUG_SIGNAL_HANDLER(
            "waiting for value at %lx, currently value %hhx",
            tracer_regs[TRACER_REG_RIP_DO_NOT_USE], value);*/
        /*	TRACER_PRINT_DEBUG_SIGNAL_HANDLER(
"Returning from invalid signal handler because the "
"displaced "
"instruction is not yet written for address %p",
(void*)tracer_regs[TRACER_REG_RIP_DO_NOT_USE]);
*/
    }

    TRACER_PRINT_DEBUG_SIGNAL_HANDLER(" new, Found value of %hhx at the rip",
                                      value);
    DisplacedInstructionLocation *displaced_info = get_displaced_location_info(
        (long)tracer_regs[TRACER_REG_RIP_DO_NOT_USE], displaced_instructions);
    if (displaced_info && displaced_info->type == TRACER_UNDECIDED) {
#ifdef TRACER_USERSPACE
        int index = get_tid(pthread_self()) - id_offset;
#else
        int index = 0;
#endif
        TRACER_PRINT_DEBUG_SIGNAL_HANDLER("Setting instrution to do not trace");
        set_following_must_be_traced(
            thread_mappings->mappings[index].following_info.original_address,
            TRACER_DO_NOT_TRACE);

        /*for (int i = 0; i < MAX_SUPPORTED_THREADS; i++) {
            if (thread_mappings->mappings[i]
                    .following_info.expected_new_address ==
                tracer_regs[TRACER_REG_RIP_DO_NOT_USE]) {
                set_following_must_be_traced(
                    thread_mappings->mappings[i]
                        .following_info.original_address,
                    TRACER_DO_NOT_TRACE);
                break;
            }
        }
        */
    }
#ifdef TRACER_MEASURE_SIGNAL_HANDLER
    tracer_core_handler(number, info, ucontext, 1, ticks_handler_start);
#else
    tracer_core_handler(number, info, ucontext, 1);
#endif
}

void pku_signal_handler(int number, siginfo_t *info, void *ucontext) {
    TRACER_PRINT_DEBUG("is in signal handler\n");
#ifdef TRACER_MEASURE_SIGNAL_HANDLER
    long ticks_handler_start = rdtsc();
#endif
#ifdef TRACER_MEASURE_KERNEL_TIME
    long ticks = rdtsc();
    disable_pkey(1);
    measurements->signal_kernel_pre = ticks;
    enable_pkey(1);
#endif

#ifdef TRACER_PREVENT_LIBC
    use_glibc[thread_index] = 0;
#endif
    ucontext_t *uc = (ucontext_t *)ucontext;
    tracer_regs_t tracer_regs = uc->uc_mcontext.gregs;
    TRACER_PRINT_DEBUG_SIGNAL_HANDLER(
        "Is in pku instruction signal handler from rip %px and with sp %p and "
        "si_code %i",
        (void *)tracer_regs[TRACER_REG_RIP_DO_NOT_USE],
        (void *)tracer_regs[TRACER_REG_RSP], info->si_code);
#ifdef TRACER_USERSPACE
    //   int index = get_tid(pthread_self()) - id_offset;
#else
//    int index = 0;
#endif
    int is_tracing_following = 0;
    int index_of_original_thread = -1;
    if (thread_mappings->mappings[thread_index]
            .following_info.expected_new_address ==
        tracer_regs[TRACER_REG_RIP_DO_NOT_USE]) {
        is_tracing_following = 1;
    }
    for (int i = 0; i < MAX_SUPPORTED_THREADS; i++) {
        if (thread_mappings->mappings[i].following_info.expected_new_address ==
            tracer_regs[TRACER_REG_RIP_DO_NOT_USE]) {
            is_tracing_following = 1;
            index_of_original_thread = i;
            TRACER_PRINT_DEBUG_SIGNAL_HANDLER(
                "set do trace for address %lx",
                thread_mappings->mappings[index_of_original_thread]
                    .following_info.original_address);
            set_following_must_be_traced(
                thread_mappings->mappings[index_of_original_thread]
                    .following_info.original_address,
                TRACER_DO_TRACE);
            break;
        }
    }
    ZyanUSize longest_length = 15;
    ZydisDisassembledInstruction instruction;
    ZyanStatus status = ZydisDisassembleIntel(
        /* machine_mode:    */ ZYDIS_MACHINE_MODE_LONG_64,
        /* runtime_address: */ tracer_regs[TRACER_REG_RIP_DO_NOT_USE],
        /* buffer:          */ (void *)tracer_regs[TRACER_REG_RIP_DO_NOT_USE],
        /* length:          */ longest_length,
        /* instruction:     */ &instruction);

    if (!ZYAN_SUCCESS(status)) {
        TRACER_PRINT_ERROR(
            "Could not decode the instruction in the signal handler");
    }
    if (is_write(&instruction) == 0) {
        //pr_info("setting print next");
        print_next = 1;
    }
    if (is_write(&instruction) == 0 || print_next == 1) {
        //pr_info("print_next is %x", print_next);
        if (is_write(&instruction) == 1) {
            print_next = 0;
        }
        long address = 0;
        for (int i = 0; i < instruction.info.operand_count; i++) {
            ZydisDecodedOperand *op = &instruction.operands[i];
            if (op->type != ZYDIS_OPERAND_TYPE_MEMORY) {
                continue;
            }
            TracerRegister base = get_offset_of_reg(op->mem.base);
            address = tracer_regs[tracer_reg_to_specific_reg_index(base)];
            if (op->mem.disp.size > 0) {
                address += op->mem.disp.value;
            }
            if (op->mem.index != ZYDIS_REGISTER_NONE) {
                TracerRegister index = get_offset_of_reg(op->mem.index);
                address += op->mem.scale *
                           tracer_regs[tracer_reg_to_specific_reg_index(index)];
            }
            // TRACER_PRINT_DEBUG(
             //    "next print should be the address of virtual address");
        //TRACER_PRINT_DEBUG("trace->virtual_address is %p, and trace is
         //   %p",
            //                   (void *)&trace->virtual_address, (void
            //                   *)trace);
            //
        }

        int pkru;
        asm("mov $0x0, %%ecx\n\t"
            "rdpkru\n\t"
            "mov %%eax, %0"
            : "=m"(pkru)::"ecx", "eax", "edx");

/*      pr_info("instruction is %s and is write, from rip %lx, address is "
                "%lx, pkru is %x",
                instruction.text, tracer_regs[TRACER_REG_RIP_DO_NOT_USE],
                (page_to_phys(virt_to_page((void *)address)) - 134217728) |
                    (address & 0xFFF),
                pkru); // trace->address =
		// 
		// */
    }

    // support for nova, need to trace sfence, clwb. To do that, we use an
    // invalid instruction, 0xD5. Then we have at least 4 bytes that we can use
    // to send meta data
    // second byte has to also contain invalid values:
    // 0xEA is fence
    // 0xD6 is clwb
    // TODO: this needs to be incoperated with the trampolines

    // thread_mappings->mappings[thread_index].following_info.expected_new_address
    // = 0;
    TRACER_PRINT_DEBUG_SIGNAL_HANDLER("is_tracing following: %i",
                                      is_tracing_following);
    if (info->si_code != SEGV_PKUERR && is_tracing_following == 0) {
        // just crash it so that we get a crash dump
        TRACER_PRINT_DEBUG_SIGNAL_HANDLER(
            "Err cause not pku err wiht the following context and si_code %i",
            info->si_code);
#ifdef TRACER_LOG_DEBUG_CONTEXT
        tracer_regs_t tracer_regs = uc->uc_mcontext.gregs;
        TRACER_PRINT_DEBUG_CONTEXT("RAX: %016llx", tracer_regs[TRACER_REG_RAX]);
        TRACER_PRINT_DEBUG_CONTEXT("RCX: %016llx", tracer_regs[TRACER_REG_RCX]);
        TRACER_PRINT_DEBUG_CONTEXT("RDX: %016llx", tracer_regs[TRACER_REG_RDX]);
        TRACER_PRINT_DEBUG_CONTEXT("RBX: %016llx", tracer_regs[TRACER_REG_RBX]);
        TRACER_PRINT_DEBUG_CONTEXT("RSI: %016llx", tracer_regs[TRACER_REG_RSI]);
        TRACER_PRINT_DEBUG_CONTEXT("RDI: %016llx", tracer_regs[TRACER_REG_RDI]);
        TRACER_PRINT_DEBUG_CONTEXT("RSP: %016llx", tracer_regs[TRACER_REG_RSP]);
        TRACER_PRINT_DEBUG_CONTEXT("RBP: %016llx", tracer_regs[TRACER_REG_RBP]);
        TRACER_PRINT_DEBUG_CONTEXT("R8: %016llx", tracer_regs[TRACER_REG_R8]);
        TRACER_PRINT_DEBUG_CONTEXT("R9: %016llx", tracer_regs[TRACER_REG_R9]);
        TRACER_PRINT_DEBUG_CONTEXT("R10: %016llx", tracer_regs[TRACER_REG_R10]);
        TRACER_PRINT_DEBUG_CONTEXT("R11: %016llx", tracer_regs[TRACER_REG_R11]);
        TRACER_PRINT_DEBUG_CONTEXT("R12: %016llx", tracer_regs[TRACER_REG_R12]);
        TRACER_PRINT_DEBUG_CONTEXT("R13: %016llx", tracer_regs[TRACER_REG_R13]);
        TRACER_PRINT_DEBUG_CONTEXT("R14: %016llx", tracer_regs[TRACER_REG_R14]);
        TRACER_PRINT_DEBUG_CONTEXT("R15: %016llx", tracer_regs[TRACER_REG_R15]);
#endif
        // printf("RAX: %016llx\n", tracer_regs[TRACER_REG_RAX]);
        // printf("RCX: %016llx\n", tracer_regs[TRACER_REG_RCX]);
        // printf("RDX: %016llx\n", tracer_regs[TRACER_REG_RDX]);
        // printf("RBX: %016llx\n", tracer_regs[TRACER_REG_RBX]);
        // printf("RSI: %016llx\n", tracer_regs[TRACER_REG_RSI]);
        // printf("RDI: %016llx\n", tracer_regs[TRACER_REG_RDI]);
        // printf("RSP: %016llx\n", tracer_regs[TRACER_REG_RSP]);
        // printf("RBP: %016llx\n", tracer_regs[TRACER_REG_RBP]);
        // printf("R8: %016llx\n", tracer_regs[TRACER_REG_R8]);
        // printf("R9: %016llx\n", tracer_regs[TRACER_REG_R9]);
        // printf("R10: %016llx\n", tracer_regs[TRACER_REG_R10]);
        // printf("R11: %016llx\n", tracer_regs[TRACER_REG_R11]);
        // printf("R12: %016llx\n", tracer_regs[TRACER_REG_R12]);
        // printf("R13: %016llx\n", tracer_regs[TRACER_REG_R13]);
        // printf("R14: %016llx\n", tracer_regs[TRACER_REG_R14]);
        // printf("R15: %016llx\n", tracer_regs[TRACER_REG_R15]);
        //  crash to program so we get a crash dump
        /*printf("the last 10 adresses are: \n");
        for (int i = 0; i < 10; i++) {
            printf("address %i to last is %p\n", i,
                   &tracebuffer->traces[tracebuffer->amount - i]);
        }
        printf("the last address is %p\n", tracebuffer->next_trace_address);
        */
        /*
                long rip_address = tracer_regs[TRACER_REG_RIP_DO_NOT_USE];
                void *rip_start =
                    (void *)(rip_address - (((long long)rip_address) %
        getpagesize()));
                // Unprotect two pages in case the function is on a page
        boundary mprotect(rip_start, getpagesize() * 2, PROT_READ | PROT_WRITE |
        PROT_EXEC); ZydisDisassembledInstruction instr; ZyanUSize local_offset =
        0; int offset = 0; long local_runtime_address =
        tracer_regs[TRACER_REG_RIP_DO_NOT_USE]; while
        (ZYAN_SUCCESS(ZydisDisassembleIntel( ZYDIS_MACHINE_MODE_LONG_64,
                     tracer_regs[TRACER_REG_RIP_DO_NOT_USE] +
                        local_offset,

                    (void *)((long)tracer_regs[TRACER_REG_RIP_DO_NOT_USE] +
                             local_offset),
                   40,
                     &instr))) {
                    char bytes[15];
        #ifdef TRACER_USERSPACE
                    for (int i = 0; i < instr.info.length; i++) {
        #else
                    for (int i = 0; i < instr->info.length; i++) {
        #endif
                        bytes[i] = *((char *)(TRACER_REG_RIP_DO_NOT_USE +
        local_offset + OFFSET_FROM_BUFFER_START + i));
                    }
                    local_offset += instr.info.length;
                    local_runtime_address += instr.info.length;
                    for (int i = 0; i < instr.info.length; i++) {
                        //printf("%02hhx ", bytes[i]);
                    }
                }
        */
        *(volatile int *)0x1 = 4;
        TRACER_PRINT_ERROR(
            "Got into the signal handler, but is not a PKU fault, got "
            "code: %i, rip is at %llx",
            info->si_code, tracer_regs[TRACER_REG_RIP_DO_NOT_USE]);

#ifdef TRACER_PREVENT_LIBC
        use_glibc[thread_index] = 1;
#endif
        return;
    }
#ifdef TRACER_USE_TRAMPOLINES
#ifdef TRACER_USERSPACE
    // TODO: this shoudl use the correct pkey, but using it destroys  w
    int used_key = 1; // atoi(getenv("TRACER_PKEY"));

#else
    // TODO: use the correct key everywhere, not just one
    int used_key = 1;
#endif
    char *address = (char *)tracer_regs[TRACER_REG_RIP_DO_NOT_USE];
    disable_pkey(used_key);
#ifdef TRACER_USERSPACE

    void *location_to_pick_page =
        (void *)((long)address - ((long)address % 4096));
    // TODO: we have to restore the original protections
    mprotect(location_to_pick_page, 4096 * 2,
             PROT_READ | PROT_WRITE | PROT_EXEC);
#endif
    TRACER_PRINT_DEBUG("address is %lx", (long unsigned int)address);
#ifndef TRACER_USERSPACE
    // we disable the write protection globally while we install the
    // tramoline. Afterwards we reinstall it
    disable_write_protection();
#endif
    TRACER_PRINT_DEBUG("after disable write protection\n");
    volatile unsigned char head = *address;

    TRACER_PRINT_DEBUG("after disable write protect 2\n");
    if (head == 0xEA || head == 0xD6 || head == 0xE9) {
        // we have to return here, i think there is a case where this can read a
        // valid head, i.e. the orginal instruction, then put ea there after the
        // installer thread overwrites the 0xEA byte, which results in this
        // threads waiting for the byte to change
#ifdef TRACER_PREVENT_LIBC
        use_glibc[thread_index] = 1;
#endif
#ifndef TRACER_USERSPACE
        enable_write_protection();
#endif
        return;
    }
    // 0xEA means a thread is installing a trampoline, do not install a
    // trampoline and do also not run the normal signal handler, just spin
    // because the displaced value is not yet written 0xD6 means that
    // another thread is installing the trampoline, and has already written
    // the displaced instruction, so just continue
    //
    if (!__atomic_compare_exchange_1(address, (void *)&head, 0xEA, 0,
                                     __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        TRACER_PRINT_DEBUG("returning bece other thread is already "
                           "isntalling trampoline\n");
#ifdef TRACER_PREVENT_LIBC
        use_glibc[thread_index] = 1;
#endif
#ifndef TRACER_USERSPACE
        enable_write_protection();
#endif
#ifdef TRACER_MEASURE_SIGNAL_HANDLER
        long end_ticks = rdtsc();
        measurements->fault_signal_handler[thread_index] +=
            end_ticks - ticks_handler_start;

#endif

        return;
    }
    TRACER_PRINT_DEBUG("Successfully changed the head\n");
    // prevent the case where a thread reads a value another thread already
    // wrote because it and does already install the signal handler, without
    // this, this thread would also try to write the trampoline
    if (head == 0xEA || head == 0xD6) {
        TRACER_PRINT_DEBUG("returning becuase of EA or D6\n");
#ifdef TRACER_PREVENT_LIBC
        use_glibc[thread_index] = 1;
#endif
#ifndef TRACER_USERSPACE
        enable_write_protection();
#endif
#ifdef TRACER_MEASURE_SIGNAL_HANDLER
        long end_ticks = rdtsc();
        measurements->fault_signal_handler[thread_index] +=
            end_ticks - ticks_handler_start;

#endif
        return;
    }

    TRACER_PRINT_DEBUG("installing trampoline\n");
    int is_following;
    tracer_follow_type follow_type;
    long location = find_displaced_location(
        (long)address, displaced_instructions, &is_following, &follow_type);
    // follow_type == TRACER_UNDECIDED is a condition here because only the
    if (is_tracing_following) {
        TRACER_PRINT_DEBUG_SIGNAL_HANDLER(
            "the oringal address is %lx while the new rip is %lx",
            thread_mappings->mappings[index_of_original_thread]
                .following_info.original_address,
            (long unsigned int)tracer_regs[TRACER_REG_RIP_DO_NOT_USE]);
        TRACER_PRINT_DEBUG_SIGNAL_HANDLER(
            "setting to do trace with address %lx and length %lx",
            thread_mappings->mappings[index_of_original_thread]
                .following_info.probe_address,
            thread_mappings->mappings[index_of_original_thread]
                .following_info.length);
        set_following_must_be_traced(
            tracer_regs[TRACER_REG_RIP_DO_NOT_USE],
            // thread_mappings->mappings[index_of_original_thread]
            //    .following_info.original_address,
            TRACER_DO_TRACE);
#ifdef TRACER_TRACE_SUBSEQUENT
        *((int *)((thread_mappings->mappings[index_of_original_thread]
                       .following_info.probe_address))) =
            thread_mappings->mappings[index_of_original_thread]
                .following_info.length;
    }

    // TODO: this case must be handler in the post handler of the original
    // instructoin handler, in the case that we do not trace the following
    // isntruction
    else {
        // this should not be required as the do not trace thingy is set in
        // the probe
        /*
        DisplacedInstructionLocation *info =
            get_displaced_location_info((long)address,
        displaced_instructions); if (info && info->type == TRACER_UNDECIDED)
        { set_following_must_be_traced(
                thread_mappings->mappings[index_of_original_thread]
                    .following_info.original_address,
                TRACER_DO_NOT_TRACE);
        }
        */
#endif
    }

	// TODO: this is probably broken
    DisplacedInstructionLocation *loc =
        get_displaced_location_info((long)address, displaced_instructions);
    if (loc && !loc->trampolineInstalled) {
        // catch the case where no trampoline is found and the thread continues
        // with the next instructoin, traps and then tries to install another
        // trampoline. This differs from the case where a trampoline can be
        // found because there, the isntauction head is reaplced with an invalid
        // instruction so we do not get here also other threads dont wait in the
        // loop below because the installer thread will always eventually change
        // the byte from 0xEA, i hope
        // reinstall the original valuue that we replaced with 0xEA, that should
        // be fine

        *address = head;

        tracer_core_handler(number, info, ucontext, 0);
        return;
    }

    TRACER_PRINT_DEBUG("installing trampoline 2, locatoin is %lx", location);
    if (location == 0) {
        TRACER_PRINT_DEBUG("installing trampoline because locatoin is 0");
        // dont instlall the trampoline if there is already a trampoline
        // before this one that would interfere
        // TODO: make a different method to check the availablily of the
        // trampoline (instead of find_displaced_location)
        long oringal_address = tracer_regs[TRACER_REG_RIP_DO_NOT_USE];
        if (is_tracing_following) {
            oringal_address =
                thread_mappings->mappings[index_of_original_thread]
                    .following_info.original_address +
                thread_mappings->mappings[index_of_original_thread]
                    .following_info.offset_from_original_address;
        }

#ifdef TRACER_MEASURE_SIGNAL_HANDLER
        long start_ticks = rdtsc();
#endif
#ifdef TRACER_USE_SEPERATE_THREAD

        pthread_t t;
        TrampInstallArguments *tramp_args =
            malloc(sizeof(TrampInstallArguments));
        tramp_args->rip = (long)tracer_regs[TRACER_REG_RIP_DO_NOT_USE];
        tramp_args->pkey = used_key;
        tramp_args->first_byte = head;
        tramp_args->is_tracing_following = is_tracing_following;
        tramp_args->original_address = oringal_address;

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        pthread_create(&t, &attr, install_trampoline_wrapper, tramp_args);
        return;

#else
        install_trampoline((long)tracer_regs[TRACER_REG_RIP_DO_NOT_USE],
                           used_key, head, is_tracing_following,
                           oringal_address);
#endif
#ifdef TRACER_MEASURE_SIGNAL_HANDLER
        long end_ticks = rdtsc();
        measurements->install_trampoline[thread_index] +=
            end_ticks - start_ticks;
#endif
    }
    TRACER_PRINT_DEBUG("after installing trampoline 2");
#ifdef TRACER_LOG_ADDRESS_SPACE
    log_address_space();
#endif
#ifdef TRACER_PREVENT_LIBC
    use_glibc[thread_index] = 1;
#endif
#ifndef TRACER_USERSPACE
    enable_write_protection();
#endif
#ifdef TRACER_MEASURE_SIGNAL_HANDLER
    long end_ticks = rdtsc();
    measurements->fault_signal_handler[thread_index] +=
        end_ticks - ticks_handler_start;

#endif
    TRACER_PRINT_DEBUG("after installing trampoline 2");
    return;
#endif
#ifdef TRACER_MEASURE_SIGNAL_HANDLER
    tracer_core_handler(number, info, ucontext, 0, ticks_handler_start);
#else
#ifdef TRACER_ENABLE_HANDLER_THREADS
    measurements->fault_signal_handler[thread_index] += end_ticks - start_ticks;
#endif
    tracer_core_handler(number, info, ucontext, 0);

#endif
}
static void __attribute__((__noinline__)) stub_caller(tracer_regs_t tracer_regs,
                                                      long value_address,
                                                      ucontext_t *uc,
                                                      long start_address) {
#ifdef TRACER_USERSPACE
    asm volatile("movq %0, %%rdi\n\t"
                 "movq %1, %%rsi\n\t"
                 "movq %2, %%rdx\n\t"
                 "callq %3\n\t"
                 :
                 : "mr"(tracer_regs), "mr"(value_address),
                   "m"(uc->uc_mcontext.fpregs),
#ifdef TRACER_USERSPACE
                   "Im"(start_address)
                 : "rdi", "rsi", "rdx");
#else
                       "Ir"(start_address);
#endif
#else
    asm volatile("sub $0xC8, %%rsp\n\t"
                 "pushfq\n\t"
                 "push %%rax\n\t"
                 "push %%rbx\n\t"
                 "push %%rcx\n\t"
                 "push %%rdx\n\t"
                 "push %%rdi\n\t"
                 "push %%rsi\n\t"
                 "push %%r8\n\t"
                 "push %%r9\n\t"
                 "push %%r10\n\t"
                 "push %%r11\n\t"
                 "push %%r12\n\t"
                 "push %%r13\n\t"
                 "push %%r14\n\t"
                 "push %%r15\n\t"
                 "mov %0, %%rdi\n\t"
                 "mov %1, %%rsi\n\t"
                 "mov %2, %%rdx\n\t"
                 "callq %3\n\t"
                 "pop %%r15\n\t"
                 "pop %%r14\n\t"
                 "pop %%r13\n\t"
                 "pop %%r12\n\t"
                 "pop %%r11\n\t"
                 "pop %%r10\n\t"
                 "pop %%r9\n\t"
                 "pop %%r8\n\t"
                 "pop %%rsi\n\t"
                 "pop %%rdi\n\t"
                 "pop %%rdx\n\t"
                 "pop %%rcx\n\t"
                 "pop %%rbx\n\t"
                 "pop %%rax\n\t"
                 "popfq\n\t"
                 "add $0xc8, %%rsp\n\t"
                 :
                 : "mr"(tracer_regs), "mr"(value_address),
                   "m"(uc->uc_mcontext.fpregs),
#ifdef TRACER_USERSPACE
                   "Im"(start_address));
#else
                   "Ir"(start_address));
#endif
#endif
}

#ifdef TRACER_MEASURE_SIGNAL_HANDLER
void tracer_core_handler(int number, siginfo_t *info, void *ucontext,
                         int from_invalid, long start_ticks) {
#else
void tracer_core_handler(int number, siginfo_t *info, void *ucontext,
                         int from_invalid) {
#endif
    TRACER_PRINT_DEBUG_SIGNAL_HANDLER("Is in tracer core handler");
    // disable the pkey since the default key in the signal handler seems to
    // disable the access
    // TODO: set correct pkey
#ifdef TRACER_USERSPACE
    int used_key = 1; // atoi(getenv("TRACER_PKEY"));
#else
    int used_key = 1;
#endif
    disable_pkey(used_key);

    // restore the avx registers
    ucontext_t *uc = (ucontext_t *)ucontext;
    tracer_regs_t tracer_regs = uc->uc_mcontext.gregs;
    // according to the man page
    // (https://man7.org/linux/man-pages/man7/pkeys.7.html), the PKRU is
    // overwritten while we are in this signal handler and restored
    // afterwards. Thus, we do not have to worry about disabling/enabling
    // the MPK here. But, we have to disable the one that is set by default,
    // since we cannot write data if we do not do that
    TRACER_PRINT_DEBUG_CONTEXT("RIP: %lx",
                               (long)tracer_regs[TRACER_REG_RIP_DO_NOT_USE]);
    long location_of_instruction = (long)tracer_regs[TRACER_REG_RIP_DO_NOT_USE];
    // if is_following != 0, this indicates that the instruction just
    // needs to be executed, either because it was hit as a
    // jump target or when the instruction was just patched
    // for the trampoline but the signal handler returned to
    // that instruction
    // we initialize the value with 0 since when we dont use trampolines
    // there is no way for us to hit the following instruction
    int is_following = 0;
    tracer_follow_type following_must_be_traced = TRACER_DO_TRACE;
#ifdef TRACER_USE_TRAMPOLINES
    if (from_invalid) {
        // if we come from an invalid instruction, we have to search the
        // list of displaced instruction and find the original bytes

        // TODO: this is broken in pthread szenarios, as
        // location_of_instruction changes with every invokation
        long original_loc = location_of_instruction;
        location_of_instruction = find_displaced_location(
            location_of_instruction, displaced_instructions, &is_following,
            &following_must_be_traced);

#ifndef TRACER_TRACE_SUBSEQUENT
        following_must_be_traced = TRACER_DO_NOT_TRACE;
#else
        following_must_be_traced =
            get_displaced_location_info(original_loc, displaced_instructions)
                ->type;
#endif

        while (following_must_be_traced == TRACER_UNDECIDED) {
            TRACER_PRINT_DEBUG_SIGNAL_HANDLER("spinning at address %lx",
                                              original_loc);
            following_must_be_traced = get_displaced_location_info(
                                           original_loc, displaced_instructions)
                                           ->type;
        }
        // we spin here because if an other thread is currently
        // installing the trampoline and we get here we do not know
        // whether we have to trace the instruction, so we wait until
        // the other thread decides this by running the trampoline
        // }
    }
#endif
    TRACER_PRINT_DEBUG_CONTEXT("Instruction: %li", location_of_instruction);
    TRACER_PRINT_DEBUG_NOVA("Instruction: %li", location_of_instruction);

    // see
    // https://stackoverflow.com/questions/14698350/x86-64-asm-maximum-bytes-for-an-instruction
    ZyanUSize longest_length = 15;
    ZydisDisassembledInstruction instruction;
    ZyanStatus status = ZydisDisassembleIntel(
        /* machine_mode:    */ ZYDIS_MACHINE_MODE_LONG_64,
        /* runtime_address: */ tracer_regs[TRACER_REG_RIP_DO_NOT_USE],
        /* buffer:          */ (void *)location_of_instruction,
        /* length:          */ longest_length,
        /* instruction:     */ &instruction);

    if (!ZYAN_SUCCESS(status)) {
        TRACER_PRINT_ERROR(
            "Could not decode the instruction in the signal handler");
    }

    if (is_write(&instruction) == 0) {
        //pr_info("this should not be a read a1: %s", instruction.text);
    }
#ifdef TRACER_LOG_DEBUG_SIGNAL_HANDLER
    int length = instruction.info.length;
    TRACER_PRINT_DEBUG_SIGNAL_HANDLER(
        "Address of instruction to trace : 0x%016lx"
        ", name: %s, size: %i",
        (long)tracer_regs[TRACER_REG_RIP_DO_NOT_USE], instruction.text, length);
    TRACER_PRINT_DEBUG("Raw bytes: ");
    for (int i = 0; i < length; i++) {
    }
#ifdef TRACER_USERSPACE
    TRACER_PRINT_DEBUG("\n");
#endif
#endif

#ifdef TRACER_LOG_DEBUG_CONTEXT
    TRACER_PRINT_DEBUG_CONTEXT("RAX: %016llx", tracer_regs[TRACER_REG_RAX]);
    TRACER_PRINT_DEBUG_CONTEXT("RCX: %016llx", tracer_regs[TRACER_REG_RCX]);
    TRACER_PRINT_DEBUG_CONTEXT("RDX: %016llx", tracer_regs[TRACER_REG_RDX]);
    TRACER_PRINT_DEBUG_CONTEXT("RBX: %016llx", tracer_regs[TRACER_REG_RBX]);
    TRACER_PRINT_DEBUG_CONTEXT("RSI: %016llx", tracer_regs[TRACER_REG_RSI]);
    TRACER_PRINT_DEBUG_CONTEXT("RDI: %016llx", tracer_regs[TRACER_REG_RDI]);
    TRACER_PRINT_DEBUG_CONTEXT("RSP: %016llx", tracer_regs[TRACER_REG_RSP]);
    TRACER_PRINT_DEBUG_CONTEXT("RBP: %016llx", tracer_regs[TRACER_REG_RBP]);
    TRACER_PRINT_DEBUG_CONTEXT("R8: %016llx", tracer_regs[TRACER_REG_R8]);
    TRACER_PRINT_DEBUG_CONTEXT("R9: %016llx", tracer_regs[TRACER_REG_R9]);
    TRACER_PRINT_DEBUG_CONTEXT("R10: %016llx", tracer_regs[TRACER_REG_R10]);
    TRACER_PRINT_DEBUG_CONTEXT("R11: %016llx", tracer_regs[TRACER_REG_R11]);
    TRACER_PRINT_DEBUG_CONTEXT("R12: %016llx", tracer_regs[TRACER_REG_R12]);
    TRACER_PRINT_DEBUG_CONTEXT("R13: %016llx", tracer_regs[TRACER_REG_R13]);
    TRACER_PRINT_DEBUG_CONTEXT("R14: %016llx", tracer_regs[TRACER_REG_R14]);
    TRACER_PRINT_DEBUG_CONTEXT("R15: %016llx", tracer_regs[TRACER_REG_R15]);
#endif
#ifdef TRACER_USERSPACE
    int offset = OFFSET_FROM_BUFFER_START;

#else
    offset = OFFSET_FROM_BUFFER_START;
#endif
    // i do not think that we want to trace clwb instruction even though its
    // technically a read
    if (instruction.info.mnemonic == ZYDIS_MNEMONIC_NOP ||
        instruction.info.mnemonic == ZYDIS_MNEMONIC_CLWB) {
        is_following = 1;
        following_must_be_traced = TRACER_DO_NOT_TRACE;
    }
    // leave the first 24 bytes for temp storage

    used_regs used = get_used_registers(&instruction);
    used_regs modified = get_modified_registers(&instruction);
    AddressReg valueReg =
        get_register_for_trace_value_address(&instruction, used | modified);
#ifdef TRACER_USERSPACE
    long base_patch_address =
        thread_mappings->mappings[get_tid(pthread_self()) - id_offset]
            .buffer_address;
#endif

    TRACER_PRINT_DEBUG("Address of the buffer is %px",
                       (void *)base_patch_address);
    // PERF: patch_fun_start needs only to be called once
    offset += patch_fun_start((char *)base_patch_address + offset,
                              base_patch_address);

// PERF: xrstor only needs to be executed for the instruction that need
// the x/y/zmm registers
#ifdef TRACER_USERSPACE
    offset += patch_xrstor((char *)base_patch_address + offset);
#endif
    ContextInfo context_info;
    if (is_following == 0 || following_must_be_traced == TRACER_DO_TRACE) {
        offset += patch_create_context(
            base_patch_address + offset, valueReg, used, 1, 0, modified,
            (long *)base_patch_address, &context_info,
            get_actual_used_registers(&instruction));
        offset += patch_nop((char *)base_patch_address + offset);
    }
    TracerRegister reg = address_reg_to_tracer_reg(valueReg);
    used |= reg;

    if (is_write(&instruction)) {
#ifndef TRACER_LOG_ACCESSES
#ifdef TRACER_COLLECT_VALUE
        TRACER_PRINT_DEBUG("is_following: %i\n", is_following);
        if (is_following == 0 || following_must_be_traced == TRACER_DO_TRACE) {
            offset += patch_collect_instructions(
                (unsigned long)(base_patch_address + offset),
                (char *)location_of_instruction, &instruction, valueReg, used,
                0);
            offset += patch_nop((char *)base_patch_address + offset);
            offset += patch_nop((char *)base_patch_address + offset);
        }
#endif
#else
        CollectionInfo collectionInfo;
#ifdef TRACER_COLLECT_VALUE
        TRACER_PRINT_DEBUG(
            "is_following is %i before patch collect instruction\n");
        if (is_following == 0 || following_must_be_traced == TRACER_DO_TRACE) {
            offset += patch_collect_instructions(
                (unsigned long)base_patch_address + offset,
                (char *)location_of_instruction, &instruction, valueReg, used,
                0, &collectionInfo);
        }
#endif
        offset += patch_nop((char *)base_patch_address + offset);
        TRACER_PRINT_DEBUG_SIGNAL_HANDLER(
            "Patched the collection intrustruction");
        char implemented =
            collectionInfo.implemented == TRACER_STATE_IMPLEMENTED ? 'y' : 'n';
        char load_write = collectionInfo.type == TRACER_TYPE_LOAD ? 'l' : 'w';
        fprintf(trace_log,
                "Thread: %i, Address: %lx, Status: %c, Type:  %c, Length: %i, "
                "OpCode: %hhx, "
                "Instr:  %s\n",
                gettid(), instruction.runtime_address, implemented, load_write,
                instruction.info.length, instruction.info.opcode,
                instruction.text);
#endif
    }
    if (is_following != 0) {
        // we create a 'fake' probe site with the instruction we actually
        // care about
        ProbeSite probe_site;
        probe_site.instructions[0] = &instruction;
        probe_site.address_of_instruction = (char *)location_of_instruction;
        probe_site.num_instructions = 1;
        probe_site.rip_of_instruction =
            (char *)tracer_regs[TRACER_REG_RIP_DO_NOT_USE];

        // probe_site.address_of_instruction is unnessecary here
        TRACER_PRINT_DEBUG_PATCHER(
            "patching execution because of following instrution");
        long patched_address;
        if (following_must_be_traced == TRACER_DO_TRACE) {

            offset += patch_execute_single_instruction(
                (char *)base_patch_address + offset,
                (char *)location_of_instruction, &instruction,
                tracer_regs[TRACER_REG_RIP_DO_NOT_USE], &patched_address);
        } else {
            offset +=
                patch_execute(&probe_site, (char *)base_patch_address + offset,
                              (long *)base_patch_address, 1,
                              location_of_instruction, &context_info, 0, 0, 0);
            if (get_jump_type(&instruction) == TRACER_JUMP_COND) {
                TRACER_PRINT_DEBUG_SIGNAL_HANDLER("Pathing a conditional jump");
                offset += patch_cond_jump_for_handler(
                    &probe_site, (char *)base_patch_address + offset,
                    base_patch_address);
            }
        }

    } else {
        // TODO: this needs rework to be incoporated with the case of the
        // instruction that is following
        long patched_address;
        offset += patch_execute_single_instruction(
            (char *)(base_patch_address + offset),
            (char *)location_of_instruction, &instruction,
            tracer_regs[TRACER_REG_RIP_DO_NOT_USE], &patched_address);
        if (get_used_registers(&instruction) & RBP) {
            // we pushed the rbp onto the stack before, lets restore it
            //*((char *)(base_patch_address + offset)) = 0x5d;
            // offset += 1;
        }

#ifdef TRACER_COLLECT_VALUE
#ifdef TRACER_LOG_ACCESSES
        CollectionInfo info;
        if (!is_write(&instruction)) {
            offset += patch_nop((char *)base_patch_address + offset);
            offset += patch_collect_instructions(
                (unsigned long)base_patch_address + offset,
                (char *)location_of_instruction, &instruction, valueReg, used,
                0, &info);
        }
#else
        if (!is_write(&instruction)) {

            if ((is_following == 0 ||
                 following_must_be_traced == TRACER_DO_TRACE) &&
                instruction.info.mnemonic != ZYDIS_MNEMONIC_NOP) {
                offset += patch_nop((char *)base_patch_address + offset);
                offset += patch_collect_instructions(
                    (unsigned long)base_patch_address + offset,
                    (char *)location_of_instruction, &instruction, valueReg,
                    used, 0);
            }
        }
#endif
#endif
        offset += patch_nop((char *)base_patch_address + offset);
        offset += patch_store_updated_regs(
            (char *)(base_patch_address + offset), used,
            get_modified_registers(&instruction), 0, (long *)base_patch_address,
            &context_info);
    }
#ifdef TRACER_USERSPACE
    if (!is_write(&instruction)) {
        offset += patch_xsave((char *)base_patch_address + offset);
    }
#endif
#ifndef TRACER_USERSPACE
    // offset += patch_adjust_rsp_up((char *)base_patch_address + offset);
#endif

    offset += patch_fun_end((char *)base_patch_address + offset);
#ifdef TRACER_LOG_DEBUG_SIGNAL_HANDLER
    TRACER_PRINT_DEBUG_SIGNAL_HANDLER("Patched the intrustruction");
#endif

#ifdef TRACER_LOG_DEBUG_INSTRUCTION
    TRACER_PRINT_DEBUG_INSTRUCTION("The instruction is the following:");
    TRACER_PRINT_DEBUG_INSTRUCTION("Operands:");
    for (int i = 0; i < instruction.info.operand_count; i++) {
        TRACER_PRINT_DEBUG_INSTRUCTION("Operand %i: Type: %i", i,
                                       instruction.operands[i].type);
        if (instruction.operands[i].type == ZYDIS_OPERAND_TYPE_MEMORY) {
            TRACER_PRINT_DEBUG_INSTRUCTION(
                "Operand %i is of type memory and uses register %s for the "
                "address",
                i, ZydisRegisterGetString(instruction.operands[i].mem.base));
        }
    }

#endif

// execute the instruction
#ifdef TRACER_LOG_DEBUG_STUB_FUNCTION
    TRACER_PRINT_DEBUG("Logging the stub function\n");
// we just use zydis to decompile the instructions starting at the
// address of the stub function
#ifdef TRACER_USERSPACE
    ZydisDisassembledInstruction instr;
#else
    ZydisDisassembledInstruction *instr = disassembled_instruction;
#endif
    ZyanUSize local_offset = 0;
    long local_runtime_address = base_patch_address;
    while (ZYAN_SUCCESS(ZydisDisassembleIntel(
        /* machine_mode:    */ ZYDIS_MACHINE_MODE_LONG_64,
        /* runtime_address: */ base_patch_address + local_offset +
            OFFSET_FROM_BUFFER_START,
        /* buffer:          */ (void *)base_patch_address + local_offset +
            OFFSET_FROM_BUFFER_START,
        /* length:          */ offset - local_offset,
#ifdef TRACER_USERSPACE
        /* instruction:     */ &instr))) {
#else
        /* instruction:	    */ instr))) {
#endif
        char bytes[15];
#ifdef TRACER_USERSPACE
        for (int i = 0; i < instr.info.length; i++) {
#else
        for (int i = 0; i < instr->info.length; i++) {
#endif
            bytes[i] = *((char *)(base_patch_address + local_offset +
                                  OFFSET_FROM_BUFFER_START + i));
        }
// memcpy(&bytes,
//       (void *)base_patch_address + local_offset +
//	       OFFSET_FROM_BUFFER_START,
//       instr.info.length);
#ifdef TRACER_USERSPACE
        TRACER_PRINT_DEBUG("%016lx"
                           "  %s, Bytes: ",
                           local_runtime_address + OFFSET_FROM_BUFFER_START,
                           instr.text);
        local_offset += instr.info.length;
        local_runtime_address += instr.info.length;
        for (int i = 0; i < instr.info.length; i++) {
            TRACER_PRINT_DEBUG("%02hhx ", bytes[i]);
        }
        TRACER_PRINT_DEBUG("\n");
#else
        TRACER_PRINT_DEBUG("%016lx"
                           "  %s, Bytes: ",
                           local_runtime_address + OFFSET_FROM_BUFFER_START,
                           instr->text);
        local_offset += instr->info.length;
        local_runtime_address += instr->info.length;
        for (int i = 0; i < instr->info.length; i++) {
            //      TRACER_PRINT_DEBUG("%02hhx ", bytes[i]);
        }
        // TRACER_PRINT_DEBUG("\n");
        kfree(instr);
#endif
    }

#endif
#ifdef TRACER_LOG_PROTECTED_BUFFER
    printf("Buffer before:\n");
    log_buffer();
#endif

    TRACER_PRINT_DEBUG("is after logging stub function");
    long value_address = 0;

    if (is_following == 0 || following_must_be_traced == TRACER_DO_TRACE) {
        value_address = collect_pre(tracer_regs, &instruction, 0,
                                    tracer_regs[TRACER_REG_RIP_DO_NOT_USE]);
        TRACER_PRINT_DEBUG("value address is %p", (void *)value_address);
    }

#ifdef TRACER_LOG_DEBUG_SIGNAL_HANDLER
    TRACER_PRINT_DEBUG_SIGNAL_HANDLER("Executing stub function");
#endif

    TRACER_PRINT_DEBUG("Executing stub function, arguments: %p, %p, %p",
                       tracer_regs, (void *)value_address,
                       (void *)uc->uc_mcontext.fpregs);
    TRACER_PRINT_DEBUG("offset before execution of the stub function: %i",
                       offset);
    // we move the  arguments into the registers and then 'call' the buffer
    long start_address = base_patch_address + OFFSET_FROM_BUFFER_START;

    // i believe that this does not work as we have to respect the red zone
    // and technically globber all registers, calling the function first
    // will take care of us not ruining the redzone i hope
    TRACER_PRINT_DEBUG("tracer_resg are at %lx\n", (long)tracer_regs);
    TRACER_PRINT_DEBUG("rp before:%llx\n",
                       tracer_regs[TRACER_REG_RIP_DO_NOT_USE]);
    TRACER_PRINT_DEBUG("rp calc : %llx\n",
                       tracer_regs[TRACER_REG_RIP_DO_NOT_USE] +
                           instruction.info.length);
    stub_caller(tracer_regs, value_address, uc, start_address);
    TRACER_PRINT_DEBUG("rp after: %llx\n",
                       tracer_regs[TRACER_REG_RIP_DO_NOT_USE]);
    // we globber all calle saved registers so that they are preserved
    // accross the function call

    // stub_function(tracer_regs, value_address,
    // (long)uc->uc_mcontext.fpregs); stub_function(tracer_regs,
    // value_address, (long)uc->uc_mcontext.fpregs);

    TRACER_PRINT_DEBUG_SIGNAL_HANDLER("Executed stub function");
#ifdef TRACER_USE_POST_HANDLER
    if (is_following == 0 || following_must_be_traced == TRACER_DO_TRACE)
        collect_post(tracer_regs, 0);
#endif
    TRACER_PRINT_DEBUG(
        "Clearing the bytes, base_patch_address is %px, length: %i",
        (void *)base_patch_address, offset);

    patch_clear_bytes((char *)base_patch_address, offset);
#ifdef TRACER_LOG_PROTECTED_BUFFER
    printf("Buffer after:\n");
    log_buffer();
#endif

// collect_post(uc, &instruction, trace_address);
//  skip to next instruction
#ifdef TRACER_LOG_ERROR
    TRACER_PRINT_DEBUG("address of instruction before get_jump_type: %p",
                       &instruction);
    if (get_jump_type(&instruction) == TRACER_JUMP_UNCOND) {
        TRACER_PRINT_ERROR(
            "Unconditional jumps are not handled in the signal handler");
    }
#endif
    TRACER_PRINT_DEBUG("is after get_jump_type");
    TRACER_PRINT_DEBUG_SIGNAL_HANDLER(
        "RIP before: %p, length: %i",
        (void *)tracer_regs[TRACER_REG_RIP_DO_NOT_USE],
        instruction.info.length);

    if (get_jump_type(&instruction) == TRACER_JUMP_NONE) {
        tracer_regs[TRACER_REG_RIP_DO_NOT_USE] += instruction.info.length;
    }
    TRACER_PRINT_DEBUG_SIGNAL_HANDLER(
        "Returning to traced program at rip %px",
        (void *)tracer_regs[TRACER_REG_RIP_DO_NOT_USE]);
    enable_pkey(used_key);
#ifdef TRACER_PREVENT_LIBC
    use_glibc[thread_index] = 1;
#endif
#ifdef TRACER_MEASURE_KERNEL_TIME
    long ticks = rdtsc();
    disable_pkey(used_key);
    measurements->signal_kernel_post = ticks;
    measurements->total_signal_handler =
        measurements->signal_kernel_post - measurements->signal_kernel_pre;
    enable_pkey(used_key);
#endif
#ifdef TRACER_MEASURE_SIGNAL_HANDLER
    long end_ticks = rdtsc();
    if (from_invalid) {
        measurements->invalid_signal_handler[thread_index] +=
            end_ticks - start_ticks;
    } else {
        measurements->fault_signal_handler[thread_index] +=
            end_ticks - start_ticks;
    }
#endif
}
