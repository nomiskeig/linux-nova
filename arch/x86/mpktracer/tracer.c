#include "asm/pgtable_types.h"
#include "linux/mm.h"
#include <asm/tlbflush.h>
#include <asm/pgtable.h>
#include <asm/pgtable.h>
#include <asm/tracer.h>
#include "patcher.h"
#include "collector.h"
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "logging.h"

#include <Zydis.h>
#include <linux/highmem.h>

Tracebuffer *tracebuffer;
Valuebuffer *valuebuffer;
Allocator *allocator;
XsaveAreas *xsave_areas;
ThreadMappings *thread_mappings;
extern DisplacedInstructions *displaced_instructions;
extern Allocator *allocator;

ZydisDisassembledInstruction *disassembled_instruction;
ZydisDisassembledInstruction *temp_instructions;
long alternate_stack_address;
long trampoline_stack_base;
long base_patch_address;
long kernel_trace_diff;
int id_offset;
int use_glibc[MAX_SUPPORTED_THREADS];
TraceAddresses *trace_addresses;

/*static void *get_next_trace_address(void)
{
	void *address = tracebuffer->next_trace_address;
	tracebuffer->next_trace_address += sizeof(Trace);
	return address;
}
*/


/*static void log_mappings(struct mm_struct *mm) {
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep;

	pgd = pgd_offset(mm, address);
	if (pgd_none(*pgd) || pgd_bad(*pgd)) {
	//	return NULL;
	}
	p4d = p4d_offset(pgd, address);
	if (p4d_none(*p4d) || p4d_bad(*p4d)) {
	//	return NULL;
	}
	pud = pud_offset(p4d, address);
	if (pud_none(*pud) || pud_bad(*pud)) {
	//	return NULL;
	}
	pmd = pmd_offset(pud, address);

	if (pmd_none(*pmd) || pmd_bad(*pmd)) {
	//	return NULL;
	}

	ptep = pte_offset_kernel(pmd, address);
	if (!ptep) {
		//return NULL;
	}
	//return NULL;

}


}
*/
static pte_t __attribute__((__noinline__)) *
	walk_page_table(struct mm_struct *mm, unsigned long address)
{
	//pr_info("waling the page table");
	// see https://www.kernel.org/doc/gorman/html/understand/understand006.html
	// and
	// https://github.com/davidhcefx/Translate-Virtual-Address-To-Physical-Address-in-Linux-Kernel
	// and
	// https://stackoverflow.com/questions/58743052/getting-error-when-compiling-kernel-for-page-table-walk
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep;

	pgd = pgd_offset(mm, address);
	if (pgd_none(*pgd) || pgd_bad(*pgd)) {
		return NULL;
	}
	p4d = p4d_offset(pgd, address);
	if (p4d_none(*p4d) || p4d_bad(*p4d)) {
		return NULL;
	}
	pud = pud_offset(p4d, address);
	if (pud_none(*pud) || pud_bad(*pud)) {
		return NULL;
	}
	pmd = pmd_offset(pud, address);

	if (pmd_none(*pmd) || pmd_bad(*pmd)) {
		return NULL;
	}

	ptep = pte_offset_kernel(pmd, address);
	if (!ptep) {
		return NULL;
	}

	return ptep;
}
int __attribute__((__noinline__)) protect_pkey_kernel(unsigned long address,
						      long pkey)

{
	TRACER_PRINT_DEBUG("in protect_pkey_kernel");
	pte_t *pte = NULL;
	int count = 1;
	while (pte == NULL) {
		//pr_info("before walking page table");
		count += 1;
		pte = walk_page_table(current->mm, address);
		if (count == 5 && pte == NULL) {
			return -1;
		}
	}
	//pr_info("Old pte: %lx\n", pte->pte);
	//pr_info("Page frame number of address: %lx\n",
	//	page_to_pfn(virt_to_page(address)));
	// pte_t new_pte = _pte(virt_to_page(address),
	//                      {(unsigned long)pkey << _PAGE_BIT_PKEY_BIT0});
	pte_t new_pte = { .pte = (long)(pte->pte |
					(pkey << _PAGE_BIT_PKEY_BIT0)) };
	//pr_info("new pte: %lx\n", new_pte.pte);
	set_pte(pte, new_pte);
	flush_tlb_all();
	//pte_t *supposed_pte = walk_page_table(current->mm, address);
	//pr_info("should be same pte: %lx\n", supposed_pte->pte);
	return 0;
}

