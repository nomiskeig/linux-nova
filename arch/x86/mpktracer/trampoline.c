#define _GNU_SOURCE
#include "trampoline.h"
#include "allocator.h"
#include "collector.h"
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
#ifdef TRACER_USERSPACE
#ifdef TRACER_ENABLE_MEASUREMENTS
extern Measurements *measurements;
#include "rdtsc.h"
#endif
#include <Zydis/Disassembler.h>
#include <immintrin.h>
#include <inttypes.h>
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>
#else
#include <linux/slab.h>
#endif
extern Allocator *allocator;
extern long trampoline_stack_base;
#ifdef TRACER_COLLECT_TRAMPOLINE_STATISTICS
extern int trampoline_fd;
#endif
#ifdef TRACER_LOG_TRAMPOLINE_LAYOUTS_TO_FILE
extern int trampoline_layouts_fd;
#endif
#ifdef TRACER_USERSPACE
static int regs_offset_from_rsp = -152;
#else
static int regs_offset_from_rsp = -168;
#endif
extern DisplacedInstructions *displaced_instructions;
extern int ignore_mmaps;
extern int id_offset;
extern ThreadMappings *thread_mappings;
extern ZydisDisassembledInstruction *temp_instructions;
#ifdef TRACER_USE_DECREASING
unsigned char invalid_opcodes[NUM_INVALID_OPCODES] = {
    0xEA, 0xD6, 0xD5, 0xD4, 0xC5, 0xC4, 0x9A, 0x82, 0x62, 0x61, 0x60, 0x3F,
    0x37, 0x2F, 0x27, 0x1F, 0x1E, 0x17, 0x16, 0x0E, 0x07, 0x06

};
#else
unsigned char invalid_opcodes[NUM_INVALID_OPCODES] = {
    0xEA, 0xD6, 0x06, 0x07, 0x0E, 0x16, 0x17, 0x1E, 0x1F, 0x27, 0x2F, 0x37,
    0x3F, 0x60, 0x61, 0x62, 0x82, 0x9A, 0xC4, 0xC5, 0xD4, 0xD5

};
#endif
#ifdef TRACER_LOG_TRAMPOLINE_LAYOUTS_TO_FILE
int t_lock = 0;
int new = 0;
static void own_lock_lock(void) {
    int val = 0;
repeat:

    if (!__atomic_compare_exchange_4(&t_lock, &val, 1, 0, __ATOMIC_SEQ_CST,
                                     __ATOMIC_SEQ_CST)) {
		val = 0;
        goto repeat;
    }
}
static void own_lock_unlock(void) { t_lock = 0; }
#endif
/*
static int is_constrained(ProbeSite *probe_site) {
    return probe_site->instructions[0]->info.length >= 3 ? 0 : 1;
};
*/

int patch_reg_mov(char *address_to_patch, TracerRegister from,
                  TracerRegister to) {
    int from_index = tracer_reg_to_index(from);
    int to_index = tracer_reg_to_index(to);
    char prefix = 0x48;
    if (from_index >= 8) {
        prefix |= 0x04;
    }
    if (to_index >= 8) {
        prefix |= 0x01;
    }
    address_to_patch[0] = prefix;
    address_to_patch[1] = 0x89;
    address_to_patch[2] = 0xC0 + to_index % 8 + (from_index % 8) * 8;

    return 3;
}
static int patch_trampoline_simple(ProbeSite *probe_site, int pkey,
                                   int is_tracing_following) {
#ifdef TRACER_COLLECT_TRAMP_INST_INFO
    long start_ticks = rdtsc();
#endif
    TRACER_PRINT_DEBUG_TRAMPOLINES(
        "Tring to patch a simple trampoline for address %lx",
        (long)probe_site->rip_of_instruction);
#ifdef TRACER_COLLECT_TRAMPOLINE_STATISTICS
    int amount_attemps = 0;
#endif
    // start the loops at i = 2 because the values for i = 0 and i = 1 are
    // reserved for the allocation algorithm
    for (int i = 2; i < NUM_INVALID_OPCODES; i++) {
        for (int j = 2; j < NUM_INVALID_OPCODES; j++) {
#ifdef TRACER_COLLECT_TRAMPOLINE_STATISTICS
            amount_attemps += 1;
#endif

            // TODO: for the kernel, we can probably jump also jump backwards as
            // we could land in the unused hole
            if (invalid_opcodes[j] >= 0x80) {
                // skip the ones that result in negative addresse=
                continue;
            }
            // we add a offset of 5 since the jump instruction has 5 bytes and
            // the offset is counted from the start of the following instruction
            // (see intel manual/https://www.felixcloutier.com/x86/jmp)
            long new_address = (long)probe_site->rip_of_instruction + 5 +
                               (invalid_opcodes[i] << 16) +
                               (invalid_opcodes[j] << 24);
            TRACER_PRINT_DEBUG_TRAMPOLINES(
                "New address for the trampoline page is %lx", new_address);
            PageAddress page_address = new_address >> 12;
#ifdef TRACER_PRINT_ERROR
            if (allocator == NULL) {
                TRACER_PRINT_ERROR("allocator is 0 for some reason");
            }
#endif
            TRACER_PRINT_DEBUG_TRAMPOLINES("after 1\n");
            PageInfo *page_info = get_page_info(page_address, allocator);
            if (page_info == NULL) {

                TRACER_PRINT_DEBUG_TRAMPOLINES("after 1.5\n");
                page_info = allocate(page_address, allocator);
                TRACER_PRINT_DEBUG_TRAMPOLINES("after 1.6\n");
                if (page_info == NULL) {
                    continue;
                }
#ifdef TRACER_COUNT_AMOUNT_TAKEN
                measurements->used_tramp_pages[thread_index] += 1;
#endif
            }
            TRACER_PRINT_DEBUG_TRAMPOLINES("after 2\n");

            UsedOpcodes opcodes;
            TrampolineInfo *tramp_info = find_trampoline_location_on_page(
                page_info, new_address, &opcodes);
            TRACER_PRINT_DEBUG_TRAMPOLINES("after 3\n");
            if (tramp_info == NULL) {
                continue;
            };
            long tramp_address = page_info->address + tramp_info->start_offset;
            TRACER_PRINT_DEBUG_TRAMPOLINES(
                "Location for the trampoline is 0x%lx", tramp_address);
#ifdef TRACER_USERSPACE
            void *location_to_pick_page =
                (void *)((long)probe_site->rip_of_instruction -
                         ((long)probe_site->rip_of_instruction % 4096));
            mprotect(location_to_pick_page, 4096 * 2,
                     PROT_READ | PROT_WRITE | PROT_EXEC);
#endif
            int length = write_trampoline(probe_site, tramp_address, pkey,
                                          is_tracing_following);
#ifdef TRACER_LOG_TRAMPOLINE_LAYOUTS_TO_FILE
            own_lock_lock();
            safe_print_to_file(trampoline_layouts_fd,
                               "The trampoline for instruction %lx (original "
                               "address was %lx) was written by thread with "
                               "id %i and is_tracing_following %i has the "
                               "following instructions:\n",
                               (long)probe_site->rip_of_instruction,
                               probe_site->original_address, thread_index,
                               is_tracing_following);
            int offset_log = 0;
            int is_following;

            tracer_follow_type type;
            long displaced = find_displaced_location(
                (long)probe_site->rip_of_instruction, displaced_instructions,
                &is_following, &type);
            for (int i = 0; i < probe_site->num_instructions; i++) {
                safe_print_to_file(
                    trampoline_layouts_fd,
                    "Instrution %i: Length: %i, text: %s, Bytes:  ", i,
                    probe_site->instructions[i]->info.length,
                    probe_site->instructions[i]->text);
                for (int j = 0; j < probe_site->instructions[i]->info.length;
                     j++) {
                    safe_print_to_file(trampoline_layouts_fd, "%02hhx ",
                                       *((char *)(displaced + offset_log + j)));
                }

                safe_print_to_file(trampoline_layouts_fd, "\n");
                offset_log += probe_site->instructions[i]->info.length;
            }
            ZydisDisassembledInstruction instr;
            ZyanUSize offset = 0;
            while (ZYAN_SUCCESS(ZydisDisassembleIntel(
                ZYDIS_MACHINE_MODE_LONG_64, tramp_address + offset,

                (void *)(tramp_address + offset), length - offset, &instr))) {
                char bytes[instr.info.length];
                // memcpy(&bytes, (void *)(tramp_address + offset),
                //       instr.info.length);
                for (int i = 0; i < instr.info.length; i++) {
                    bytes[i] = *(char *)(tramp_address + offset + i);
                }
                safe_print_to_file(trampoline_layouts_fd,
                                   "%016" PRIX64 "  %s, Bytes: ",
                                   tramp_address + offset, instr.text);
                offset += instr.info.length;
                for (int i = 0; i < instr.info.length; i++) {
                    // safe_print_to_file(trampoline_layouts_fd, "%02hhx ",
                    //                   bytes[i]);
                }
                safe_print_to_file(trampoline_layouts_fd, "\n");
            }
            own_lock_unlock();
            /*
#ifdef TRACER_LOG_ERROR
if (offset < length) {
    TRACER_PRINT_ERROR("Something is wrong with the trampoline, "
                       "could not decode all instructions on it");
}
#endif
*/
#endif

#ifdef TRACER_LOG_DEBUG_TRAMPOLINES
            TRACER_PRINT_DEBUG_TRAMPOLINES("The trampoline looks like this:");
#ifndef TRACER_LOG_TRAMPOLINE_LAYOUTS_TO_FILE
            ZydisDisassembledInstruction instr;
            ZyanUSize offset = 0;
#endif

            while (ZYAN_SUCCESS(ZydisDisassembleIntel(
                /* machine_mode:    */ ZYDIS_MACHINE_MODE_LONG_64,
                /* runtime_address: */ tramp_address + offset,
                /* buffer:          */
                (void *)(tramp_address + offset),
                /* length:          */ length - offset,
                /* instruction:     */ &instr))) {
                char bytes[15];
                for (int i = 0; i < instr.info.length; i++) {
                    bytes[i] = *(char *)(tramp_address + offset);
                }
                // memcpy(&bytes, (void *)(tramp_address + offset),
                //       instr.info.length);
                TRACER_PRINT_DEBUG("%016lx"
                                   "  %s, Bytes: ",
                                   tramp_address + offset, instr.text);
                offset += instr.info.length;
                for (int i = 0; i < instr.info.length; i++) {
#ifdef TRACER_USERSPACE
                    TRACER_PRINT_DEBUG("%02hhx ", bytes[i]);
#endif
                }
#ifdef TRACER_USERSPACE
                TRACER_PRINT_DEBUG("\n");
#endif
            }
#ifdef TRACER_LOG_ERROR
            if (offset < length) {
                TRACER_PRINT_ERROR("Something is wrong with the trampoline, "
                                   "could not decode all instructions on it");
            }
#endif
#endif

            if (length > MAX_TRAMPOLINE_SIZE) {
                TRACER_PRINT_ERROR("Constructed a trampoline that is longer "
                                   "than the max size");
            }
            TRACER_PRINT_DEBUG_TRAMPOLINES("Acutal trampoline size: %i",
                                           length);
            // patch the code so it jumps to the trampoline
            // TODO: we have to restore the original protections
            probe_site->rip_of_instruction[4] = invalid_opcodes[j];
            probe_site->rip_of_instruction[3] = invalid_opcodes[i];
            probe_site->rip_of_instruction[2] = opcodes.d;
            probe_site->rip_of_instruction[1] = opcodes.e;
            probe_site->rip_of_instruction[0] = 0xE9;
            TRACER_PRINT_DEBUG_TRAMPOLINES(
                "Patched the address for the jump with 0x%hhx 0x%hhx 0x%hhx "
                "0x%hhx 0x%hhx",
                probe_site->rip_of_instruction[0],
                probe_site->rip_of_instruction[1],
                probe_site->rip_of_instruction[2],
                probe_site->rip_of_instruction[3],
                probe_site->rip_of_instruction[4]);

#ifdef TRACER_PRINT_MEM_TRAMPOLINES
            char text[2];
            text[0] = '1';
            text[1] = '\n';
            int res = write(measurements->mem_timings_file, text, 2);
            if (res == 1) {
                TRACER_PRINT_ERROR("could not write file");
            }

#endif
#ifdef TRACER_COLLECT_TRAMP_INST_INFO
            long end_ticks = rdtsc();
            int index = measurements->tramp_addresses_indices[thread_index];
            measurements->tramp_addresses[thread_index][index] =
                (long)tramp_address;
            measurements->tramp_addresses_indices[thread_index] = index + 1;
            measurements->tramp_install_addresses[thread_index][index] =
                (long)tramp_address - (long)probe_site->rip_of_instruction;
#endif
#ifdef TRACER_COLLECT_TRAMP_INSTR
            int index = measurements->tramp_addresses_indices[thread_index];
            measurements->tramp_instr_amount[thread_index][index] =
                probe_site->num_instructions;
            measurements->tramp_first_length[thread_index][index] =
                probe_site->instructions[0]->info.length;

            measurements->tramp_addresses_indices[thread_index] = index + 1;
#endif

            return 1;
        }
    };
    return 0;
};

