#include "config.h"
#include "instruction.h"
#ifdef TRACER_USERSPACE
#include <string.h>
#endif
#include "collector.h"
#include "logging.h"
#include "patcher.h"
int patch_mov_absolute_offset_to_reg(ZydisDisassembledInstruction *instruction,
                                     TracerRegister reg,
                                     char *address_to_patch) {
    int index = tracer_reg_to_index(reg);
    // TODO: this is not tested for all cases
    address_to_patch[0] = 0x48;
    if (index >= 8) {
        address_to_patch[0] |= 0x01;
    }
    address_to_patch[1] = 0xb8 + (index % 8);
    *((long *)&address_to_patch[2]) = instruction->runtime_address;
    // now we need to add the offset
    int rip_offset = instruction->info.raw.disp.value;
    // add temp_reg, rip_offset
    address_to_patch[10] = 0x48 + (index >= 8 ? 1 : 0);
    address_to_patch[11] = 0x81;
    address_to_patch[12] = 0xC0 + (index % 8);
    *((int *)&address_to_patch[13]) = rip_offset + instruction->info.length;
    return 17;
}
int patch_execute_single_instruction(char *address_to_patch,
                                     char *address_of_instruction,
                                     ZydisDisassembledInstruction *instruction,
                                     long rip,
                                     long *address_of_patched_instruction) {
    TRACER_PRINT_DEBUG_PATCHER(
        "Patching instruction at %px with the instruction from %px ",
        address_to_patch, address_of_instruction);
    // copy over the instruction
    // TODO: this should proably use a memcpy
    TRACER_PRINT_DEBUG_PATCHER("Location of the patch before : %lx %lx",
                               *((unsigned long *)address_to_patch),
                               *((unsigned long *)(address_to_patch + 8)));

    used_regs used = get_used_registers(instruction);
    int total_offset = 0;
    int offset = 0;
    if (used & RIP) {
        // instruction uses rip, so we have to handle it here
        TracerRegister temp_reg = temp_reg = get_and_set_unused_register(&used);
        // we load the rip into that and than add the offset to it

        // fix up the instruction to write to the address stored in temp_reg
        offset += patch_mov_absolute_offset_to_reg(instruction, temp_reg,
                                                   address_to_patch);

        *address_of_patched_instruction = (long)address_to_patch + offset;
        offset +=
            set_mem_address_to_reg_address(instruction, address_of_instruction,
                                           address_to_patch + offset, temp_reg);
        total_offset += offset;
    } else {
        *address_of_patched_instruction = (long)address_to_patch + offset;
        total_offset += instruction->info.length;
        for (size_t i = 0; i < instruction->info.length; i++) {
            uint8_t *to_address = ((uint8_t *)address_to_patch) + offset + i;
            uint8_t *from_address = ((uint8_t *)address_of_instruction) + i;

            TRACER_PRINT_DEBUG_PATCHER(
                "Patching address %px with value 0x%x from address %px",
                to_address,

                *from_address, from_address);
            *to_address = *from_address;
        }
    }

    TRACER_PRINT_DEBUG_PATCHER("location of the patch after : %lx %lx",
                               *((unsigned long *)address_to_patch),
                               *((unsigned long *)(address_to_patch + 8)));
    return total_offset;
}

void patch_clear_bytes(char *address, int length) {
    for (int i = 0; i < length; i++) {
        address[i] = 0x90;
    }
}

int patch_nop(char *address) {
    address[0] = 0x90;
    return 1;
}