void enable_rw_prot(int pkey, int kind)
{
	// TODO: this does not actually use the pkey
	// Needs to also use the pkey when enabling/disabling the pkey around
	// the stub function
	/*	int err;
	unsigned long old_value = native_read_msr_safe(1761, &err);
	if (err) {
		pr_info("error value of old read: %i", err);
	}
	*/
	//pr_info("old msr value: %lul", old_value);
	//pr_info("got kind %x", kind);
	native_write_msr_safe(1761, kind << 2, 0);
	/*unsigned long new_value = native_read_msr_safe(1761, &err);
	if (err) {
		pr_info("error value of new read: %i", err);
	}
//	pr_info("new msr value: %lul", new_value);
	//	*/
}

void disable_rw_prot(int pkey)
{
	/*
	int err;
	unsigned long old_value = native_read_msr_safe(1761, &err);
	if (err) {
		pr_info("error value of old read: %i", err);
	}

	pr_info("old msr value: %lul", old_value);
	*/
	native_write_msr_safe(1761, 0, 0);
	/*unsigned long new_value = native_read_msr_safe(1761, &err);
	if (err) {
		pr_info("error value of new read: %i", err);
	}
	pr_info("new msr value: %lul", new_value);
	*/
}
void set_pks_bit(int a)
{
	//pr_info("setting pks bit\n");
	u32 cr4;
	__asm__ __volatile__("mov %%cr4, %%rax\n\t"
			     "mov %%eax, %0\n\t"
			     : "=m"(cr4)
			     : /* no input */
			     : "%rax");
	//pr_info("cr4 before: 0x: %x", cr4);
	//pr_info("should have printed");
	cr4_set_bits(1 << 24);
	__asm__ __volatile__("mov %%cr4, %%rax\n\t"
			     "mov %%eax, %0\n\t"
			     : "=m"(cr4)
			     : /* no input */
			     : "%rax");
	//pr_info("cr4 after: 0x: %x", cr4);
// see  https://patchwork.kernel.org/project/linux-kselftest/patch/20201022222701.887660-4-ira.weiny@intel.com/
	//if (!cpu_feature_enabled(16*32 + 31))  {
	//   pr_info("pks feature not available\n");
	//  return;
	// }

	// this is ATT syntax since its in the kernel
	// use r12-r15 should be fine see
	// https://stackoverflow.com/questions/18024672/what-registers-are-preserved-through-a-linux-x86-64-function-call
	//asm("MOV %CR4, %r12");
	//asm("MOV $1, %r13");
	// PKS is the 24th bit
	//asm("SHL $24, %r13");
	//asm("OR %r13, %r12");
	//asm("MOV %r12, %cr4");
}