void set_following_must_be_traced(long original_address,
                                  tracer_follow_type type) {
    for (int i = 0; i < MAX_DISPLACED_INSTRUCTIONS; i++) {
        // we check that the address we search is in the range of the first
        // instruction so that we also find displacements for instructions that
        // follow an instruction that we want to trace
        if (displaced_instructions->instructions[i].orig_address <=
                original_address &&
            original_address <
                displaced_instructions->instructions[i].orig_address + 5) {
            displaced_instructions->instructions[i].type = type;
        }
    }
}
DisplacedInstructionLocation *
get_displaced_location_info(long original_address,
                            DisplacedInstructions *displaced_instructions) {
    for (int i = 0; i < MAX_DISPLACED_INSTRUCTIONS; i++) {
        // we check that the address we search is in the range of the first
        // instruction so that we also find displacements for instructions that
        // follow an instruction that we want to trace
        if (displaced_instructions->instructions[i].orig_address <=
                original_address &&
            original_address <
                displaced_instructions->instructions[i].orig_address + 5) {
            return &displaced_instructions->instructions[i];
        }
    }
    return NULL;
}
long find_displaced_location(long orignal_address,
                             DisplacedInstructions *displaced_instructions,
                             int *is_following,
                             tracer_follow_type *follow_type) {
    TRACER_PRINT_DEBUG_TRAMPOLINES(
        "Searching for displaced instruction with original address %lx",
        orignal_address);
    for (int i = 0; i < MAX_DISPLACED_INSTRUCTIONS; i++) {
        // we check that the address we search is in the range of the first
        // instruction so that we also find displacements for instructions that
        // follow an instruction that we want to trace
        if (displaced_instructions->instructions[i].orig_address <=
                orignal_address &&
            orignal_address <
                displaced_instructions->instructions[i].orig_address + 5) {
            *follow_type = displaced_instructions->instructions[i].type;
            if (displaced_instructions->instructions[i].orig_address !=
                orignal_address) {
                TRACER_PRINT_DEBUG_TRAMPOLINES(
                    "Found displaced instruction that we were not tracing");
                *is_following = 0x1;
                return (long)&displaced_instructions->instructions[i]
                           .instruction +
                       orignal_address -
                       displaced_instructions->instructions[i].orig_address;
            }
            return (long)&displaced_instructions->instructions[i].instruction;
        }
    }
    /**TRACER_PRINT_ERROR("Tried to search for a displaced instruction and did "
           "not find it, that should not happen");*/
    return 0;
}
static void patch_trampoline(ProbeSite *probe_site, int pkey, char first_byte,
                             int is_tracing_following, int index) {
    TRACER_PRINT_DEBUG_TRAMPOLINES("Patching trampoline for address %lx",
                                   (long)probe_site->rip_of_instruction);
// store the displaced instruction
#ifdef TRACER_LOG_ERROR
    if (displaced_instructions->next_index >= MAX_DISPLACED_INSTRUCTIONS) {
        TRACER_PRINT_ERROR("Trying to store too many displaced instructions");
    }
#endif
    // TODO:  i think this breaks if we displace mor ethan one instruction and
    // (and i think this is what we do not to till now) actually need the
    // following instruction
    TRACER_PRINT_DEBUG_TRAMPOLINES("Got index %i\n", index);

    displaced_instructions->instructions[index].instruction[0] = first_byte;
    for (int i = 1; i < MAX_CHARS_DISPLACED_LOCATION; i++) {
        displaced_instructions->instructions[index].instruction[i] =
            probe_site->rip_of_instruction[i];
    }
    probe_site->address_of_instruction =
        &displaced_instructions->instructions[index].instruction[0];
    TRACER_PRINT_DEBUG_TRAMPOLINES(
        "Storing displaced instruction for original address %lx",
        (long)probe_site->rip_of_instruction);
    displaced_instructions->instructions[index].orig_address =
        (long)probe_site->rip_of_instruction;
    if (probe_site->num_instructions > 1) {

        displaced_instructions->instructions[index].type = TRACER_UNDECIDED;
    } else {
        // we set the type of the first instruction to do trace, beause we know
        // that we trace it (otherwise we would not be on the trampoline, if we
        // dont do this, it could happen that another thread waits for this to
        // turn to tracer do tace, but we never set it so it just spins forever
        // (since for the first instruction we dont isntall the probe))
        displaced_instructions->instructions[index].type = TRACER_DO_TRACE;
    }
    // we need to set this here as before the displaced thingy is not yet
    // created this case is basically the case where a invlaid instruction on
    // the trampoline is hit, but we install that trampoline only because we
    // trace a subsequent so we knwo that we want to tace
    if (is_tracing_following) {
        displaced_instructions->instructions[index].type = TRACER_DO_TRACE;
    }
    // displaced_instructions->next_index += 1;
    //  update the location of the trampoline so that a thread that tries to
    //  use the normal signal handler knows that it can do so b/c the
    //  displaced instruction was already written

    // asm volatile("mfence" ::: "memory"); // Prevent CPU reordering
    *probe_site->rip_of_instruction = invalid_opcodes[1];
#ifdef TRACER_COLLECT_TRAMPOLINE_STATISTICS
    safe_print_to_file(trampoline_fd, "Before %ld\n", _rdtsc());
#endif
    int res = patch_trampoline_simple(probe_site, pkey, is_tracing_following);
#ifdef TRACER_COUNT_AMOUNT_TRAMPOLINES
    if (res) {
        measurements->amount_success_trampolines[thread_index] += 1;
        if (is_write(probe_site->instructions[0])) {
            measurements->amount_write_tramps[thread_index] += 1;
        } else {
            measurements->amount_read_tramps[thread_index] += 1;
        }

    } else {
        measurements->amount_fail_trampolines[thread_index] += 1;
    }
#endif
    TRACER_PRINT_DEBUG_TRAMPOLINES("Ran patch_trampoline_simple");
    if (!res) {
#ifdef TRACER_COLLECT_TRAMPOLINE_STATISTICS
        safe_print_to_file(trampoline_fd, "After %ld\n", _rdtsc());
        safe_print_to_file(trampoline_fd, "no allocation, %p, %i\n",
                           (long *)probe_site->rip_of_instruction);
#endif
        // we could not find a trampoline, so we set the first byte of the
        // instruction to an invalid instruciton NOTE: normally we would use the
        // 0x33 (int3) instruction, but i think that interferes with our ptrace
        // thing
        TRACER_PRINT_DEBUG_TRAMPOLINES("Could not find a trampoline, so "
                                       "patching with in invalid instruction");
        *probe_site->rip_of_instruction = invalid_opcodes[1];
        return;
    }
#ifdef TRACER_COLLECT_TRAMPOLINE_STATISTICS
    safe_print_to_file(trampoline_fd, "After %ld\n", _rdtsc());
    safe_print_to_file(trampoline_fd, "allocation, %p\n",
                       (long *)probe_site->rip_of_instruction);
#endif
    TRACER_PRINT_DEBUG_TRAMPOLINES("Returning from patching the  trampoline");
}

