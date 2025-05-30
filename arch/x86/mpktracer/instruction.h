#pragma once

#include "register.h"
#ifdef TRACER_USERSPACE
#include <Zydis/Disassembler.h>
#else
#include <Zydis.h>
#endif
int is_write(ZydisDisassembledInstruction *instruction);
long get_value_size(ZydisDisassembledInstruction *instruction);
int needs_value_copy(ZydisDisassembledInstruction *instruction);
int needs_allignment(ZydisDisassembledInstruction *instruction);
typedef enum {
    TRACER_JUMP_NONE,
    TRACER_JUMP_UNCOND,
    TRACER_JUMP_COND,
    TRACER_JUMP_CALL,
    TRACER_JUMP_RET

} JumpType;
JumpType get_jump_type(ZydisDisassembledInstruction *instruction);

ZydisDecodedOperand *
get_memory_opearand(ZydisDisassembledInstruction *instruction);
ZydisDecodedOperand *
get_register_opearand(ZydisDisassembledInstruction *instruction);