int patch_fun_start(char *address, long storage_at_beginning) {
    int local_offset = 0;
    // this is literally the same asm that the compiler produced for the
    // stub_function push rbp
#ifdef abc

    // mov the rsp down by 200 bytes before we put stuff on the stack because
    // there is the redzone, we don not need to do this in the usersapce since
    // the signal handler gets its own stack anyways
    address[0] = 0x48;
    address[1] = 0x81;
    address[2] = 0xec;
    address[3] = 0xc8;
    address[4] = 0x00;
    address[5] = 0x00;
    address[6] = 0x00;
    local_offset += 7;
#endif
    address[0 + local_offset] = 0x55;
    // mov rbp, rsp
    address[1 + local_offset] = 0x48;
    address[2 + local_offset] = 0x89;
    address[3 + local_offset] = 0xe5;
    local_offset += patch_adjust_rsp_down((char *)(address + 4 + local_offset));
    // mov [rbp-0x8], rdi
    address[4 + local_offset] = 0x48;
    address[5 + local_offset] = 0x89;
    address[6 + local_offset] = 0x7d;
    address[7 + local_offset] = 0xf8;
    // mov [rbp-0x10], rsi
    address[8 + local_offset] = 0x48;
    address[9 + local_offset] = 0x89;
    address[10 + local_offset] = 0x75;
    address[11 + local_offset] = 0xf0;
    // mov [rbp-0x18], rdx
    address[12 + local_offset] = 0x48;
    address[13 + local_offset] = 0x89;
    address[14 + local_offset] = 0x55;
    address[15 + local_offset] = 0xe8;
    // put the base of the registers into the beginning storage, so that we can
    // reload rax from there mov [rip + offset], rdx
    address[16 + local_offset] = 0x48;
    address[17 + local_offset] = 0x89;
    address[18 + local_offset] = 0x3d;
    int offset =
        storage_at_beginning + 8 - ((long)address + 16 + local_offset + 7);
    *((int *)&address[19 + local_offset]) = offset;
    return 23 + local_offset;
}
int patch_xrstor(char *address) {
    // mov eax, 0x7
    address[0] = 0xb8;
    address[1] = 0x07;
    address[2] = 0x00;
    address[3] = 0x00;
    address[4] = 0x00;
    // mov rdx, rax
    address[5] = 0x48;
    address[6] = 0x89;
    address[7] = 0xc2;
    // shr rdx, 0x20
    address[8] = 0x48;
    address[9] = 0xc1;
    address[10] = 0xea;
    address[11] = 0x20;
    // mov rbx [rbp-0x18]
    address[12] = 0x48;
    address[13] = 0x8b;
    address[14] = 0x5d;
    address[15] = 0xe8;
    // xrstor [rbx]
    address[16] = 0x0f;
    address[17] = 0xae;
    address[18] = 0x2b;
    return 19;
}
int patch_xsave(char *address) {
    // mov eax, 0x7
    address[0] = 0xb8;
    address[1] = 0x07;
    address[2] = 0x00;
    address[3] = 0x00;
    address[4] = 0x00;
    // mov rdx, rax
    address[5] = 0x48;
    address[6] = 0x89;
    address[7] = 0xc2;
    // shr rdx, 0x20
    address[8] = 0x48;
    address[9] = 0xc1;
    address[10] = 0xea;
    address[11] = 0x20;
    // mov rbx [rbp-0x18]
    address[12] = 0x48;
    address[13] = 0x8b;
    address[14] = 0x5d;
    address[15] = 0xe8;
    // xsave [rbx]
    address[16] = 0x0f;
    address[17] = 0xae;
    address[18] = 0x23;
    return 19;
}
int patch_fun_end(char *address) {
    int local_offset = 0;
    // add rsp, 24
    address[0] = 0x48;
    address[1] = 0x83;
    address[2] = 0xc4;
    address[3] = 0x18;
#ifdef abc
    // add rsp, 200
    address[0] = 0x48;
    address[1] = 0x81;
    address[2] = 0xc4;
    address[3] = 0xc8;
    address[4] = 0x00;
    address[5] = 0x00;
    address[6] = 0x00;
    local_offset += 7;
#endif
    // pop rbp
    address[4 + local_offset] = 0x5d;
    // ret
    address[5 + local_offset] = 0xc3;
    return 6 + local_offset;
}
int patch_cond_jump_for_handler(ProbeSite *probe_site, char *address,
                                long storage_at_beginning) {

    char *current_address = address;

    // reload rax from begginging
    // mov rax, [rip + offset]
    int offset = storage_at_beginning + 8 - ((long)current_address + 7);
    current_address[0] = 0x48;
    current_address[1] = 0x8B;
    current_address[2] = 0x05;
    *((int *)&address[3]) = offset;
    current_address += 7;
    current_address += patch_create_context(
        (long)current_address, 0,
        get_used_registers(probe_site->instructions[0]), 0, 0,
        get_modified_registers(probe_site->instructions[0]), 0, NULL,
        get_actual_used_registers(probe_site->instructions[0]));
    current_address += patch_modified_jump(probe_site->instructions[0],
                                           probe_site, current_address, 0, 17);
    // if we do not jump, just add the length of the instruction to rip
    // add [rax + offset], instruction.length
    current_address[0] = 0x48;
    current_address[1] = 0x81;
    current_address[2] = 0x80;
    offset = TRACER_REG_RIP_DO_NOT_USE;
    *((int *)&current_address[3]) = offset * 8;
    *((int *)&current_address[7]) =
        (int)probe_site->instructions[0]->info.length ;
    current_address += 11;
    current_address += patch_fun_end(current_address);
    // if we do jump, we just mov the new address into the [rax + offset]
    // mov r15, address
    current_address[0] = 0x49;
    current_address[1] = 0xbf;
    *((long *)&current_address[2]) =
        (long)(probe_site->rip_of_instruction +
               probe_site->instructions[0]->info.length +
               probe_site->instructions[0]->operands[0].imm.value.s);
    current_address += 10;
    // mov [rax + offset], r15
    current_address[0] = 0x4c;
    current_address[1] = 0x89;
    current_address[2] = 0xb8;
    *((int *)&current_address[3]) = offset * 8;
    current_address += 7;
    return current_address - address;
}