void install_trampoline(long address, int pkey, char first_byte,
                        int is_tracing_following, long original_address) {
    // disable the pkey for the cases where the instruction itself is in a
    // protected buffer (like the relative test)
    disable_pkey(pkey);
    TRACER_PRINT_DEBUG_TRAMPOLINES(
        "Installing trampoline for address %lx, original address is %lx",
        address, original_address);
    ProbeSite probe_site;
    probe_site.address_of_instruction = (char *)(address);
    probe_site.original_address = original_address;
    int total_length = 0;
    int current_index = 0;
    int index;
    asm("mov $1, %%rax\n\t"
        "lock xadd %%rax, %1\n\t"
        "mov %%rax, %0\n\t"
        : "=m"(index)
        : "m"(displaced_instructions->next_index)
        : "rax", "memory");
    char buffer[40];
    buffer[0] = first_byte;
    probe_site.rip_of_instruction = (char *)address;
	probe_site.index = index;
    // memcpy(&buffer[1], (void *)address + 1, 39);
    for (int i = 0; i < 39; i++) {
        buffer[1 + i] = *(char *)(address + 1 + i);
    }

#if !(defined(TRACER_CACHE_INSTRUCTIONS) || defined(TRACER_ENCODE_INDEX_ON_TRAMPOLINE))
#ifdef TRACER_USERSPACE
#ifndef TRACER_CACHE_INSTRUCTIONS
    ZydisDisassembledInstruction instructions[5];
#endif

#else
    // we have to kmalloc the instrutions since we cannot but them onto the
    // stack because in the kernel, the frame size is limited to 2^11 bytes
    ZydisDisassembledInstruction *instructions = temp_instructions;
#endif
    while (ZYAN_SUCCESS(ZydisDisassembleIntel(
        /* machine_mode:    */ ZYDIS_MACHINE_MODE_LONG_64,
        /* runtime_address: */ address + total_length,
        /* buffer:          */ (void *)(buffer + total_length),
        /* length:          */ 40,
        /* instruction:     */ &instructions[current_index]))) {
        total_length += instructions[current_index].info.length;
        current_index += 1;
        if (total_length >= 5) {
            probe_site.num_instructions = current_index;
            for (int i = 0; i < current_index; i++) {
                probe_site.instructions[i] = &instructions[i];
            }
            break;
        }
    }
#else
    while (ZYAN_SUCCESS(ZydisDisassembleIntel(
        /* machine_mode:    */ ZYDIS_MACHINE_MODE_LONG_64,
        /* runtime_address: */ address + total_length,
        /* buffer:          */ (void *)(buffer + total_length),
        /* length:          */ 40,
        /* instruction:     */
        &displaced_instructions->instructions[index]
             .disassembled_instructions[current_index]))) {
        displaced_instructions->instructions[index]
            .orig_addresses[current_index] = (long)address + total_length;
        total_length += displaced_instructions->instructions[index]
                            .disassembled_instructions[current_index]
                            .info.length;
        current_index += 1;
        if (total_length >= 5) {
            probe_site.num_instructions = current_index;
            for (int i = 0; i < current_index; i++) {
                probe_site.instructions[i] =
                    &displaced_instructions->instructions[index]
                         .disassembled_instructions[i];
            }
            break;
        }
    }
#endif
    // TODO: this could be problematic in the case o fmultithreading
    // block sigusr1 while we patch the trampoline and patch that memory, we
    // dont want to protect the trampoline itself
#ifdef TRACER_USERSPACE
    sigset_t set;
    sigemptyset(&set);
    // we tempoaraly replace our
    sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
#endif
    patch_trampoline(&probe_site, pkey, first_byte, is_tracing_following,
                     index);
#ifdef TRACER_USERSPACE
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);
#endif
    enable_pkey(pkey);
}

