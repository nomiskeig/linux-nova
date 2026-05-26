#pragma once

#include "asm/ptrace.h"
#include <asm-generic/siginfo.h>
void trace_access(struct pt_regs *regs);
int protect_pkey_kernel(unsigned long address, long pkey);
void enable_rw_prot(int pkey, int kind);
void disable_rw_prot(int pkey);

void set_pks_bit(int a);
void disable_write_protection(void);
void enable_write_protection(void);
void disable_smap(void);

typedef long long int reg_t;
typedef int reg_index_t;
typedef reg_t *tracer_regs_t;

enum {
	TRACER_REG_R15 = 0,
	TRACER_REG_R14,
	TRACER_REG_R13,
	TRACER_REG_R12,
	TRACER_REG_RBP,
	TRACER_REG_RBX,
	TRACER_REG_R11,
	TRACER_REG_R10,
	TRACER_REG_R9,
	TRACER_REG_R8,
	TRACER_REG_RAX,
	TRACER_REG_RCX,
	TRACER_REG_RDX,
	TRACER_REG_RSI,
	TRACER_REG_RDI,
	UNUSED1,
	TRACER_REG_RIP_DO_NOT_USE,
    UNUSED2,
	TRACER_REG_FLAGS,
	TRACER_REG_RSP
	// TODO: we need the flags register
};

/*
 * The address is where the the buffer is allocated in userspace (for this
 * process). The function determines the pfn of the mapping, allocates memory
 * for the trace buffer in the kernel, and then changes those mappings to point
 * to the same pfn. That lets the kernel write to the same buffer
*/
void tracer_kernel_init(unsigned long trace_buffer_address,
			unsigned long value_buffer_address);
void tracer_kernel_reset(void);
typedef struct {
	tracer_regs_t gregs;
	long __unused2__[23];
	// fpregs is only used in order to have an address that we use to restore the fregs in the kernel
	// this has to be aligned to a 64 byte
	int fpregs[1024 / sizeof(int)];
} __attribute__((aligned(64))) mcontext_t;
typedef struct {
	mcontext_t uc_mcontext;
} ucontext_t;

#define SEGV_PKUERR 4
//void enable_rw_prot(long pkey, int kind);
//void disable_rw_prot(long pkey);
void sigill_handler(int number, siginfo_t *info, void *ucontext);
void sigsegv_handler(int number, siginfo_t *info, void *ucontext);

int tracer_can_handle(long address);
void pku_signal_handler(int number, siginfo_t *info, void *ucontext);
void tracer_core_handler(int number, siginfo_t *info, void *ucontext,
			 int from_invalid);
long get_tracebuffer_address(void);
long get_valuebuffer_address(void);
void reset_buffers(void);

static __inline__ long rdtsc_self(void) {
    unsigned long hi, lo;
    __asm__ __volatile__(
                         "rdtsc"
                         : "=a"(lo), "=d"(hi));
    return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}