void disable_smap(void)  {
	u32 cr4;
	__asm__ __volatile__("mov %%cr4, %%rax\n\t"
			     "mov %%eax, %0\n\t"
			     : "=m"(cr4)
			     :
			     : "rax");
	//pr_info("cr0 before: 0x: %x", cr0);
	cr4 = cr4 & ~(1 << 21);
	__asm__ __volatile("mov %0, %%eax\n\t"
			   "mov %%rax, %%cr4\n\t"
			   :
			   : "m"(cr4)
			   : "rax");

}
void disable_write_protection(void)
{
	// TODO: this currently clears the cr0.wp bit, in a optimal world this
	// is not necessary, it is enough to just change the access bits on the
	// corresponging ptes, but i am not really sure what those are and i
	// wanted to get this working
	//unsigned long first_page = (unsigned long)(stub_function) -((long long)stub_function % page_size);
	//pte_t *pte = walk_page_table(current->mm, first_page);
	u32 cr0;
	__asm__ __volatile__("mov %%cr0, %%rax\n\t"
			     "mov %%eax, %0\n\t"
			     : "=m"(cr0)
			     :
			     : "rax");
	//pr_info("cr0 before: 0x: %x", cr0);
	cr0 = cr0 & ~(1 << 16);
	__asm__ __volatile("mov %0, %%eax\n\t"
			   "mov %%rax, %%cr0\n\t"
			   :
			   : "m"(cr0)
			   : "rax");
}
void enable_write_protection(void)
{
	// TODO: this currently clears the cr0.wp bit, in a optimal world this
	// is not necessary, it is enough to just change the access bits on the
	// corresponging ptes, but i am not really sure what those are and i
	// wanted to get this working
	//unsigned long first_page = (unsigned long)(stub_function) -((long long)stub_function % page_size);
	//pte_t *pte = walk_page_table(current->mm, first_page);
	u32 cr0;
	__asm__ __volatile__("mov %%cr0, %%rax\n\t"
			     "mov %%eax, %0\n\t"
			     : "=m"(cr0)
			     :
			     : "rax");
	//pr_info("cr0 before: 0x: %x", cr0);
	cr0 = cr0 | (1 << 16);
	__asm__ __volatile("mov %0, %%eax\n\t"
			   "mov %%rax, %%cr0\n\t"
			   :
			   : "m"(cr0)
			   : "rax");
}
void tracer_kernel_reset(void) {
	reset_buffers();
	vfree(displaced_instructions);
	displaced_instructions =
		(DisplacedInstructions *)vzalloc(sizeof(DisplacedInstructions));
	vfree(allocator);
	allocator = (Allocator *)vzalloc(sizeof(Allocator));

}