static int patch_store_return_address(ProbeSite *probe_site, char *address,
                                      long *store_at_end) {
    int c = 0;
    // calculate the return address;
    long return_address = (long)probe_site->rip_of_instruction;
    for (int i = 0; i < probe_site->num_instructions; i++) {
        return_address += probe_site->instructions[i]->info.length;
    }
    int patched_return_address = 0;
    long address_of_instruction = (long)probe_site->address_of_instruction;
    int counter = 0;
    for (int i = 0; i < probe_site->num_instructions; i++) {
        counter += probe_site->instructions[i]->info.length;
        if (get_jump_type(probe_site->instructions[i]) == TRACER_JUMP_UNCOND) {
            TRACER_PRINT_DEBUG_TRAMPOLINES("Found unconditional jump");

            // in this case, we just calculate the new return address
            //  there are 3 cases: 1) relative jump, 2) address in memory, 3)
            //  address in register
            ZydisDisassembledInstruction *instruction =
                probe_site->instructions[i];
            if (instruction->operands[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
                TRACER_PRINT_DEBUG_TRAMPOLINES(
                    "Patching unconditiaonal jump with immediate");
                long jump_target = (long)probe_site->rip_of_instruction +
                                   counter +
                                   instruction->operands[0].imm.value.s;
                return_address = jump_target;
                patched_return_address = 1;
                address[0] = 0x48;
                address[1] = 0xB8;
                *((long *)&address[2]) = return_address;
                c += 10;
                break;
            }
            if (instruction->operands[0].type == ZYDIS_OPERAND_TYPE_MEMORY) {
                TRACER_PRINT_DEBUG_TRAMPOLINES(
                    "Patching unconditiaonal jump with memory");
                // in this case, we just copy over the the instruction to the
                // end of the trampolin
                return 0;
            }
            if (instruction->operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER) {
                TRACER_PRINT_DEBUG_TRAMPOLINES(
                    "Patching unconditiaonal jump with register");
                int reg = tracer_reg_to_specific_reg_index(
                    get_offset_of_reg(instruction->operands[0].reg.value));
                // mov rax, [rsp + offset]
                address[0] = 0x48;
                address[1] = 0x8b;
                address[2] = 0x84;
                address[3] = 0x24;
                int offset = 8 * reg;
                *((int *)&address[4]) = offset;
                c += 8;
                patched_return_address = 1;
                break;
            }

            TRACER_PRINT_ERROR("Found an unconditional jump that follows "
                               "a traced "
                               "instruction, this is not yet implemented");
            break;
        }
        address_of_instruction += probe_site->instructions[i]->info.length;
    }
    address_of_instruction = (long)probe_site->address_of_instruction;
    if (!patched_return_address) {
        TRACER_PRINT_DEBUG_TRAMPOLINES("Patching normal return address");
        address[0] = 0x48;
        address[1] = 0xB8;
        *((long *)&address[2]) = return_address;
        c += 10;
    }
    // store the return address in (rsp);
    // we first load the address into rax as an immidiate
    //
    // then we load the location for the return address into rbx
    address[c] = 0x48;
    address[c + 1] = 0xbb;
    *((long *)&address[c + 2]) = (long)&store_at_end[RETURN_ADDRESS_INDEX];
    // now, we do mov [rbx], rax
    // then, we can jump back with an rip-relative instruction (do not do this
    // at home kids...)
    address[c + 10] = 0x48;
    address[c + 11] = 0x89;
    address[c + 12] = 0x03;
    counter = 0;
    for (int i = 0; i < probe_site->num_instructions; i++) {
        counter += probe_site->instructions[i]->info.length;
        if (get_jump_type(probe_site->instructions[i]) == TRACER_JUMP_COND) {
            ZydisDisassembledInstruction *instruction =
                probe_site->instructions[i];
            // in this case, we store the return address that the code would
            // jump to if the jump is taken
            int offset = instruction->info.raw.imm[0].value.s;
            // i believe we have to to the following cases to make sure that the
            // values are sign extended correctly (so that a negative number
            // does not become positive if we just put it into the bigger
            // address location)
            if (instruction->info.raw.imm[0].size == 8) {
                offset = (char)instruction->info.raw.imm[0].value.s;
            }
            if (instruction->info.raw.imm[0].size == 16) {
                offset = (short)instruction->info.raw.imm[0].value.s;
            }
            long alt_return_address =
                (long)probe_site->rip_of_instruction + counter + offset;
            // store the alternate jump target at the end of the trmapoline
            // write the return address into rax
            address[c + 13] = 0x48;
            address[c + 14] = 0xB8;
            *((long *)&address[c + 15]) = alt_return_address;
            // then we load the location for the return address into rbx
            address[c + 23] = 0x48;
            address[c + 24] = 0xbb;
            *((long *)&address[c + 25]) =
                (long)&store_at_end[JUMP_TAKEN_RETURN_ADDRESS_INDEX];
            // now, we do mov [rbx], rax
            // then, we can jump back with an rip-relative instruction (do not
            // do this at home kids...)
            address[c + 33] = 0x48;
            address[c + 34] = 0x89;
            address[c + 35] = 0x03;
            return c + 36;
        }
        address_of_instruction += probe_site->instructions[i]->info.length;
    }

    return c + 13;
}

static int patch_save_single_register(char *address, int offset,
                                      TracerRegister reg) {
    address[0] = 0x48;
    int index = tracer_reg_to_index(reg);
    if (index >= 8) {
        address[0] = 0x4c;
    }
    address[1] = 0x89;
    address[2] = 0x84 + (index % 8) * 8;
    address[3] = 0x24;
    *((int *)&address[4]) = offset;
    return 8;
}

static int patch_prepare_stub_arguments(char *address) {
    // mov rsi, rax (rax contains the return value from the pre function, which
    // is the address for the value)
    address[0] = 0x48;
    address[1] = 0x89;
    address[2] = 0xc6;
    // mov rdi, rsp // rsp contains the bottom of the stack, and since there are
    // only the registers on the stack that is what we get
    address[3] = 0x48;
    address[4] = 0x89;
    address[5] = 0xe7;

    return 6;
}

static int patch_set_rsp_to_new_stack(char *address, long *store_at_end,
                                      int is_tracing_following) {
    // PERF: this could theoretically be faster if we know that the program will
    // never use multiple threads, because then we can just use the base address
    // everytime and we d not have to load the index and calculate...
    // we assume that all stacks are allocated in sequence in one buffer.
    // this way, we can get the address by doing baseaddress + index *
    // stack_size, weere baseaddress and stack size are available when this code
    // here is running if we did seperate buffers for each thread, we would have
    // to determine the address at runtime (we would need to access the array,
    // but thats hard and slower!) first, we push rax and rbx onto the stack so
    // that we have some space to work with
    //
    // we need to save the flags register as well, as we perform some arithmetic
    // when we calculate the location of the stack. Since flags register can
    // only be accessed by pushf/popf, we move the stack pointer to [rsp -200]
    // and then to a pushf, then we continue as before When we load the register
    // into the struct, we first store the flags register, and then add 200 to
    // the rsp (which is the old position)
    //
    //
    //
    // lea rsp, [rsp- 200]
    address[0] = 0x48;
    address[1] = 0x8d;
    address[2] = 0xa4;
    address[3] = 0x24;
    address[4] = 0x38;
    address[5] = 0xff;
    address[6] = 0xff;
    address[7] = 0xff;

    // NOTE: we were originally using push/and pop operations to tmporaly store
    // rax/rdx, but we cannot assume that there is nothing below the stack
    // pointer the program wont need
    // the pthread test has a case where rpb=rsp and then rbp-4 holds a local
    // variable but that just gets overwritten by the push below so, in order to
    // not destroy the layout of the program, we use the locations at the end of
    // the trampoline to temporally store them we move rax to store_at_end mov
    // [rip + offset], rax
    // UPDATE: this does not work either, because multiple trampolines might
    // execute at once and then they would try to use the same memory locations,
    // resulting in race conditions. i thikn what really works is the usage of
    // the stack, but with we respect the red zone mov [rsp - 200], rax
    int local_offset = 8;
    // well now that we just pushed the rsp, we can just push the rax and rdx
    // registers push rax
    address[0 + local_offset] = 0x50;
    // push rdx (we use rdx since it may be overwritten by the multiplication
    //
    // below, i am not sure how exaclty that works but this way we do not have
    // to worry about it)
    // mov [rip + offset], rdx
    // mov [rsp- 208], rdx
    // push rdx
    address[1 + local_offset] = 0x52;

    // pushf
    address[2 + local_offset] = 0x9c;
#ifdef TRACER_USERSPACE
    // next, we load the index of the current thread into rax
    // mov rax, fs:0x10
    // this loads the pointer to the pthread objetct into rax (this is glibc
    // specific and not portable at all...)
    address[3 + local_offset] = 0x64;
    address[4 + local_offset] = 0x48;
    address[5 + local_offset] = 0x8b;
    address[6 + local_offset] = 0x04;
    address[7 + local_offset] = 0x25;
    address[8 + local_offset] = 0x10;
    address[9 + local_offset] = 0x00;
    address[10 + local_offset] = 0x00;
    address[11 + local_offset] = 0x00;
    // mov rax, 720(rax)
    // this loads the actual thread id into rax, the thread id is stored within
    // the pthread struct at an offset of 720 bytes (i dont think userspace is
    // supposed to use this, but it works, although it might just break on
    // different architectures/versions of glibc...)
    address[12 + local_offset] = 0x48;
    address[13 + local_offset] = 0x8b;
    address[14 + local_offset] = 0x80;
    address[15 + local_offset] = 0xd0;
    address[16 + local_offset] = 0x02;
    address[17 + local_offset] = 0x00;
    address[18 + local_offset] = 0x00;

    // now, in rax, we have the gettid() value
    // we have to substract the value we store i nthe beginning to get the
    // actual inxex sub rax, id_offset
    address[19 + local_offset] = 0x48;
    address[20 + local_offset] = 0x2d;
    *((int *)&address[21 + local_offset]) = id_offset;
    // we multiply rax by MAX_THREAD_STACK_SIZE to get the offset from the
    // thread stack base first, we load TRAMPOLINE_STACK_SIZE mov rdx,
    // TRAMPOLONE_STACK_SIZE
    local_offset -= 13;
    address[38 + local_offset] = 0x48;
    address[39 + local_offset] = 0xc7;
    address[40 + local_offset] = 0xc2;
    *((int *)&address[41 + local_offset]) = (int)TRAMPOLINE_STACK_SIZE;
    // mul rdx (this stores the result in rdx:rax, but we assume that the result
    // is aalways small enough to fit into rax (if we have an offset that is  >>
    // 2^64 we have other problems anyways))
    address[45 + local_offset] = 0x48;
    address[46 + local_offset] = 0xf7;
    address[47 + local_offset] = 0xe2;
#else
    // mov rdx, 0
    local_offset -= 38;
    address[41 + local_offset] = 0x48;
    address[42 + local_offset] = 0xc7;
    address[43 + local_offset] = 0xc0;
    address[44 + local_offset] = 0x00;
    address[45 + local_offset] = 0x00;
    address[46 + local_offset] = 0x00;
    address[47 + local_offset] = 0x00;
#endif
    // now, we load the base address into rdx and than add it to rax to get the
    // address of the stack, we add trampoline_stak_size because the stack grows
    // down thus we want to opmost address
    address[48 + local_offset] = 0x48;
    address[49 + local_offset] = 0xba;
    *((long *)&address[50 + local_offset]) =
        trampoline_stack_base + TRAMPOLINE_STACK_SIZE +
        (is_tracing_following ? TRAMPOLINE_STACK_SIZE * MAX_SUPPORTED_THREADS
                              : 0);

    // add rax, rdx (sadly there is no addi with 64 bit immediate)
    address[58 + local_offset] = 0x48;
    address[59 + local_offset] = 0x01;
    address[60 + local_offset] = 0xd0;

    // we first store flags at [rax + offset] by using rdx as an tmp
    // mov rdx, [rsp]
    // we can just pop the value into rdx
    address[61 + local_offset] = 0x5a;
    // mov [rax + offset], rdx
    address[62 + local_offset] = 0x48;
    address[63 + local_offset] = 0x89;
    address[64 + local_offset] = 0x90;
    int offset = regs_offset_from_rsp + TRACER_REG_FLAGS * 8;
    *((int *)&address[65 + local_offset]) = offset;

    // now, we do a little trick: we can rdx to already pop rax/rdx from the
    // stack, and write them to rax + offset. We also store rsp that way. Then,
    // we do not have to do that in the patch_save_registers method pop rdx
    // mov rdx, [rip + offset]
    // mov rdx, [rip - 208]
    // pop rdx
    address[69 + local_offset] = 0x5a;

    // mov rax + offset, rdx
    offset = regs_offset_from_rsp + 8 * TRACER_REG_RDX;
    address[70 + local_offset] = 0x48;
    address[71 + local_offset] = 0x89;
    address[72 + local_offset] = 0x90;
    *((int *)&address[73 + local_offset]) = offset;
    // pop rdx // this pops the old value of rax into rdx
    // mov rdx, [rip + offset]
    // mov rdx, [rsp -200]
    // pop rdx
    address[77 + local_offset] = 0x5a;
    local_offset -= 6;
    // mov rax + offset, rdx
    offset = regs_offset_from_rsp + 8 * TRACER_REG_RAX;
    address[84 + local_offset] = 0x48;
    address[85 + local_offset] = 0x89;
    address[86 + local_offset] = 0x90;
    *((int *)&address[87 + local_offset]) = offset;
    // add rsp, 200
    // byte
    local_offset += 4;
    address[87 + local_offset] = 0x48;
    address[88 + local_offset] = 0x81;
    address[89 + local_offset] = 0xc4;
    address[90 + local_offset] = 0xc8;
    address[91 + local_offset] = 0x00;
    address[92 + local_offset] = 0x00;
    address[93 + local_offset] = 0x00;
    // mov rax+ offset, rsp
    // store the old value of rsp
    offset = regs_offset_from_rsp + 8 * TRACER_REG_RSP;
    address[94 + local_offset] = 0x48;
    address[95 + local_offset] = 0x89;
    address[96 + local_offset] = 0xa0;
    *((int *)&address[97 + local_offset]) = offset;
    // finally, mov rax to rsp
    address[101 + local_offset] = 0x48;
    address[102 + local_offset] = 0x89;
    address[103 + local_offset] = 0xc4;
    return 104 + local_offset;
}

static int patch_restore_single_register(char *address, int offset,
                                         TracerRegister reg) {
    int index = tracer_reg_to_index(reg);

    if (offset <= 120) {
        address[0] = 0x48;
        if (index >= 8) {
            address[0] = 0x4c;
        }
        address[1] = 0x8B;
        address[2] = 0x44 + (index % 8) * 8;
        address[3] = 0x24;
        address[4] = (char)offset;
        return 5;
    }

    address[0] = 0x48;
    if (index >= 8) {
        address[0] = 0x4c;
    }
    address[1] = 0x8B;
    address[2] = 0x44 + (index % 8) * 8 + 0x40;
    address[3] = 0x24;
    *((int *)&address[4]) = offset;
    return 8;
}

static int patch_save_registers(char *current_address, int *offset) {
    // this has to match the struct in regs.h
    char *curr_addr = current_address;
    // PERF: we could optimize and not save all registers instead just the ones
    // we need
    // PERF /TODO: I think we could also do mov rsp down, and then add the ssp
    // back, this would save bytes
    // -> this sets the registers up like a "normal register"
    // the commented out registers are saved by the fuctnion that changes rsp to
    // the correct rsp,
    curr_addr += patch_save_single_register(
        curr_addr, regs_offset_from_rsp + TRACER_REG_R8 * 8, R8);
    curr_addr += patch_save_single_register(
        curr_addr, regs_offset_from_rsp + TRACER_REG_R9 * 8, R9);
    curr_addr += patch_save_single_register(
        curr_addr, regs_offset_from_rsp + TRACER_REG_R10 * 8, R10);
    curr_addr += patch_save_single_register(
        curr_addr, regs_offset_from_rsp + TRACER_REG_R11 * 8, R11);
    curr_addr += patch_save_single_register(
        curr_addr, regs_offset_from_rsp + TRACER_REG_R12 * 8, R12);
    curr_addr += patch_save_single_register(
        curr_addr, regs_offset_from_rsp + TRACER_REG_R13 * 8, R13);
    curr_addr += patch_save_single_register(
        curr_addr, regs_offset_from_rsp + TRACER_REG_R14 * 8, R14);
    curr_addr += patch_save_single_register(
        curr_addr, regs_offset_from_rsp + TRACER_REG_R15 * 8, R15);
    curr_addr += patch_save_single_register(
        curr_addr, regs_offset_from_rsp + TRACER_REG_RDI * 8, RDI);
    curr_addr += patch_save_single_register(
        curr_addr, regs_offset_from_rsp + TRACER_REG_RSI * 8, RSI);
    curr_addr += patch_save_single_register(
        curr_addr, regs_offset_from_rsp + TRACER_REG_RBP * 8, RBP);
    curr_addr += patch_save_single_register(
        curr_addr, regs_offset_from_rsp + TRACER_REG_RBX * 8, RBX);
    // curr_addr +=
    // patch_save_single_register(curr_addr, regs_offset_from_rsp +
    // TRACER_REG_RDX
    // * 8, RDX);
    // curr_addr +=
    //    patch_save_single_register(curr_addr, regs_offset_from_rsp +
    //    TRACER_REG_RAX * 8, RAX);
    curr_addr += patch_save_single_register(
        curr_addr, regs_offset_from_rsp + TRACER_REG_RCX * 8, RCX);
    // curr_addr +=
    //    patch_save_single_register(curr_addr, regs_offset_from_rsp +
    //    TRACER_REG_RSP * 8, RSP);

    // mov the rsp down so we do not destroy the stack
    // sub rsp,bsae
    // we use lea since it does not modify the flags, the sub instruction
    // modifies flags which may lead to us modifiying the flag register
    curr_addr[0] = 0x48;
    curr_addr[1] = 0x8d;
    curr_addr[2] = 0xa4;
    curr_addr[3] = 0x24;

    *((int *)&curr_addr[4]) = regs_offset_from_rsp;
    curr_addr += 8;
    *offset = -regs_offset_from_rsp;
    return curr_addr - current_address;
}

static int patch_restore_registers(char *address) {
    char *curr_addr = address;
    // restore the flags register, but we do that before
    //
    // mov r15, [rax + offset *8 ]
    curr_addr[0] = 0x4c;
    curr_addr[1] = 0x8b;
    curr_addr[2] = 0xbc;
    curr_addr[3] = 0x24;
    int index = TRACER_REG_FLAGS;
    *((int *)&curr_addr[4]) = index * 8;
    // pushfq
    curr_addr[8] = 0x9c;
    // mov [rsp], r15
    curr_addr += 9;
    curr_addr[0] = 0x4c;
    curr_addr[1] = 0x89;
    curr_addr[2] = 0x3c;
    curr_addr[3] = 0x24;
    // popfq
    curr_addr[4] = 0x9d;
    curr_addr += 5;
    curr_addr +=
        patch_restore_single_register(curr_addr, TRACER_REG_R8 * 8, R8);
    curr_addr +=
        patch_restore_single_register(curr_addr, TRACER_REG_R9 * 8, R9);
    curr_addr +=
        patch_restore_single_register(curr_addr, TRACER_REG_R10 * 8, R10);
    curr_addr +=
        patch_restore_single_register(curr_addr, TRACER_REG_R11 * 8, R11);
    curr_addr +=
        patch_restore_single_register(curr_addr, TRACER_REG_R12 * 8, R12);
    curr_addr +=
        patch_restore_single_register(curr_addr, TRACER_REG_R13 * 8, R13);
    curr_addr +=
        patch_restore_single_register(curr_addr, TRACER_REG_R14 * 8, R14);
    curr_addr +=
        patch_restore_single_register(curr_addr, TRACER_REG_R15 * 8, R15);
    curr_addr +=
        patch_restore_single_register(curr_addr, TRACER_REG_RDI * 8, RDI);
    curr_addr +=
        patch_restore_single_register(curr_addr, TRACER_REG_RSI * 8, RSI);
    curr_addr +=
        patch_restore_single_register(curr_addr, TRACER_REG_RBP * 8, RBP);
    curr_addr +=
        patch_restore_single_register(curr_addr, TRACER_REG_RBX * 8, RBX);
    curr_addr +=
        patch_restore_single_register(curr_addr, TRACER_REG_RDX * 8, RDX);
    curr_addr +=
        patch_restore_single_register(curr_addr, TRACER_REG_RAX * 8, RAX);
    curr_addr +=
        patch_restore_single_register(curr_addr, TRACER_REG_RCX * 8, RCX);
    curr_addr +=
        patch_restore_single_register(curr_addr, TRACER_REG_RSP * 8, RSP);
    return curr_addr - address;
}
static int patch_collect(ZydisDisassembledInstruction *instruction,
                         char *address_of_instruction, char *address,
                         long *store_at_end, ContextInfo *context_info,
                         AddressReg *value_reg) {
    used_regs used = get_used_registers(instruction);
    used_regs modified = get_modified_registers(instruction);
    AddressReg valueReg =
        get_register_for_trace_value_address(instruction, used | modified);
    *value_reg = valueReg;
    int size = 0;
    if (is_write(instruction)) {
        size += patch_create_context(
            (long)address, valueReg, used, 1, 1,
            modified | address_reg_to_tracer_reg(valueReg), store_at_end,
            context_info, get_actual_used_registers(instruction));
    }
    TracerRegister reg = address_reg_to_tracer_reg(valueReg);
    used |= reg;

#ifndef TRACER_LOG_ACCESSES
#ifdef TRACER_COLLECT_VALUE
    if (is_write(instruction)) {
        size += patch_collect_instructions((long)address + size,
                                           address_of_instruction, instruction,
                                           valueReg, used, 1);
    }
#endif
    if (used & RSP) {
        // we destroyed the rsp by createing the context, so now we have to
        // restore it
        TracerRegister rsp_reg = context_info->reg_for_rsp;
        size += patch_reg_mov(address + size, rsp_reg, RSP);
    }

#else
#ifdef TRACER_COLLECT_VALUE
    if (is_write(instruction)) {
        CollectionInfo info;
        size += patch_collect_instructions((long)address + size,
                                           address_of_instruction, instruction,
                                           valueReg, used, 1, &info);
    }
#endif
#endif
    return size;
}
static int patch_enable_pkey(char *address, int pkey) {
#ifdef TRACER_TRACE_NONE
    int mask = 0;
    return 0;
#endif
#ifdef TRACER_USERSPACE
    // clear ecx, mov ecx, 0x0
    address[0] = 0xb9;
    address[1] = 0x00;
    address[2] = 0x00;
    address[3] = 0x00;
    address[4] = 0x00;
    // read the old value of pkru into rax, this clears edx
    address[5] = 0x0f;
    address[6] = 0x01;
    address[7] = 0xEE;
    // calculate the mask
// see intel manual
#ifdef TRACER_TRACE_WRITES
    int mask = 0x2;
#endif
#ifdef TRACER_TRACE_ALL
    int mask = 0x1;
#endif
    mask = mask << (pkey * 2);
    // we use a LEA instruction since we know that those bits are not set and
    // this allows us to not to modify the flags register, which is important if
    // we have a conditional jump in the end LEA eax, [eax + mask]
    address[8] = 0x67;
    address[9] = 0x8d;
    address[10] = 0x80;
    *((int *)&address[11]) = mask;
    // mask now has the bits we want to set
    // or eax, mask (mask is immediate)
    // write the new value into eax, ecx and edx are already cleared
    address[15] = 0x0f;
    address[16] = 0x01;
    address[17] = 0xef;
    return 18;
#else
    // mov msr index into ecx
    // mov ecx, 1761
    address[0] = 0xb9;
    address[1] = 0xe1;
    address[2] = 0x06;
    address[3] = 0x00;
    address[4] = 0x00;
    // read the old value of pkru into rax, this clears edx
    address[5] = 0x0f;
    address[6] = 0x32;
    // calculate the mask
// see intel manual
#ifdef TRACER_TRACE_WRITES
    int mask = 0x2;
#endif
#ifdef TRACER_TRACE_ALL
    int mask = 0x1;
#endif
    mask = mask << (pkey * 2);
    // we use a LEA instruction since we know that those bits are not set
    // and this allows us to not to modify the flags register, which is
    // important if we have a conditional jump in the end LEA eax, [eax +
    // mask]
    address[7] = 0x67;
    address[8] = 0x8d;
    address[9] = 0x80;
    *((int *)&address[10]) = mask;
    // mask now has all bits set except the two for the used pkey
    // write the new value into the msr, the address is still in ecx
    address[14] = 0x0f;
    address[15] = 0x30;
    return 16;
#endif
}

static int patch_call_collect_post(char *address, ProbeSite *probe_site) {
    // setup arguments, we need the regs in the rdi, and the address of the
    // instruction we trace in rsi mov rdi, rsp
    address[0] = 0x48;
    address[1] = 0x89;
    address[2] = 0xE7;

    // mov rsi, addrss of instruction
    address[3] = 0x48;
    address[4] = 0xBE;
    *((long *)&address[5]) = (long)probe_site->rip_of_instruction;

    // mov rax, address of collect_post
    // address[13] = 0x9c;
    address[13] = 0x48;
    address[14] = 0xB8;
    // we pushfq because we need to keep it acroso the post collector wrapper
    // call and the compiler does some sub rsp stuff, which destroys the flags,
    // so we have to push them onto the stack and reload them from there (yes
    // this is ugly but the alternative would have been to patch the loaded
    // program)
    *((long *)&address[15]) = (long)&collect_post_wrapper;
    // call [rax]
    address[23] = 0xff;
    address[24] = 0xD0;
    return 25;
}
int patch_execute(ProbeSite *probe_site, char *address, long *store_at_end,
                  int is_single_instruction, long address_of_single_instruction,
                  ContextInfo *context_info, int pkey, int is_tracing_following,
                  AddressReg value_reg) {
    TRACER_PRINT_DEBUG_TRAMPOLINES(
        "Patching the executiono of the following instructions:");
#ifdef TRACER_LOG_DEBUG_TRAMPOLINES

    for (int i = 0; i < probe_site->num_instructions; i++) {
        TRACER_PRINT_DEBUG_TRAMPOLINES(
            "Instruction %i: %s, size %i", i, probe_site->instructions[i]->text,
            probe_site->instructions[i]->info.length);
    }
#endif
    long new_address = (long)address;
    char *address_of_instruction;
    int is_following;
    tracer_follow_type type;

    if (is_single_instruction) {
        address_of_instruction = (char *)address_of_single_instruction;
    } else {
        address_of_instruction = (char *)find_displaced_location(
            (long)probe_site->rip_of_instruction, displaced_instructions,
            &is_following, &type);
    }

    // the first instruction is the one we definitly want to trace, so we create
    // the context, execute it and save the updated registers
    used_regs modified_registers =
        get_modified_registers(probe_site->instructions[0]);
    used_regs used = get_used_registers(probe_site->instructions[0]);
    used_regs used_by_rest = 0;
    used_regs used_by_rest_actually = 0;
    used_regs modified_by_rest = 0;
    int jump_at = -1;
    for (int i = 1; i < probe_site->num_instructions; i++) {
        if (get_jump_type(probe_site->instructions[i]) != TRACER_JUMP_NONE) {
            jump_at = i;
        };
        used_by_rest |= get_used_registers(probe_site->instructions[i]);
        modified_by_rest |= get_modified_registers(probe_site->instructions[i]);
        used_by_rest_actually |=
            get_actual_used_registers(probe_site->instructions[i]);
    }
    if (jump_at != -1) {
        get_displaced_location_info((long)probe_site->rip_of_instruction,
                                    displaced_instructions)
            ->type = TRACER_DO_NOT_TRACE;
        // mov
    }
    if (used > 0 && used & RAX) {
        // TODO: we have to keep track of all the registers we globber for
        // the execution up until here if the previous context restore
        // overwrites rax, we have to put rsp into rax again mov rax, rsp
        // PERF: i think this is often unessecary

        ((char *)new_address)[0] = 0x48;
        ((char *)new_address)[1] = 0x89;
        ((char *)new_address)[2] = 0xe0;
        new_address += 3;
        used |= RAX;
    }
    int include_load = is_write(probe_site->instructions[0]) ? 0 : 1;
    if (!is_single_instruction) {
        new_address += patch_create_context(
            new_address, value_reg, used, include_load, 1, modified_registers,
            store_at_end, context_info,
            get_actual_used_registers(probe_site->instructions[0]));

    } else {
        new_address += patch_create_context(
            new_address, value_reg, used, 1, 0, modified_registers,
            store_at_end, context_info,
            get_actual_used_registers(probe_site->instructions[0]));
    }

    if (!is_single_instruction ||
        get_jump_type(probe_site->instructions[0]) == TRACER_JUMP_NONE) {
        long patched_address;
        int length = patch_execute_single_instruction(
            (char *)new_address, address_of_instruction,
            probe_site->instructions[0], (long)address_of_instruction,
            &patched_address);
        new_address += length;
    }
    if (used & RBP) {
        // restore rbp
        // pop rbp
        // ((char *)new_address)[0] = 0x5d;
        // new_address += 1;
    }
#ifdef TRACER_COLLECT_VALUE
    if (!is_write(probe_site->instructions[0]) && !is_single_instruction) {
        used_regs used = get_used_registers(probe_site->instructions[0]) |
                         address_reg_to_tracer_reg(value_reg);
#ifndef TRACER_LOG_ACCESSES
        new_address += patch_collect_instructions(
            (long)new_address, probe_site->address_of_instruction,
            probe_site->instructions[0], value_reg, used, 1);
#else
        CollectionInfo info;
        new_address += patch_collect_instructions(
            (long)new_address, probe_site->address_of_instruction,
            probe_site->instructions[0], value_reg, used, 1, &info);
#endif
    }
#endif
    if (is_single_instruction) {
        new_address += patch_store_updated_regs((char *)new_address, used,
                                                modified_registers, 0,
                                                store_at_end, context_info);
    } else {
        new_address += patch_store_updated_regs((char *)new_address, used,
                                                modified_registers, 1,
                                                store_at_end, context_info);
    }

// then, we call the post handler
#ifdef TRACER_USE_POST_HANDLER
    if (!is_single_instruction) {
        new_address += patch_call_collect_post((char *)new_address, probe_site);
    }
#endif

    // re-enable the pkru
    if (!is_single_instruction && probe_site->num_instructions > 1) {
        new_address += patch_enable_pkey((char *)new_address, pkey);
    }
    // move rsp, rax
    if (!is_single_instruction) {
        ((char *)new_address)[0] = 0x48;
        ((char *)new_address)[1] = 0x89;
        ((char *)new_address)[2] = 0xe0;
        new_address += 3;
    }
    if (is_single_instruction || probe_site->num_instructions == 1) {
        // early return in case there are no following instructions
        return new_address - (long)address;
    }

    // then, create context for the rest of the instructions
    new_address += patch_create_context(new_address, 0, used_by_rest, 0, 1,
                                        modified_by_rest, store_at_end,
                                        context_info, used_by_rest_actually);

    int offset = 0;

    TRACER_PRINT_DEBUG_TRAMPOLINES("num instructions:%i, jump_at is %i",
                                   probe_site->num_instructions, jump_at);
    for (int i = 1; i < probe_site->num_instructions; i++) {
        if (get_jump_type(probe_site->instructions[i]) != TRACER_JUMP_NONE &&
            i == probe_site->num_instructions) {
            break;
        }
        int index;
        if (i == 1 && !is_tracing_following) {

#ifdef TRACER_USERSPACE
            index = get_tid(pthread_self()) - id_offset;
            TRACER_PRINT_DEBUG_TRAMPOLINES("index in trampoline is %i", index);
#else
            index = 0;
#endif

            thread_mappings->mappings[index]
                .following_info.expected_new_address = new_address;
            TRACER_PRINT_DEBUG_TRAMPOLINES(
                "Settings following_info.orignial_address to %lx",
                (long)probe_site->rip_of_instruction);
            thread_mappings->mappings[index].following_info.original_address =
                (long)probe_site->rip_of_instruction;
            thread_mappings->mappings[index]
                .following_info.offset_from_original_address =
                probe_site->instructions[0]->info.length;
        }
        offset += probe_site->instructions[i - 1]->info.length;

        long patched_address;
        if (jump_at == -1 || i < jump_at) {
            TRACER_PRINT_DEBUG_TRAMPOLINES("installing following instrutcion");

            long __attribute__((unused)) address_of_patched_instruction;
            int length = patch_execute_single_instruction(
                (char *)new_address, address_of_instruction + offset,
                probe_site->instructions[i],
                (long)probe_site->rip_of_instruction, &patched_address);
            new_address += length;
            // if (get_used_registers(probe_site->instructions[i]) & RBP) {
            //    ((char *)new_address)[0] = 0x5d;
            //   new_address += 1;
            //}
            // add a nop to prevent following trampolines form having a
            // traced following instruction as well, otherwise it could
            // happen that we get some kind of recursion / many calls
            if (!is_tracing_following) {
                for (int i = length; i < 5; i++) {
                    *((char *)new_address) = 0x90;
                    new_address += 1;
                }
            }
            // after the first of the following instructions, install the
            // tracing_probe
#ifdef TRACER_TRACE_SUBSEQUENT
            if (i == 1 && !is_tracing_following) {
                // first, we patch a jump that we can overwrite
                *((char *)(new_address + 0)) = 0xe9;
                *((char *)(new_address + 1)) = 0x00;
                *((char *)(new_address + 2)) = 0x00;
                *((char *)(new_address + 3)) = 0x00;
                *((char *)(new_address + 3)) = 0x00;
                long address_of_jump_patch = new_address + 1;
                new_address += 5;

                used_regs modified_by_second =
                    get_modified_registers(probe_site->instructions[i]);
                used_regs used_by_second =
                    get_used_registers(probe_site->instructions[i]);
                used_regs used_by_second_actually =
                    get_used_xmm_registers(probe_site->instructions[i]);
                // then, we restore the context
                if (used_by_second & RBP) {
                    // restore rbp
                    // pop rbp
                    //((char *)new_address)[0] = 0x5d;
                    // new_address += 1;
                }
                new_address += patch_store_updated_regs(
                    (char *)new_address, used_by_second, modified_by_second, 1,
                    store_at_end, context_info);
                // mov rdi, address of instruction that we expected not to trace

                *((char *)(new_address + 0)) = 0x48;
                *((char *)(new_address + 1)) = 0xbf;
                *((long *)(new_address + 2)) = patched_address;
                new_address += 10;

                //*((char *)(new_address + 0)) = 0x9c;
                *((char *)(new_address + 0)) = 0xff;
                *((char *)(new_address + 1)) = 0x15;
                *((int *)(new_address + 2)) =
                    (int)((((long)store_at_end + TAKEN_PROBE_ADDRESS * 8)) -
                          (new_address + 6));

                new_address += 6;
                // mov rsp to rax again cause the probe destroys rax
                *((char *)(new_address + 0)) = 0x48;
                *((char *)(new_address + 1)) = 0x89;
                *((char *)(new_address + 2)) = 0xe0;

                new_address += 3;

                thread_mappings->mappings[index].following_info.probe_address =
                    address_of_jump_patch;

                // put the location we patch the jump to rdi -> first arg
                new_address += patch_create_context(
                    new_address, 0, used_by_rest & ~used_by_second, 0, 1,
                    modified_by_rest & ~modified_by_second, store_at_end,
                    context_info,
                    used_by_rest_actually & ~used_by_second_actually);
                int length = new_address - (address_of_jump_patch + 4);
                thread_mappings->mappings[index].following_info.length = length;
            }
#endif
        }
    }
    if (used_by_rest & RBP) {
        // restore rbp
        // pop rbp
        //((char *)new_address)[0] = 0x5d;
        // new_address += 1;
    }
    // store the updated regs in the end
    new_address += patch_store_updated_regs((char *)new_address, used_by_rest,
                                            modified_by_rest, 1, store_at_end,
                                            context_info);
    return new_address - (long)address;
}
static int patch_collect_and_execute(ProbeSite *probe_site, char *address,
                                     long *store_at_end, int pkey,
                                     int is_tracing_following) {
    // globbered_regs contains the registers
    char *current_address = address;
    ContextInfo context_info;
    AddressReg value_reg;
    current_address += patch_collect(
        probe_site->instructions[0], probe_site->address_of_instruction,
        address, store_at_end, &context_info, &value_reg);
    current_address +=
        patch_execute(probe_site, current_address, store_at_end, 0, 0,
                      &context_info, pkey, is_tracing_following, value_reg);
    return current_address - address;
}

/*static int patch_mov_rsp_down(char *address, int offset) {
    // sub rsp, 0x88
    address[0] = 0x48;
    address[1] = 0x81;
    address[2] = 0xec;
    *((int *)&address[3]) = offset;
    return 7;
}
*/

int patch_modified_jump(ZydisDisassembledInstruction *instruction,
                        ProbeSite *probe_site, char *address, int offset,
                        int how_many_to_jump) {
    int curr = 0;
    for (int j = 0; j < instruction->info.raw.imm[0].offset; j++) {
        address[j] = *(probe_site->address_of_instruction + offset + j);
        curr += 1;
    }
    if (instruction->info.raw.imm[0].size == 32) {
        *((int *)&address[curr + 0]) = how_many_to_jump;
        curr += 4;
    }
    if (instruction->info.raw.imm[0].size == 16) {
        *((int *)&address[curr + 0]) = how_many_to_jump;
        curr += 2;
    }
    if (instruction->info.raw.imm[0].size == 8) {
        *((int *)&address[curr + 0]) = how_many_to_jump;
        curr += 1;
    }
    return curr;
}
static int patch_jump(char *address, long *store_at_end,
                      ProbeSite *probe_site) {
    // jmp -8[rsp]
    // https://stackoverflow.com/questions/47775452/how-do-i-skip-exactly-1-instruction-with-a-jump-relative-to-rip-in-x64-asm
    // --> jumps encode offset from the end of the instruction, our jump
    // instruction has 6 bytes this is the offset to the end of the
    // allocated trampoline
    int curr = 0;
    int offset = 0;
    int found_cond_jump = 0;
    int index_with_uncond_jump = -1;
    for (int i = 0; i < probe_site->num_instructions; i++) {
        if (get_jump_type(probe_site->instructions[i]) == TRACER_JUMP_RET) {
            address[0] = 0xc3;
            return 1;
        }
        if (get_jump_type(probe_site->instructions[i]) == TRACER_JUMP_UNCOND &&
            probe_site->instructions[i]->operands[0].type ==
                ZYDIS_OPERAND_TYPE_MEMORY) {
            // we found a jump of whcih the target is a memory location
            // TODO: the same thing should work if the address is in the
            // register directly
            index_with_uncond_jump = i;
            break;
        }
        if (get_jump_type(probe_site->instructions[i]) == TRACER_JUMP_COND) {
            found_cond_jump = 1;

            // we found a conditional jump, so now we copy over the opcode
            // of the jump and set the the offset to 6 so we jump over the
            // following instruction which is the 'normal jump'
            // we can just copy over from the original address + offset,
            // since we only have patched the first byte up until here
            ZydisDisassembledInstruction *instruction =
                probe_site->instructions[i];
            curr += patch_modified_jump(instruction, probe_site, address,
                                        offset, 0x6);
            break;
        }
        offset += probe_site->instructions[i]->info.length;
    }
    if (index_with_uncond_jump != -1) {
        // we found a unconditional jump with memory location, so just copy
        // over the instruction
        int length =
            probe_site->instructions[index_with_uncond_jump]->info.length;
        for (int i = 0; i < length; i++) {
            address[i] = *(probe_site->address_of_instruction + offset + i);
        }
        return length;

    } else {

        // This is the normal jump
        int offset_for_instr = (long)&store_at_end[RETURN_ADDRESS_INDEX] -
                               (long)address - curr - 6;
        address[0 + curr] = 0xff;
        address[1 + curr] = 0x25;
        *((int *)&address[2 + curr]) = offset_for_instr;
        curr += 6;
        if (found_cond_jump) {
            int alt_offset_for_instr =
                (long)&store_at_end[JUMP_TAKEN_RETURN_ADDRESS_INDEX] -
                (long)address - curr - 6;
            address[0 + curr] = 0xff;
            address[1 + curr] = 0x25;
            *((int *)&address[2 + curr]) = alt_offset_for_instr;
            curr += 6;
        }
        return curr;
    }
}

static int patch_call_collect_pre(char *address, ProbeSite *probe_site) {
    // setup arguments, we need the regs in the rdi, and the address of the
    // instruction we trace in rsi mov rdi, rsp
    address[0] = 0x48;
    address[1] = 0x89;
    address[2] = 0xE7;

    // mov rsi, addrss of instruction
    address[3] = 0x48;
    address[4] = 0xBE;
    *((long *)&address[5]) = (long)probe_site->rip_of_instruction;

    // mov rdx, orginal_addrss
    address[13] = 0x48;
    address[14] = 0xBA;
    *((long *)&address[15]) = (long)probe_site->original_address;
// mov rcx, index
    address[23] = 0x48;
    address[24] = 0xB9;
    *((long *)&address[25]) = (long)probe_site->index;

    // mov rax, address of collect_pre
    address[33] = 0x48;
    address[34] = 0xB8;
    *((long *)&address[35]) = (long)&collect_pre_wrapper;
    // call [rax]
    address[43] = 0xff;
    address[44] = 0xD0;
    return 45;
}
static int patch_disable_pkey(char *address, int pkey) {
#ifdef TRACER_USERSPACE
    // clear ecx, mov ecx, 0x0
    address[0] = 0xb9;
    address[1] = 0x00;
    address[2] = 0x00;
    address[3] = 0x00;
    address[4] = 0x00;
    // read the old value of pkru into rax, this clears edx
    address[5] = 0x0f;
    address[6] = 0x01;
    address[7] = 0xEE;
    // calculate the mask
    int mask = 0x3;
    mask = mask << (pkey * 2);
    mask = ~mask;
    // mask now has all bits set except the two for the used pkey
    // AND rax, mask (mask is immediate)
    address[8] = 0x25;
    *((int *)&address[9]) = mask;
    // write the new value into eax, ecx and edx are already cleared
    address[13] = 0x0f;
    address[14] = 0x01;
    address[15] = 0xef;
    return 16;
#else
    // mov msr index into ecx
    // mov ecx, 1761
    address[0] = 0xb9;
    address[1] = 0xe1;
    address[2] = 0x06;
    address[3] = 0x00;
    address[4] = 0x00;
    // read the old value of pkru into rax, this clears edx
    address[5] = 0x0f;
    address[6] = 0x32;
    // address[7] = 0xEE;
    //  calculate the mask
    int mask = 0x3;
    mask = mask << (pkey * 2);
    mask = ~mask;
    // mask now has all bits set except the two for the used pkey
    // AND rax, mask (mask is immediate)
    address[7] = 0x25;
    *((int *)&address[8]) = mask;
    // write the new value into the msr, the address is still in ecx
    address[12] = 0x0f;
    address[13] = 0x30;
    return 14;
#endif
}
static void patch_tracing_probe(long *store_at_end) {
    TRACER_PRINT_DEBUG_TRAMPOLINES("Putting address of probe at %lx",
                                   (long)store_at_end +
                                       TAKEN_PROBE_ADDRESS * 8);
    *((long *)((long)store_at_end + TAKEN_PROBE_ADDRESS * 8)) =
        (long)&tracing_probe;
}

static int patch_call_target(ProbeSite *probe_site, char *current_address,
                             long *store_at_end) {
    int ins_offset = 0;
    for (int i = 0; i < probe_site->num_instructions; i++) {
        if (get_jump_type(probe_site->instructions[i]) == TRACER_JUMP_CALL) {
            // we abuse the jump target taken thingy because we dont need it on
            // this trampoline
            ZydisDisassembledInstruction *instruction =
                probe_site->instructions[i];
            // in this case, we store the return address that the code
            // would jump to if the jump is taken
            int offset = instruction->info.raw.imm[0].value.s;
            int offset_bits = 32;
            // i believe we have to to the following cases to make sure
            // that the values are sign extended correctly (so that a
            // negative number does not become positive if we just put
            // it into the bigger address location)
            if (instruction->info.raw.imm[0].size == 8) {
                offset = (char)instruction->info.raw.imm[0].value.s;
                offset_bits = 8;
            }
            if (instruction->info.raw.imm[0].size == 16) {
                offset = (short)instruction->info.raw.imm[0].value.s;
                offset_bits = 16;
            }
            // add offset_bits / 8 + 1 for the size of the call instruciton
            long alt_return_address = (long)probe_site->rip_of_instruction +
                                      offset + ins_offset + 1 + offset_bits / 8;
            // store the alternate jump target at the end of the
            // trmapoline write the return address into rax
            current_address[0] = 0x48;
            current_address[1] = 0xB8;
            *((long *)&current_address[2]) = alt_return_address;
            // then we load the location for the return address into rbx
            current_address[10] = 0x48;
            current_address[11] = 0xbb;
            *((long *)&current_address[12]) =
                (long)&store_at_end[JUMP_TAKEN_RETURN_ADDRESS_INDEX];
            // now, we do mov [rbx], rax
            // then, we can jump back with an rip-relative instruction
            // (do not do this at home kids...)
            current_address[20] = 0x48;
            current_address[21] = 0x89;
            current_address[22] = 0x03;
            return 23;
        }
        ins_offset += probe_site->instructions[i]->info.length;
    }

    return 0;
}
static int patch_call(char *current_address, long *store_at_end,
                      ProbeSite *probe_site) {
    // add 6 for the size of the instruciton
    int offset = (long)&store_at_end[JUMP_TAKEN_RETURN_ADDRESS_INDEX] -
                 ((long)current_address + 6);
    current_address[0] = 0xff;
    current_address[1] = 0x15;
    *((int *)&current_address[2]) = offset;
    return 6;
}

int write_trampoline(ProbeSite *probe_site, long address_of_trampoline,
                     int pkey, int is_tracing_following) {
    // we first have to save the registers, but we want to construct a
    // 'normal' stack frame at the same time so that we can just 'ret' at
    // the end store thus, we store them at the offset from rsp: The
    // registers themselves require 16 * 8 = 120 byte, and the return
    // address and the old rbp value require 8 bytes each. Thus, we store
    // the registers at rsp - (120 + 16) = rsp - 136
    long *store_at_end =
        (long *)(address_of_trampoline + MAX_TRAMPOLINE_SIZE - STORE_SIZE_END);
    TRACER_PRINT_DEBUG_TRAMPOLINES("store at end is %lx", (long)store_at_end);
    char *current_address = (char *)address_of_trampoline;
    int offset;
    patch_tracing_probe(store_at_end);
    current_address += patch_set_rsp_to_new_stack(
        (char *)current_address, store_at_end, is_tracing_following);
    current_address += patch_save_registers((char *)current_address, &offset);
    current_address +=
        patch_store_return_address(probe_site, current_address, store_at_end);
    int has_call = patch_call_target(probe_site, current_address, store_at_end);
    current_address += has_call;
    // current_address += patch_mov_rsp_down(current_address, offset);
    //  TODO: now, we need to set up the parameters for the pre handler and
    //  call it, we assume that it returns the address we should write the
    //  value to in rax (the normal return parameter)
    current_address += patch_disable_pkey(current_address, pkey);
    current_address += patch_call_collect_pre(current_address, probe_site);

    // set up the paramers for the stub function
    current_address += patch_prepare_stub_arguments(current_address);
    // now, we can patch the stuff that normally goes into the stub function
    current_address += patch_collect_and_execute(
        probe_site, current_address, store_at_end, pkey, is_tracing_following);
    // TODO: call post if we need it
    // //
    if (probe_site->num_instructions == 1) {
        current_address += patch_enable_pkey(current_address, pkey);
    }
    current_address += patch_restore_registers(current_address);
    if (has_call) {
        current_address +=
            patch_call(current_address, store_at_end, probe_site);
    }
    current_address += patch_jump(current_address, store_at_end, probe_site);

    int size = (long)current_address - address_of_trampoline;
#ifdef TRACER_LOG_ERROR
    // check that there is enough space after the instructions to fit the
    // rsp
    if (MAX_TRAMPOLINE_SIZE - size < STORE_SIZE_END) {
        TRACER_PRINT_ERROR("Trampoline size is too small");
    }
#endif

    return size;
}
