#pragma once
#include "collector.h"
#include "register.h"
#ifdef TRACER_USERSPACE
#include <Zydis/Disassembler.h>
#endif
typedef struct {
    TracerRegister reg_for_rsp;
	TracerRegister temp_rsp;

} ContextInfo;
int patch_create_context(long address, AddressReg valueReg, used_regs used,
                         int include_load, int for_trampoline,
                         used_regs modified, long *store_at_end,
                         ContextInfo *info, used_regs used_registers);

int patch_store_updated_regs(char *current_address, used_regs used_registers,
                             used_regs modified_registers, int for_trampoline,
                             long *store_at_end, ContextInfo *info);
#define OFFSET_FROM_BUFFER_START 32
