#include "register.h"
#include "instruction.h"
#include "logging.h"
#include "regs.h"
#ifdef TRACER_USERSPACE
#include <Zydis/Disassembler.h>
#include <Zydis/SharedTypes.h>
#else
#include <Zydis.h>
#endif

int get_offset_of_xmm_reg(ZydisRegister reg) {
    switch (reg) {
    case ZYDIS_REGISTER_XMM0:
    case ZYDIS_REGISTER_YMM0:
        return RAX;

    case ZYDIS_REGISTER_XMM1:
    case ZYDIS_REGISTER_YMM1:
        return RCX;
    case ZYDIS_REGISTER_XMM2:
    case ZYDIS_REGISTER_YMM2:
        return RDX;
    case ZYDIS_REGISTER_XMM3:
    case ZYDIS_REGISTER_YMM3:
        return RBX;
    case ZYDIS_REGISTER_XMM4:
    case ZYDIS_REGISTER_YMM4:
        return RSP;
    case ZYDIS_REGISTER_XMM5:
    case ZYDIS_REGISTER_YMM5:
        return RBP;
    case ZYDIS_REGISTER_XMM6:
    case ZYDIS_REGISTER_YMM6:
        return RSI;
    case ZYDIS_REGISTER_XMM7:
    case ZYDIS_REGISTER_YMM7:
        return RDI;
    case ZYDIS_REGISTER_XMM8:
    case ZYDIS_REGISTER_YMM8:
        return R8;
    case ZYDIS_REGISTER_YMM9:
    case ZYDIS_REGISTER_XMM9:
        return R9;
    case ZYDIS_REGISTER_XMM10:
    case ZYDIS_REGISTER_YMM10:
        return R10;
    case ZYDIS_REGISTER_YMM11:
    case ZYDIS_REGISTER_XMM11:
        return R11;
    case ZYDIS_REGISTER_XMM12:
    case ZYDIS_REGISTER_YMM12:
        return R12;
    case ZYDIS_REGISTER_YMM13:
    case ZYDIS_REGISTER_XMM13:
        return R13;
    case ZYDIS_REGISTER_YMM14:
    case ZYDIS_REGISTER_XMM14:
        return R14;
    case ZYDIS_REGISTER_XMM15:
    case ZYDIS_REGISTER_YMM15:
        return R15;

    default:
        return 0;
    }
}

