
#include "regs.h"
#ifdef TRACER_USERSPACE
#include <Zydis/Disassembler.h>
#else
#include <Zydis.h>
#endif
void collect_post(tracer_regs_t regs, int from_trampoline);
void collect_post_wrapper(tracer_regs_t regs, long address_of_instruction);
void tracing_probe(long expected);