int patch_adjust_rsp_down(char *address) {
    // sub rsp, 24
    address[0] = 0x48;
    address[1] = 0x83;
    address[2] = 0xec;
    address[3] = 0x18;
    return 4;
}
int patch_adjust_rsp_up(char *address) {
    // sub rsp, 24
    address[0] = 0x48;
    address[1] = 0x83;
    address[2] = 0xec;
    address[3] = 0x18;
    return 4;
}
void stub_function(tracer_regs_t regs, long address, long rstor_address) {
    // Make sure that these are not compiled out!
    // Put 15 nops here so even the longest instructions fit
    // PERF: theoretically, this can be optimized since we can figure out
    // the longest instruction that accesses memory, which should not be 15
    // bytes restore context
    // TODO: this will break if the instruction uses r15, we should make a
    // test case and make sure to handle that case individually
    //
    //
    // asm volatile("mov -16(%rbp,1), %rax");
    //
    /*
asm volatile("mov %0, %%rbx\n\t"
         "mov %1, %%rcx\n\t"
         "mov %2, %%rdx\n\t"
         "mov %3, %%rsi\n\t"
         "mov %4, %%r8\n\t"
         "mov %5, %%r9\n\t"
         "mov %6, %%r10\n\t"
         "mov %7, %%r11\n\t"
         "mov %8, %%r12\n\t"
         "mov %9, %%r13\n\t"
         "mov %10, %%r14\n\t"
         "mov %11, %%r15\n\t"
         "mov %12, %%rax\n\t"
         :

         : "m"(regs[TRACER_REG_RBX]), "m"(regs[TRACER_REG_RCX]),
           "m"(regs[TRACER_REG_RDX]), "m"(regs[TRACER_REG_RSI]),
           "m"(regs[TRACER_REG_R8]), "m"(regs[TRACER_REG_R9]),
           "m"(regs[TRACER_REG_R10]), "m"(regs[TRACER_REG_R11]),
           "m"(regs[TRACER_REG_R12]), "m"(regs[TRACER_REG_R13]),
           "m"(regs[TRACER_REG_R14]), "m"(regs[TRACER_REG_R15]),
           "m"(regs[TRACER_REG_RAX])
         :);
         */

    // we restore the xmm registers here since otherwise they might be destroyed
    // before (i think the main problem was the printf stuff)
    // PERF: this does not have executed for every location... not here and not
    // in the trampoline
    asm("mov $0x7, %%eax\n\t"
        "mov  %%rax, %%rdx\n\t"
        "shr $0x20, %%rdx\n\t"
        "mov %0, %%rbx\n\t"
        "xrstor (%%rbx)\n\t" ::"m"(rstor_address));
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    // TODO: restore the old context
    // asm volatile("mov %%rax, %0": "=m"(regs[TRACER_REG_RAX]): : "%rax");

    /*
asm volatile("mov %%rax, %%r15\n\t" : : :);
// the asm for the following modiefies the rax register when loading the
// addresses of the context..., thus, we save to rax register
// beforehand, this will break once we need to store/restore all
// registeres
// altough we could store rax first and then we dont care about the rest
asm volatile("mov %%rbx, %0\n\t"
         "mov %%rcx, %1\n\t"
         "mov %%rdx, %2\n\t"
         "mov %%r15, %3"
         : "=m"(regs[TRACER_REG_RBX]), "=m"(regs[TRACER_REG_RCX]),
           "=m"(regs[TRACER_REG_RDX]), "=m"(regs[TRACER_REG_RAX])
         :
         : "%rax", "%rbx", "%rcx", "%rdx");
*/
    return;
}