int get_offset_of_reg(ZydisRegister reg) {
    switch (reg) {
    case ZYDIS_REGISTER_R12:
    case ZYDIS_REGISTER_R12W:
    case ZYDIS_REGISTER_R12D:
    case ZYDIS_REGISTER_R12B:
        return R12;
    case ZYDIS_REGISTER_RAX:
    case ZYDIS_REGISTER_EAX:
    case ZYDIS_REGISTER_AX:
    case ZYDIS_REGISTER_AL:
        return RAX;
    case ZYDIS_REGISTER_RDX:
    case ZYDIS_REGISTER_EDX:
    case ZYDIS_REGISTER_DX:
    case ZYDIS_REGISTER_DL:
        return RDX;
    case ZYDIS_REGISTER_R13:
    case ZYDIS_REGISTER_R13W:
    case ZYDIS_REGISTER_R13D:
    case ZYDIS_REGISTER_R13B:
        return R13;
    case ZYDIS_REGISTER_R15:
    case ZYDIS_REGISTER_R15W:
    case ZYDIS_REGISTER_R15D:
    case ZYDIS_REGISTER_R15B:
        return R15;
    case ZYDIS_REGISTER_RBX:
    case ZYDIS_REGISTER_EBX:
    case ZYDIS_REGISTER_BX:
    case ZYDIS_REGISTER_BL:
        return RBX;
    case ZYDIS_REGISTER_RCX:
    case ZYDIS_REGISTER_ECX:
    case ZYDIS_REGISTER_CX:
    case ZYDIS_REGISTER_CL:
        return RCX;
    case ZYDIS_REGISTER_RSI:
    case ZYDIS_REGISTER_ESI:
    case ZYDIS_REGISTER_SI:
    case ZYDIS_REGISTER_SIL:
        return RSI;
    case ZYDIS_REGISTER_RDI:
    case ZYDIS_REGISTER_EDI:
    case ZYDIS_REGISTER_DI:
    case ZYDIS_REGISTER_DIL:
        return RDI;
    case ZYDIS_REGISTER_R8:
    case ZYDIS_REGISTER_R8W:
    case ZYDIS_REGISTER_R8D:
    case ZYDIS_REGISTER_R8B:
        return R8;
    case ZYDIS_REGISTER_R9:
    case ZYDIS_REGISTER_R9W:
    case ZYDIS_REGISTER_R9D:
    case ZYDIS_REGISTER_R9B:
        return R9;

    case ZYDIS_REGISTER_R10:
    case ZYDIS_REGISTER_R10W:
    case ZYDIS_REGISTER_R10D:
    case ZYDIS_REGISTER_R10B:
        return R10;

    case ZYDIS_REGISTER_R11:
    case ZYDIS_REGISTER_R11W:
    case ZYDIS_REGISTER_R11D:
    case ZYDIS_REGISTER_R11B:
        return R11;

    case ZYDIS_REGISTER_R14:
    case ZYDIS_REGISTER_R14W:
    case ZYDIS_REGISTER_R14D:
    case ZYDIS_REGISTER_R14B:
        return R14;
    case ZYDIS_REGISTER_RBP:
    case ZYDIS_REGISTER_EBP:
    case ZYDIS_REGISTER_BP:
    case ZYDIS_REGISTER_BPL:
        return RBP;
    case ZYDIS_REGISTER_RSP:
    case ZYDIS_REGISTER_ESP:
    case ZYDIS_REGISTER_SP:
    case ZYDIS_REGISTER_SPL:
        return RSP;
    case ZYDIS_REGISTER_RIP:

        return RIP;
    case ZYDIS_REGISTER_RFLAGS:
        return FLAGS;
    case ZYDIS_REGISTER_XMM0:
    case ZYDIS_REGISTER_XMM1:
    case ZYDIS_REGISTER_XMM2:
    case ZYDIS_REGISTER_XMM3:
    case ZYDIS_REGISTER_XMM4:
    case ZYDIS_REGISTER_XMM5:
    case ZYDIS_REGISTER_XMM6:
    case ZYDIS_REGISTER_XMM7:
    case ZYDIS_REGISTER_XMM8:
    case ZYDIS_REGISTER_XMM9:
    case ZYDIS_REGISTER_XMM10:
    case ZYDIS_REGISTER_XMM11:
    case ZYDIS_REGISTER_XMM12:
    case ZYDIS_REGISTER_XMM13:
    case ZYDIS_REGISTER_XMM14:
    case ZYDIS_REGISTER_XMM15:
    case ZYDIS_REGISTER_YMM0:
    case ZYDIS_REGISTER_YMM1:
    case ZYDIS_REGISTER_YMM2:
    case ZYDIS_REGISTER_YMM3:
    case ZYDIS_REGISTER_YMM4:
    case ZYDIS_REGISTER_YMM5:
    case ZYDIS_REGISTER_YMM6:
    case ZYDIS_REGISTER_YMM7:
    case ZYDIS_REGISTER_YMM8:
    case ZYDIS_REGISTER_YMM9:
    case ZYDIS_REGISTER_YMM10:
    case ZYDIS_REGISTER_YMM11:
    case ZYDIS_REGISTER_YMM12:
    case ZYDIS_REGISTER_YMM13:
    case ZYDIS_REGISTER_YMM14:
    case ZYDIS_REGISTER_YMM15:
    case ZYDIS_REGISTER_NONE:
        return IGNORE;
    default: {

        TRACER_PRINT_ERROR("Found unhandled register in get_offset_of_reg, reg "
                           "index: %i, reg name: %s",
                           reg, ZydisRegisterGetString(reg));
        return 0;
        break;
    }
    }
}

