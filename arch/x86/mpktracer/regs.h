#pragma once

#include "config.h"
#ifdef TRACER_USERSPACE
typedef long long int reg_t;
typedef int reg_index_t;
typedef reg_t *tracer_regs_t;
// needs to match uc_mcontext.gregs
enum {
    TRACER_REG_R8 = 0,
    TRACER_REG_R9,
    TRACER_REG_R10,
    TRACER_REG_R11,
    TRACER_REG_R12,
    TRACER_REG_R13,
    TRACER_REG_R14,
    TRACER_REG_R15,
    TRACER_REG_RDI,
    TRACER_REG_RSI,
    TRACER_REG_RBP,
    TRACER_REG_RBX,
    TRACER_REG_RDX,
    TRACER_REG_RAX,
    TRACER_REG_RCX,
    TRACER_REG_RSP,
    TRACER_REG_RIP_DO_NOT_USE,
    TRACER_REG_FLAGS,
};
#else
#include <asm/tracer.h>
// needs to match pt_regs in ptrace.h

#endif
