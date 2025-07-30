#pragma once
#include "shared.h"

// returns the address at which the value should be stored. This could be done
// in an extra function, but doing it this way makes the trampoline 12 bytes
// shorter :)
#include "regs.h"
#ifdef TRACER_USERSPACE
#include <Zydis/Disassembler.h>
#else
#include <Zydis.h>
#endif

// returns the address at which the value should be stored. This could be done
// in an extra function, but doing it this way makes the trampoline 12 bytes
// shorter :)
long collect_pre(tracer_regs_t regs, ZydisDisassembledInstruction *instruction, int from_trampoline, long rip_of_instruction);
#ifndef TRACER_ENCODE_INDEX_ON_TRAMPOLINE
long collect_pre_wrapper(tracer_regs_t regs, long address_of_instruction, long original_address);
#else
long collect_pre_wrapper(tracer_regs_t regs, long address_of_instruction, long original_address, int displaced_loc_index);
#endif

void collect_address(tracer_regs_t regs,
                            ZydisDisassembledInstruction *instruction,
                            Trace *trace);

Trace* get_next_trace(void);