used_regs get_used_xmm_registers(ZydisDisassembledInstruction *instruction) {
    used_regs used = 0;
    TRACER_PRINT_DEBUG_REGISTERS(
        "Getting used xmm registers of the following instruction: %s",
        instruction->text);

    for (int i = 0; i < instruction->info.operand_count; i++) {
        ZydisDecodedOperand op = instruction->operands[i];
        if (op.type == ZYDIS_OPERAND_TYPE_REGISTER &&
            !(op.actions &
              (ZYDIS_OPERAND_ACTION_READ | ZYDIS_OPERAND_ACTION_CONDREAD))) {
            // operand is not of the desired kind, so we dont have to restore it
            continue;
        }
        if (op.type == ZYDIS_OPERAND_TYPE_REGISTER) {
            used |= get_offset_of_xmm_reg(op.reg.value);
        }
        if (op.type == ZYDIS_OPERAND_TYPE_MEMORY) {
            if (op.mem.type == ZYDIS_MEMOP_TYPE_MIB) {
                used |= get_offset_of_xmm_reg(op.mem.base);
                continue;
            }
            if (op.mem.type == ZYDIS_MEMOP_TYPE_MEM || op.mem.type == ZYDIS_MEMOP_TYPE_AGEN) {
                used |= get_offset_of_xmm_reg(op.mem.base);
                if (op.mem.index != ZYDIS_REGISTER_NONE) {

                    used |= get_offset_of_xmm_reg(op.mem.index);
                }
                continue;
            }
            TRACER_PRINT_ERROR("Found unhandled memory type a: %i",
                               op.mem.type);
        }
    }
    TRACER_PRINT_DEBUG_REGISTERS("Got the following used registers:");
    print_registers(used);
    return used;
}
used_regs get_actual_used_registers(ZydisDisassembledInstruction *instruction) {
    used_regs used = 0;
    TRACER_PRINT_DEBUG_REGISTERS(
        "Getting actual used registers of the following instruction: %s",
        instruction->text);

    for (int i = 0; i < instruction->info.operand_count; i++) {
        ZydisDecodedOperand op = instruction->operands[i];
        if (op.type == ZYDIS_OPERAND_TYPE_REGISTER &&
            !(op.actions &
              (ZYDIS_OPERAND_ACTION_WRITE | ZYDIS_OPERAND_ACTION_CONDWRITE))) {
            // operand is not of the desired kind, so we dont have to restore it
            continue;
        }
        if (op.type == ZYDIS_OPERAND_TYPE_REGISTER &&
            op.reg.value != ZYDIS_REGISTER_RFLAGS) {
            int offset = get_offset_of_reg(op.reg.value);
            if (offset < IGNORE) {
                used |= offset;
            }
        }
    }
    TRACER_PRINT_DEBUG_REGISTERS("Got the following actual used registers:");
    print_registers(used);
    return used;
}
used_regs get_used_registers(ZydisDisassembledInstruction *instruction) {
    used_regs used = 0;
    TRACER_PRINT_DEBUG_REGISTERS(
        "Getting used registers of the following instruction: %s",
        instruction->text);

    for (int i = 0; i < instruction->info.operand_count; i++) {
        ZydisDecodedOperand op = instruction->operands[i];
        if (op.type == ZYDIS_OPERAND_TYPE_REGISTER &&
            !(op.actions &
              (ZYDIS_OPERAND_ACTION_READ | ZYDIS_OPERAND_ACTION_CONDREAD))) {
            // operand is not of the desired kind, so we dont have to restore it
            continue;
        }
        if (op.type == ZYDIS_OPERAND_TYPE_REGISTER) {
            int offset = get_offset_of_reg(op.reg.value);
            if (offset < IGNORE) {
                used |= offset;
            }
        }
        if (op.type == ZYDIS_OPERAND_TYPE_MEMORY) {
            if (op.mem.type == ZYDIS_MEMOP_TYPE_MIB) {
                int offset = get_offset_of_reg(op.mem.base);
                if (offset < IGNORE) {
                    used |= offset;
                }
            }
            // the latter thing is lea rax, [rsp + x], see DecoderTypes.h in
            // zydis
            if (op.mem.type == ZYDIS_MEMOP_TYPE_MEM ||
                op.mem.type == ZYDIS_MEMOP_TYPE_AGEN) {
                int offset = get_offset_of_reg(op.mem.base);
                if (offset < IGNORE) {
                    used |= offset;
                }
                if (op.mem.index != ZYDIS_REGISTER_NONE) {
                    int offset = get_offset_of_reg(op.mem.index);
                    if (offset < IGNORE) {
                        used |= offset;
                    }
                }

            } else {
                TRACER_PRINT_ERROR("Found unhandled memory type: %i",
                                   op.mem.type);
            }
        }
    }
    TRACER_PRINT_DEBUG_REGISTERS("Got the following used registers:");
    print_registers(used);
    return used;
}
used_regs
get_modified_xmm_registers(ZydisDisassembledInstruction *instruction) {
    used_regs regs = 0;
    for (int i = 0; i < instruction->info.operand_count; i++) {
        ZydisDecodedOperand op = instruction->operands[i];
        if (op.type != ZYDIS_OPERAND_TYPE_REGISTER) {
            continue;
        }
        if (!(op.actions &
              (ZYDIS_OPERAND_ACTION_WRITE | ZYDIS_OPERAND_ACTION_CONDWRITE))) {
            // registers is not written, so we dont have to save it after
            // execution
            continue;
        }
        regs |= get_offset_of_xmm_reg(op.reg.value);
    }
    TRACER_PRINT_DEBUG_REGISTERS("Got the following modified registers:");
    print_registers(regs);
    return regs;
};
used_regs get_modified_registers(ZydisDisassembledInstruction *instruction) {
    used_regs regs = 0;
    if (instruction->info.mnemonic == ZYDIS_MNEMONIC_RET) {
        return regs;
    }
    for (int i = 0; i < instruction->info.operand_count; i++) {
        ZydisDecodedOperand op = instruction->operands[i];
        if (op.type != ZYDIS_OPERAND_TYPE_REGISTER) {
            continue;
        }
        if (!(op.actions &
              (ZYDIS_OPERAND_ACTION_WRITE | ZYDIS_OPERAND_ACTION_CONDWRITE))) {
            // registers is not written, so we dont have to save it after
            // execution
            continue;
        }
        int offset = get_offset_of_reg(op.reg.value);
        if (offset < IGNORE) {
            regs |= offset;
        }
    }
    TRACER_PRINT_DEBUG_REGISTERS("Got the following modified registers:");
    print_registers(regs);
    return regs;
};

