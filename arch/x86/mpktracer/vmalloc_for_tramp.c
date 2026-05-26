/*** Page table manipulation functions ***/
#include "asm/pgtable_64_types.h"
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include "asm/pgtable_types.h"
#include "linux/pgtable.h"
#include <linux/vmalloc.h>


#include "../../mm/pgalloc-track.h"
#include "vmalloc_for_tramp.h"
#include "../../mm/internal.h"

#define UNSET_XD_BIT(X) (X & ~(1l << 63))
#ifdef CONFIG_HAVE_ARCH_HUGE_VMAP
static unsigned int __ro_after_init ioremap_max_page_shift = BITS_PER_LONG - 1;
#define TRACER_PRINT_DEBUG_TRAMPOLINES(x, y) 
static int __init set_nohugeiomap(char *str)
{
	ioremap_max_page_shift = PAGE_SHIFT;
	return 0;
}
early_param("nohugeiomap", set_nohugeiomap);
#else /* CONFIG_HAVE_ARCH_HUGE_VMAP */
static const unsigned int ioremap_max_page_shift = PAGE_SHIFT;
#endif	/* CONFIG_HAVE_ARCH_HUGE_VMAP */
static int vmap_pte_range_for_trampoline(pmd_t *pmd, unsigned long addr, unsigned long end,
			phys_addr_t phys_addr, pgprot_t prot,
			unsigned int max_page_shift, pgtbl_mod_mask *mask)
{
	pte_t *pte;
	u64 pfn;
	unsigned long size = PAGE_SIZE;

	pfn = phys_addr >> PAGE_SHIFT;
	pte = pte_alloc_kernel_track(pmd, addr, mask);
	if (!pte) {
	//TRACER_PRINT_DEBUG_TRAMPOLINES("pte error");
		return -ENOMEM;
	}
	do {
		if (unlikely(!pte_none(ptep_get(pte)))) {
			if (pfn_valid(pfn)) {
		//		TRACER_PRINT_DEBUG_TRAMPOLINES("returning -1");
				return -1;
			}
		}

#ifdef CONFIG_HUGETLB_PAGE
		size = PAGE_SIZE;
#endif
		//pr_info("setting pte to %lxu", pfn_pte(pfn, prot).pte);

		pgprot_t prot_new = {.pgprot = UNSET_XD_BIT(prot.pgprot)};
		set_pte_at(&init_mm, addr, pte, pfn_pte(pfn, prot_new));
		pfn++;
	} while (pte += PFN_DOWN(size), addr += size, addr != end);
	*mask |= PGTBL_PTE_MODIFIED;
	return 0;
}

static int vmap_try_huge_pmd_for_trampoline(pmd_t *pmd, unsigned long addr, unsigned long end,
			phys_addr_t phys_addr, pgprot_t prot,
			unsigned int max_page_shift)
{
	if (max_page_shift < PMD_SHIFT)
		return 0;

	if (!arch_vmap_pmd_supported(prot))
		return 0;

	if ((end - addr) != PMD_SIZE)
		return 0;

	if (!IS_ALIGNED(addr, PMD_SIZE))
		return 0;

	if (!IS_ALIGNED(phys_addr, PMD_SIZE))
		return 0;

	if (pmd_present(*pmd) && !pmd_free_pte_page(pmd, addr))
		return 0;

	return pmd_set_huge(pmd, phys_addr, prot);
}

static int vmap_pmd_range_for_trampoline(pud_t *pud, unsigned long addr, unsigned long end,
			phys_addr_t phys_addr, pgprot_t prot,
			unsigned int max_page_shift, pgtbl_mod_mask *mask)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_alloc_track(&init_mm, pud, addr, mask);

	if (!pmd)  {

	//TRACER_PRINT_DEBUG_TRAMPOLINES("pmd error");
		return -ENOMEM;
	}
	do {
		//printk("old pmd entry: %lux", pmd->pmd);
		pmd->pmd = UNSET_XD_BIT(pmd->pmd);
		next = pmd_addr_end(addr, end);

		if (vmap_try_huge_pmd_for_trampoline(pmd, addr, next, phys_addr, prot,
					max_page_shift)) {
			*mask |= PGTBL_PMD_MODIFIED;
			continue;
		}

		if (vmap_pte_range_for_trampoline(pmd, addr, next, phys_addr, prot, max_page_shift, mask)) {

			//TRACER_PRINT_DEBUG_TRAMPOLINES("error in innre vmap_pmd_range_for_trampolines");
			return -ENOMEM;
		}
	} while (pmd++, phys_addr += (next - addr), addr = next, addr != end);
	return 0;
}

static int vmap_try_huge_pud_for_trampoline(pud_t *pud, unsigned long addr, unsigned long end,
			phys_addr_t phys_addr, pgprot_t prot,
			unsigned int max_page_shift)
{
	if (max_page_shift < PUD_SHIFT)
		return 0;

	if (!arch_vmap_pud_supported(prot))
		return 0;

	if ((end - addr) != PUD_SIZE)
		return 0;

	if (!IS_ALIGNED(addr, PUD_SIZE))
		return 0;

	if (!IS_ALIGNED(phys_addr, PUD_SIZE))
		return 0;

	if (pud_present(*pud) && !pud_free_pmd_page(pud, addr))
		return 0;

	return pud_set_huge(pud, phys_addr, prot);
}