void tracer_kernel_init(unsigned long trace_buffer_address,
			unsigned long value_buffer_address)
{
	TRACER_PRINT_INFO(
		"Mapping the buffer into the kernel, tracer_buffer_address is %lx and value_buffer_address is %lx",
		(long)trace_buffer_address, (long)value_buffer_address);
	/*
	TRACER_PRINT_INFO("Size of long is %lu", sizeof(long));
	// Get the pfn of the mapping
	//flush_tlb_one_kernel((unsigned long)address);
	pte_t *user_buffer_pte = walk_page_table(current->mm, address);
	TRACER_PRINT_DEBUG_KERNEL_TRACER("pte of userspace: %lx",
					 user_buffer_pte->pte);

	tracebuffer = (Tracebuffer *)kzalloc(sizeof(Tracebuffer), GFP_KERNEL);
	// write to buffer, maybe this triggers a mapping, i am not sure
	tracebuffer->next_trace_address = 0;
	// disable cache for the page
	// TODO: if this spans multiple pages, it breaks
	pte_t new_pte = { .pte = (long)(user_buffer_pte->pte |
					(1 << _PAGE_BIT_PCD)) };
	pte_t *kernel_buffer_pte =
		walk_page_table(current->mm, (unsigned long)tracebuffer);
	set_pte(kernel_buffer_pte, new_pte);
	//flush_tlb_one_kernel((unsigned long)tracebuffer);

	flush_tlb_all();
	*/
	int amount_pages_trace = (sizeof(Tracebuffer) / PAGE_SIZE) + 1;
	int amount_pages_value = (sizeof(Valuebuffer) / PAGE_SIZE) + 1;
	struct page **page_pointer_tracebuffer =
		vzalloc(amount_pages_trace * sizeof(struct page *));
	struct page **page_pointer_valuebuffer =
		vzalloc(amount_pages_value * sizeof(struct page *));
	get_user_pages_unlocked(trace_buffer_address, amount_pages_trace,
				page_pointer_tracebuffer, 0);
	for (int i = 0; i < amount_pages_trace; i++) {
		if (page_pointer_tracebuffer[i] == NULL) {
			pr_err("Page %i is zero pointer", i);
		}
	}
	get_user_pages_unlocked(value_buffer_address, amount_pages_value,
				page_pointer_valuebuffer, 0);
	for (int i = 0; i < amount_pages_value; i++) {
		if (page_pointer_valuebuffer[i] == NULL) {
			pr_err("Valuebuffer Page %i is zero pointer", i);
		}
	}
	flush_tlb_all();
	tracebuffer = vmap(page_pointer_tracebuffer, amount_pages_trace,
			   VM_READ | VM_WRITE | VM_READ, PAGE_KERNEL);
	if (tracebuffer == NULL) {
		pr_err("could not map the tracebuffer");
	}
	valuebuffer = vmap(page_pointer_valuebuffer, amount_pages_value,
			   VM_READ | VM_WRITE, PAGE_KERNEL);
	if (valuebuffer == NULL) {
		pr_err("could not map the valuebuffer");
	}
	// disable the IA32_EFER.NXE bit to allow exectuion of the trampolines
	/*
	int err;
	unsigned long old_value = native_read_msr_safe(0xC0000080, &err);
	if (err) {
		pr_info("error value of old IA32_EFER read: %i", err);
	}
	unsigned long new_value = old_value & ~(1 << 11);
	native_write_msr_safe(61, new_value, new_value >> 32);
	//native_write_msr_safe(0xC0000080, new_value, new_value >> 32);

	if (err) {
		pr_info("error second jk value of old IA32_EFER read: %i", err);
	}
	pr_info("value_after_write: %lx", value_after_write);
	*/

	/*tracebuffer = kmap(page_pointer_tracebuffer[0]);
	valuebuffer = kmap(page_pointer_valuebuffer[0]);
	for (int i = 1; i < amount_pages_trace; i++) {
		kmap(page_pointer_tracebuffer[i]);
	}
	for (int i = 1; i < amount_pages_value; i++) {
		kmap(page_pointer_valuebuffer[i]);
	}
	*/
	TRACER_PRINT_DEBUG(
		"Base patch address is %px, valuebuffer is %px, tracebuffer is %px",
		(void *)base_patch_address, (void *)valuebuffer,
		(void *)tracebuffer);

	tracebuffer->next_trace_address = (void *)&tracebuffer->traces[0];
	tracebuffer->amount = 0;
	valuebuffer->next_offset = 0;
#ifdef TRACER_LOG_DEBUG_STUB_FUNCTION
	disassembled_instruction = (ZydisDisassembledInstruction *)kzalloc(
		sizeof(ZydisDisassembledInstruction), GFP_KERNEL);
#endif
	xsave_areas = (XsaveAreas *)kmalloc(sizeof(XsaveAreas), GFP_KERNEL);
#ifdef TRACER_USE_POST_HANDLER
	trace_addresses =
		(TraceAddresses *)kmalloc(sizeof(TraceAddresses), GFP_KERNEL);
#endif
	alternate_stack_address = (long)vzalloc(PAGE_SIZE * 4);
	pr_info("Alternate stack address is %lx", alternate_stack_address);
	if (alternate_stack_address == 0) {
		pr_err("Could not allocate altnerative stack address");
	}

	thread_mappings =
		(ThreadMappings *)kzalloc(sizeof(ThreadMappings), GFP_KERNEL);
	displaced_instructions =
		(DisplacedInstructions *)vzalloc(sizeof(DisplacedInstructions));
	allocator = (Allocator *)vzalloc(sizeof(Allocator));
	//base_patch_address = (long)kzalloc(4096, GFP_KERNEL);
	//base_patch_address = (long)vmalloc(4096);
	// see https://stackoverflow.com/questions/74741083/how-to-allocate-executable-memory-in-linux
	base_patch_address = (unsigned long)__vmalloc_node_range(
		4096, 1, VMALLOC_START, VMALLOC_END, GFP_KERNEL,
		PAGE_KERNEL_EXEC, 0, NUMA_NO_NODE, __builtin_return_address(0));

	temp_instructions = (ZydisDisassembledInstruction *)kzalloc(
		sizeof(ZydisDisassembledInstruction) * 5, GFP_KERNEL);
	trampoline_stack_base = (long)kmalloc(PAGE_SIZE * 4, GFP_KERNEL);
	kernel_trace_diff = (long)&tracebuffer->traces[0] - (long)(&(((Tracebuffer*)trace_buffer_address)->traces[0]));
	TRACER_PRINT_DEBUG("Done mapping the buffers into the kernel, diff is %lx", kernel_trace_diff);
	TRACER_PRINT_DEBUG(
		"Base patch address is %px, valuebuffer is %px, tracebuffer is %px",
		(void *)base_patch_address, (void *)valuebuffer,
		(void *)tracebuffer);
	enable_write_protection();
}

