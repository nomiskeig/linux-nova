#pragma once
#include "register.h"
#include "regs.h"
#include "shared.h"
#ifdef TRACER_USERSPACE
#include <Zydis/Zydis.h>
#else
#include <Zydis.h>
#endif
// I do not think that there are instructions that use more than 4 registers, so
// in the worst case, one of those 5 will not be used
typedef enum {
    ADDRESS_R15,
    ADDRESS_R14,
    ADDRESS_R13,
    ADDRESS_R12,
    ADDRESS_R11

} AddressReg;
// the leading opcodes bytes used for the 3 bytes vex encoding when modifiying the instruction to be able to trace them, see intel manual
typedef enum {
	NULL_F = 0x1,
	NULL_F_38 = 0x2,
	NULL_F_3A = 0x3
} VexOpcodeMap;
int patch_collect_instructions(unsigned long address_to_patch, char* address_of_instruction,
                               ZydisDisassembledInstruction *instruction,
                               AddressReg reg, used_regs used, int for_trampoline
#ifdef TRACER_LOG_ACCESSES
                               ,
                               CollectionInfo *info
#endif
);
AddressReg
get_register_for_trace_value_address(ZydisDisassembledInstruction *instruction,
                                     used_regs used);

int patch_mov_dest_addr_value_to_addr_in_reg(
    char *address_to_patch, int operand_size,
    ZydisDisassembledInstruction *instruction, AddressReg dest,
    used_regs *used);
// patches instruction so that they move the value the relative instruciton
// points to to the address dest points to
int patch_mov_relative_to_addr_in_reg(char *address_to_patch, int operand_size,
                                      ZydisDisassembledInstruction *instruction,
                                      AddressReg dest, used_regs *used);
TracerRegister address_reg_to_tracer_reg(AddressReg reg);
typedef enum { REG_TO_MEM, MEM_TO_REG } MOV_TYPE;
int set_mem_address_to_reg_address(ZydisDisassembledInstruction *instruction, char* instruction_address,
                                   char *address_to_patch, TracerRegister reg);
int set_mem_address_to_reg_address_vex(
    ZydisDisassembledInstruction *instruction, char *instruction_address,
    char *address_to_patch, TracerRegister reg, VexOpcodeMap map);
long get_and_set_value_address(ZydisDisassembledInstruction *instruction,
                               Trace *trace, Valuebuffer *buffer);
int needs_value_copy(ZydisDisassembledInstruction *instruction);
int load_rax(char *address);