static int vmap_pud_range_for_trampoline(p4d_t *p4d, unsigned long addr, unsigned long end,
			phys_addr_t phys_addr, pgprot_t prot,
			unsigned int max_page_shift, pgtbl_mod_mask *mask)
{
	pud_t *pud;
	unsigned long next;

	pud = pud_alloc_track(&init_mm, p4d, addr, mask);
	if (!pud) {
	//TRACER_PRINT_DEBUG_TRAMPOLINES("pud error");
		return -ENOMEM;
	}
	do {
		//printk("old pud entry: %lux", pud->pud);
		pud->pud = UNSET_XD_BIT(pud->pud);
		next = pud_addr_end(addr, end);

		if (vmap_try_huge_pud_for_trampoline(pud, addr, next, phys_addr, prot,
					max_page_shift)) {
			*mask |= PGTBL_PUD_MODIFIED;
			continue;
		}

		if (vmap_pmd_range_for_trampoline(pud, addr, next, phys_addr, prot,
					max_page_shift, mask)) {

			//TRACER_PRINT_DEBUG_TRAMPOLINES("error in innre vmap_pud_range_for_trampolines");
			return -ENOMEM;
		}
	} while (pud++, phys_addr += (next - addr), addr = next, addr != end);
	return 0;
}

static int vmap_try_huge_p4d_for_trampoline(p4d_t *p4d, unsigned long addr, unsigned long end,
			phys_addr_t phys_addr, pgprot_t prot,
			unsigned int max_page_shift)
{
	if (max_page_shift < P4D_SHIFT)
		return 0;

	if (!arch_vmap_p4d_supported(prot))
		return 0;

	if ((end - addr) != P4D_SIZE)
		return 0;

	if (!IS_ALIGNED(addr, P4D_SIZE))
		return 0;

	if (!IS_ALIGNED(phys_addr, P4D_SIZE))
		return 0;

	if (p4d_present(*p4d) && !p4d_free_pud_page(p4d, addr))
		return 0;

	return p4d_set_huge(p4d, phys_addr, prot);
}

static int vmap_p4d_range_for_trampoline(pgd_t *pgd, unsigned long addr, unsigned long end,
			phys_addr_t phys_addr, pgprot_t prot,
			unsigned int max_page_shift, pgtbl_mod_mask *mask)
{
	p4d_t *p4d;
	unsigned long next;

	p4d = p4d_alloc_track(&init_mm, pgd, addr, mask);

	if (!p4d) {
	//TRACER_PRINT_DEBUG_TRAMPOLINES("p4d error");
		return -ENOMEM;
	}
	do {
		//printk("old p4d entry: %lux", p4d->p4d);
		p4d->p4d = UNSET_XD_BIT(p4d->p4d);
		next = p4d_addr_end(addr, end);

		if (vmap_try_huge_p4d_for_trampoline(p4d, addr, next, phys_addr, prot,
					max_page_shift)) {
			*mask |= PGTBL_P4D_MODIFIED;
			continue;
		}

		if (vmap_pud_range_for_trampoline(p4d, addr, next, phys_addr, prot,
					max_page_shift, mask)) {

			//TRACER_PRINT_DEBUG_TRAMPOLINES("error in innre vmap_p4d_range_for_trampolines");
			return -ENOMEM;
		}
	} while (p4d++, phys_addr += (next - addr), addr = next, addr != end);
			//TRACER_PRINT_DEBUG_TRAMPOLINES("returning 0 in vmap_p4d_range_for_trampoline");
	return 0;
}

static int vmap_range_noflush_for_trampoline(unsigned long addr, unsigned long end,
			phys_addr_t phys_addr, pgprot_t prot,
			unsigned int max_page_shift)
{
	pgd_t *pgd;
	unsigned long start;
	unsigned long next;
	int err;
	pgtbl_mod_mask mask = 0;

	might_sleep();
	BUG_ON(addr >= end);

	start = addr;
	pgd = pgd_offset_k(addr);
	do {
		//printk("old pgd entry: %lux", pgd->pgd);
		pgd->pgd = UNSET_XD_BIT(pgd->pgd);
		next = pgd_addr_end(addr, end);
		err = vmap_p4d_range_for_trampoline(pgd, addr, next, phys_addr, prot,
					max_page_shift, &mask);
		if (err)
		//TRACER_PRINT_DEBUG_TRAMPOLINES("error in inner vmap_range_noflush_for_trampoline 1: %i", err);
			break;
	} while (pgd++, phys_addr += (next - addr), addr = next, addr != end);

	if (mask & ARCH_PAGE_TABLE_SYNC_MASK)
		arch_sync_kernel_mappings(start, end);

	
	//TRACER_PRINT_DEBUG_TRAMPOLINES("returning %i from vmap_range_no_flush_for_trampoline", err);

	return err;
}

int vmap_page_range_for_trampoline(unsigned long addr, unsigned long end,
		    phys_addr_t phys_addr, pgprot_t prot)
{
	int err;

	err = vmap_range_noflush_for_trampoline(addr, end, phys_addr, pgprot_nx(prot),
				 ioremap_max_page_shift);
	//TRACER_PRINT_DEBUG_TRAMPOLINES("Error is %i", err);
	flush_cache_vmap(addr, end);
	if (err ) {
	//TRACER_PRINT_DEBUG_TRAMPOLINES("got an error in vmap_page_range_for_tramp, %i", err);

	}
	if (!err) {
	//	err = kmsan_ioremap_page_range(addr, end, phys_addr, prot,
	//				       ioremap_max_page_shift);
	}
	if (err) {

	//TRACER_PRINT_DEBUG_TRAMPOLINES("error in vmap_page_range_for_tramp, %i", err);
	}
	return err;
}