// Get zero based index
int tracer_reg_to_index(TracerRegister reg) {
    int res = 0;
    TracerRegister tmp = reg;
    while (tmp != 0x01) {
        tmp = tmp >> 1;
        res += 1;
    }
    return res;
}
TracerRegister get_and_set_unused_register(used_regs *used) {
    TRACER_PRINT_DEBUG_COLLECTOR(
        "Getting new unused register, previously used are 0x%x", *used);
    used_regs u = *used;
    if (!(u & R15)) {
        *used = u | R15;
        return R15;
    }
    if (!(u & R14)) {
        *used = u | R14;
        return R14;
    }
    if (!(u & R13)) {
        *used = u | R13;
        return R13;
    }
    if (!(u & R12)) {
        *used = u | R12;
        return R12;
    }
    if (!(u & R11)) {
        *used = u | R11;
        return R11;
    }
    if (!(u & R10)) {
        *used = u | R10;
        return R10;
    }
    if (!(u & R9)) {
        *used = u | R9;
        return R9;
    }
    if (!(u & R8)) {
        *used = u | R8;
        return R8;
    }
    if (!(u & R8)) {
        *used = u | R8;
        return R8;
    }
    if (!(u & RDI)) {
        *used = u | RDI;
        return RDI;
    }
    if (!(u & RSI)) {
        *used = u | RSI;
        return RSI;
    }
    if (!(u & RBP)) {
        *used = u | RBP;
        return RBP;
    }
    if (!(u & RSP)) {
        *used = u | RSP;
        return RSP;
    }
    if (!(u & RBX)) {
        *used = u | RBX;
        return RBX;
    }
    if (!(u & RDX)) {
        *used = u | RDX;
        return RDX;
    }
    if (!(u & RCX)) {
        *used = u | RCX;
        return RCX;
    }
    if (!(u & RAX)) {
        *used = u | RAX;
        return RAX;
    }
    return -1;
}

