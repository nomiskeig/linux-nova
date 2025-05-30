#pragma once
#include "collector.h"
#include "regs.h"
#include "trampoline.h"
#ifdef TRACER_USERSPACE
#include <Zydis/Zydis.h>
#else
#include <Zydis.h>
#endif
// use objdump -d signal_handler.o to figure out the offset
// should not change as long as the offset does not change
// we skip the first byte since for some reason i cannot patch, may have
// to do with alignment of something, i do not really know
//

#ifdef TRACER_USERSPACE
#define MAX_BUFFER_SIZE getpagesize()
#define CONTEXT_RESTORE_OFFSET 36
// the offset at which the collector places its instruction(s) to collect the
// value
#define COLLECTOR_OFFSET CONTEXT_RESTORE_OFFSET + 25
// #define COLLECTOR_OFFSET 170
//  the offset at which the instruction that moves the address of the value into
//  r15
// #define VALUE_ADDRESS_OFFSET 30
//  the offset at which die displaced instruction is placed
#define INSTRUCTION_OFFSET COLLECTOR_OFFSET + 30
#else
#define COLLECTOR_OFFSET 34
#define INSTRUCTION_OFFSET COLLECTOR_OFFSET + 16
#endif
int patch_execute_single_instruction(char *address_to_patch,
                                     char *address_of_instruction,
                                     ZydisDisassembledInstruction *instruction,
                                     long rip,
                                     long *address_of_patched_instruction);

void patch_clear_bytes(char *address, int length);
void stub_function(tracer_regs_t regs, long address, long xrstor_address);
int patch_mov_absolute_offset_to_reg(ZydisDisassembledInstruction *instruction,
                                     TracerRegister reg,
                                     char *address_to_patch);
int patch_nop(char *address);
int patch_fun_start(char *address, long storage_at_beginning);
int patch_xrstor(char *address);
int patch_xsave(char *address);
int patch_fun_end(char *address);
int patch_cond_jump_for_handler(ProbeSite *probe_site, char *address,
                                long storage_at_beginning);
int patch_modified_jump(ZydisDisassembledInstruction *instruction,
                        ProbeSite *probe_site, char *address, int offset,
                        int how_many_to_jump);
int patch_adjust_rsp_down(char *address);
int patch_adjust_rsp_up(char *address);
