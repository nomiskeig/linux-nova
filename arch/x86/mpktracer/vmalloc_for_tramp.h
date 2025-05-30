#include "asm/pgtable_types.h"
#include "linux/types.h"


// this function is taken from the orignal vmap_page_range function and is changed in two ways:
// 1) the CX bit is disabled in all levels of the page table hierarchy, since otherwise the nx protection prevents the execution of the page
// 2) if the address is already mapped, it just returns a value != 0, but does not report a bug/crash
int vmap_page_range_for_trampoline(unsigned long address, unsigned long end, phys_addr_t pyhsical_addrss, pgprot_t prot);