void print_registers(used_regs regs) {
#ifdef TRACER_LOG_DEBUG_REGISTERS
    TRACER_PRINT_DEBUG("The registers 0x%x are: ", regs);
    if (regs & RAX) {
        TRACER_PRINT_DEBUG("RAX ");
    }
    if (regs & RCX) {
        TRACER_PRINT_DEBUG("RCX ");
    }
    if (regs & RDX) {
        TRACER_PRINT_DEBUG("RDX ");
    }
    if (regs & RBX) {
        TRACER_PRINT_DEBUG("RBX ");
    }
    if (regs & RSP) {
        TRACER_PRINT_DEBUG("RSP ");
    }
    if (regs & RBP) {
        TRACER_PRINT_DEBUG("RBP ");
    }
    if (regs & RSI) {
        TRACER_PRINT_DEBUG("RSI ");
    }
    if (regs & RDI) {
        TRACER_PRINT_DEBUG("RDI ");
    }
    if (regs & R8) {
        TRACER_PRINT_DEBUG("R8 ");
    }
    if (regs & R9) {
        TRACER_PRINT_DEBUG("R9 ");
    }
    if (regs & R10) {
        TRACER_PRINT_DEBUG("R10 ");
    }
    if (regs & R11) {
        TRACER_PRINT_DEBUG("R11 ");
    }
    if (regs & R12) {
        TRACER_PRINT_DEBUG("R12 ");
    }
    if (regs & R13) {
        TRACER_PRINT_DEBUG("R13 ");
    }
    if (regs & R14) {
        TRACER_PRINT_DEBUG("R14 ");
    }
    if (regs & R15) {
        TRACER_PRINT_DEBUG("R15 ");
    }
    if (regs & FLAGS) {
        TRACER_PRINT_DEBUG("FLAGS ");
    }
    if (regs & RIP) {
        TRACER_PRINT_DEBUG("RIP ");
    }
    if (regs & IGNORE) {
        TRACER_PRINT_DEBUG("IGNORE ");
    }

#ifdef TRACER_USERSPACE
    TRACER_PRINT_DEBUG("\n");
#endif
#endif
}

int tracer_reg_to_specific_reg_index(TracerRegister reg) {
    if (reg & R8) {
        return TRACER_REG_R8;
    }
    if (reg & R9) {
        return TRACER_REG_R9;
    }
    if (reg & R10) {
        return TRACER_REG_R10;
    }
    if (reg & R11) {
        return TRACER_REG_R11;
    }
    if (reg & R12) {
        return TRACER_REG_R12;
    }
    if (reg & R13) {
        return TRACER_REG_R13;
    }
    if (reg & R14) {
        return TRACER_REG_R14;
    }
    if (reg & R15) {
        return TRACER_REG_R15;
    }
    if (reg & RDI) {
        return TRACER_REG_RDI;
    }
    if (reg & RSI) {
        return TRACER_REG_RSI;
    }
    if (reg & RBP) {
        return TRACER_REG_RBP;
    }
    if (reg & RBX) {
        return TRACER_REG_RBX;
    }
    if (reg & RDX) {
        return TRACER_REG_RDX;
    }
    if (reg & RAX) {
        return TRACER_REG_RAX;
    }
    if (reg & RCX) {
        return TRACER_REG_RCX;
    }
    if (reg & RSP) {
        return TRACER_REG_RSP;
    }
    if (reg & RIP) {
        return TRACER_REG_RIP_DO_NOT_USE;
    }
    TRACER_PRINT_ERROR(
        "Tried to convert a unknown register to specific register index");
    return TRACER_REG_RIP_DO_NOT_USE;
};