long get_tracebuffer_address(void)
{
	return (long)tracebuffer;
}
long get_valuebuffer_address(void)
{
	return (long)valuebuffer;
}
void reset_buffers(void)
{
	TRACER_PRINT_DEBUG("Resetting buffers");
	valuebuffer->next_offset = 0;
	tracebuffer->amount = 0;
	tracebuffer->next_trace_address = (void *)&tracebuffer->traces[0];
}

EXPORT_SYMBOL(tracer_kernel_init);
EXPORT_SYMBOL(tracer_kernel_reset);
EXPORT_SYMBOL(disable_write_protection);
EXPORT_SYMBOL(enable_rw_prot);
EXPORT_SYMBOL(disable_rw_prot);
EXPORT_SYMBOL(protect_pkey_kernel);
EXPORT_SYMBOL(set_pks_bit);
EXPORT_SYMBOL(get_tracebuffer_address);
EXPORT_SYMBOL(get_valuebuffer_address);
EXPORT_SYMBOL(reset_buffers);

/*
static void log_stub_function(long rip) {
	// we just use zydis to decompile the instructions starting at the
	// address of the stub function
	ZydisDisassembledInstruction instr;
	ZyanUSize offset = 0;
	while (ZYAN_SUCCESS(ZydisDisassembleIntel(
		 ZYDIS_MACHINE_MODE_LONG_64,
		 (unsigned long)rip,
		 &stub_function + offset,
		100 - offset,
		 &instr))) {
		char bytes[15];
		memcpy(&bytes, stub_function + offset, instr.info.length);
		pr_info("Instruction: %s, Bytes: ",instr.text);
		offset += instr.info.length;
		for (int i = 0; i < instr.info.length; i++) {
			pr_info("%02hhx ", bytes[i]);
		}
		pr_info("\n");
	}

void trace_access(struct pt_regs *regs)
{
	TRACER_PRINT_INFO("Tracing access at %lx in kernel", regs->ip);

	ZydisDisassembledInstruction instruction;
	ZyanStatus status = ZydisDisassembleIntel(ZYDIS_MACHINE_MODE_LONG_64,
						  regs->ip, (void *)regs->ip,
						  15, &instruction);
	if (!ZYAN_SUCCESS(status)) {
		TRACER_EXIT("Could not decode the instruction");
	};
	TRACER_PRINT_INFO("The instruction is %s", instruction.text);

	tracer_regs_t tracer_regs = (tracer_regs_t)regs;
	//TRACER_PRINT_INFO(
	//	"ppn of the tracebuffer: %lx",
	//	walk_page_table(current->mm, (unsigned long)tracebuffer)->pte);

	Trace *next_address = get_next_trace_address();
	TRACER_PRINT_INFO("next address: %lx", (unsigned long)next_address);
	//collect_pre(tracer_regs, &instruction, next_address);
	CollectionInfo collec_info;
	patch_collect_instructions((unsigned long)stub_function+ COLLECTOR_OFFSET, &instruction, &collec_info);
	
	patch_instruction(stub_function + INSTRUCTION_OFFSET, regs->ip,
			  &instruction);

	log_stub_function(regs->ip);
	// TODO: use the correct pkey
	disable_rw_prot(-1);
	stub_function(tracer_regs, (long)&next_address->value);
	disable_rw_prot(-1);
	TRACER_PRINT_INFO("Executed stub function in kernel");
	TRACER_PRINT_INFO("data is %lli", tracebuffer->traces[0].value);
	flush_cache_all();

	regs->ip += instruction.info.length;
	return;
}
*/
