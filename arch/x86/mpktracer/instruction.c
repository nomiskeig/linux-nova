#include "instruction.h"
#include "config.h"
#include "logging.h"
#include "register.h"
#ifdef TRACER_USERSPACE
#include <stdio.h>
#endif

ZydisDecodedOperand *
get_register_opearand(ZydisDisassembledInstruction *instruction) {
    for (int i = 0; i < instruction->info.operand_count; i++) {
        if (instruction->operands[i].type == ZYDIS_OPERAND_TYPE_REGISTER) {
            return &instruction->operands[i];
        }
    }
    return NULL;
}
ZydisDecodedOperand *
get_memory_opearand(ZydisDisassembledInstruction *instruction) {
    for (int i = 0; i < instruction->info.operand_count; i++) {
        if (instruction->operands[i].type == ZYDIS_OPERAND_TYPE_MEMORY) {
            return &instruction->operands[i];
        }
    }
    return NULL;
}

int is_write(ZydisDisassembledInstruction *instruction) {

    // in case where we dont have a memory access we either dont care about us
    // collecting the info (like a mov from reg to reg or a jump), but we have
    // to catch that case elsewhere
    // we search the memory operand and then see if we read or write it, we need
    // to do this because cmp is a read but can have the memory operand in the
    // second operand (there may be other instructions as weill with this
    // feature, idk)
    // return instruction->operands[0].type == ZYDIS_OPERAND_TYPE_MEMORY ? 1 :
    // 0;
    for (int i = 0; i < instruction->info.operand_count; i++) {
        if (instruction->operands[i].type == ZYDIS_OPERAND_TYPE_MEMORY) {
            if (instruction->operands[i].actions &
                (ZYDIS_OPERAND_ACTION_WRITE | ZYDIS_OPERAND_ACTION_CONDWRITE)) {
                return 1;
            }
        }
    }
    return 0;
}
long get_value_size(ZydisDisassembledInstruction *instruction) {
    // TODO: this will break for reads as there not the first operand is the
    // destination
    for (int i = 0; i < instruction->info.operand_count; i++) {
        if (instruction->operands[i].type == ZYDIS_OPERAND_TYPE_MEMORY) {
            return instruction->operands[i].size / 8;
        }
    }
#ifdef TRACER_LOG_ERROR
    if (get_jump_type(instruction) == TRACER_JUMP_NONE) {

        TRACER_PRINT_ERROR(
            "did not find a memory operand, this should not happen");
    }
#endif
    return 0;
}
int needs_allignment(ZydisDisassembledInstruction *instruction) {
    switch (instruction->info.mnemonic) {
    case ZYDIS_MNEMONIC_MOVAPS:
    case ZYDIS_MNEMONIC_VMOVDQA:
        return 1;
    default:
        return 0;
    }
}

int needs_value_copy(ZydisDisassembledInstruction *instruction) {
    if (instruction->info.opcode_map == ZYDIS_OPCODE_MAP_0F) {
        switch (instruction->info.opcode) {
        case 0xb1:
            return 1;
        default:
            return 0;
        }
    }
    switch (instruction->info.opcode) {
    case 0x1:
    case 0x81:
        return 1;
    case 0x83: {
        switch (instruction->info.raw.modrm.reg) {
        case 0:
        case 5:
            return 1;
        default:
            return 0;
        }
        break;
    }

    default:
        return 0;
    }
}

JumpType get_jump_type(ZydisDisassembledInstruction *instruction) {
    used_regs used = get_used_registers(instruction);
    used_regs modified = get_modified_registers(instruction);
    if (instruction->info.mnemonic == ZYDIS_MNEMONIC_RET) {
        return TRACER_JUMP_RET;
    }
    if (instruction->info.mnemonic == ZYDIS_MNEMONIC_CALL) {
        return TRACER_JUMP_CALL;
    }
    if (!(modified & RIP)) {
        return TRACER_JUMP_NONE;
    }
    if (used & FLAGS) {
        return TRACER_JUMP_COND;
    }
    return TRACER_JUMP_UNCOND;
}
