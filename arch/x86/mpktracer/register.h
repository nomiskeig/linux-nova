#pragma once
#include "config.h"
#ifdef TRACER_USERSPACE
#include <Zydis/Disassembler.h>
#else 
#include <Zydis.h>
#endif
typedef int used_regs;

typedef enum {
    RAX = 0x1 << 0,
    RCX = 0x1 << 1,
    RDX = 0x1 << 2,
    RBX = 0x1 << 3,
    RSP = 0x1 << 4,
    RBP = 0x1 << 5,
    RSI = 0x1 << 6,
    RDI = 0x1 << 7,
    R8 = 0x1 << 8,
    R9 = 0x1 << 9,
    R10 = 0x1 << 10,
    R11 = 0x1 << 11,
    R12 = 0x1 << 12,
    R13 = 0x1 << 13,
    R14 = 0x1 << 14,
    R15 = 0x1 << 15,
    FLAGS = 01 << 16,
    RIP = 0x1 << 17,
    IGNORE = 0x1 << 18

} TracerRegister;
used_regs get_used_registers(ZydisDisassembledInstruction *instruction);
used_regs get_used_xmm_registers(ZydisDisassembledInstruction *instruction);
used_regs get_modified_registers(ZydisDisassembledInstruction *instruction);
used_regs get_modified_xmm_registers(ZydisDisassembledInstruction *instruction);
used_regs get_actual_used_registers(ZydisDisassembledInstruction *instruction);
int tracer_reg_to_index(TracerRegister reg);
int get_offset_of_reg(ZydisRegister reg);
int get_offset_of_xmm_reg(ZydisRegister reg);
TracerRegister get_and_set_unused_register(used_regs *used);
void print_registers(used_regs regs);
int tracer_reg_to_specific_reg_index(TracerRegister reg);
