#include "allocator.h"
#include "logging.h"
#include "trampoline.h"
#ifdef TRACER_USERSPACE
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// pthread_mutex_t lock;
// pthread_spinlock_t lock;
int lock = 0;
static int own_lock_lock(void) {
    int new = 0;
repeat:
    if (!__atomic_compare_exchange_4(&lock, &new, 1, 0, __ATOMIC_SEQ_CST,
                                     __ATOMIC_SEQ_CST)) {

        new = 0;
        goto repeat;
    }
}
static int own_lock_unlock(void) { lock = 0; }
#else
// see https://docs.kernel.org/locking/mutex-design.html
#include <linux/mm.h>
#include "vmalloc_for_tramp.h"
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/memory.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>
static DEFINE_MUTEX(lock);

#endif
#ifdef TRACER_USERSPACE
#define lock_mutex(lock) own_lock_lock()
#define unlock_mutex(lock) own_lock_unlock()
#else
// TODO: we dont really need a lock here as we currently only support single
// threaded program in kernel space the mutex did not work is it enables the IF
// (interrupt enable flag) on the slow path, which then crashes on fault handler
// exit
#define lock_mutex(lock)   // mutex_lock(lock)
#define unlock_mutex(lock) // mutex_unlock(lock)
#endif

#ifdef TRACER_USERSPACE
void init_allocator() {
	lock = 0;
    // pthread_mutex_init(&lock, NULL);
    //pthread_spin_init(&lock, PTHREAD_PROCESS_PRIVATE);
}

#else
void init_allocator(void) { mutex_init(&lock); }
#endif

PageInfo *get_page_info(PageAddress address, Allocator *allocator) {
    // this approach has a problem if the base address starts on an odd number
    // in that case, cutting of the bits would lower the result too much
    for (int i = 0; i < MAX_PAGES; i++) {
        for (int j = 0; j < PAGES_PER_PAGE; j++) {
            if (allocator->infos[i].address >> 12 == address - j) {
                return &allocator->infos[i];
            }
        }
    }
    return NULL;
}

PageInfo *allocate(PageAddress address, Allocator *allocator) {
    TRACER_PRINT_DEBUG_TRAMPOLINES("is afer start 1\n");
    lock_mutex(&lock);
    TRACER_PRINT_DEBUG_TRAMPOLINES("is afer start 2\n");


#ifdef TRACER_USERSPACE

    void *res_address =
        mmap((void *)(address << 12), PAGES_PER_PAGE * getpagesize(),
             PROT_READ | PROT_WRITE | PROT_EXEC,
             MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED_NOREPLACE, -1, 0);
    if (res_address == MAP_FAILED) {
        unlock_mutex(&lock);
        return NULL;
    }

#else
    // TODO: find a way to allocatte the memory
    long actual_address = address << 12;
    if (actual_address < KERNEL_TRAMP_ALLOC_START ||
        actual_address + MAX_TRAMPOLINE_SIZE > KERNEL_TRAMP_ALLOC_END) {
        // pr_info("returning early from address %lx", actual_address);
        unlock_mutex(&lock);
        return NULL;
    }
    // we allocate a continious chunk of phyiscal memory
    struct page *page = alloc_pages(GFP_KERNEL, 2);
    for (int i = 0; i < 4; i++) {
        // pr_info("pfn of page %i is %li", i, page_to_pfn(&page[i]));

        // we map the physical pages into the virtual kernel address space
        // int res = vmap_pages_range(actual_address,
        //			   actual_address + 4 * PAGE_SIZE, PAGE_KERNEL,
        //			   &page, PAGE_SIZE);
    }
    pgprot_t prot = {.pgprot = PAGE_KERNEL_EXEC.pgprot & ~(1l << 63)};
    // pr_info("prot is %lxu", prot.pgprot);
    int res = vmap_page_range_for_trampoline(
        actual_address, actual_address + 4 * PAGE_SIZE,
        page_to_pfn(page) << PAGE_SHIFT, prot);
    // something in vmap_page_range_for_trampoline sets the interrupt flag wich
    // is not ok.(its probably a lock/mutex) So we diable the IF bit again
    asm("pushf\n\t"
        "mov $0x200, %%rax\n\t"
        "not %%rax\n\t"
        "and (%%rsp), %%rax\n\t"
        "mov %%rax, (%%rsp)\n\t"
        "popfq\n\t" ::
            : "%rax");
    // pr_info("res of first vmap_page_range %i", res);
    if (res) {
        // pr_info("Failed to map the pages");
        unlock_mutex(&lock);
        return NULL;
    }

#endif
#ifdef TRACER_LOG_ERROR
    if (allocator->next_free_index >= MAX_PAGES) {
        TRACER_PRINT_ERROR("Allocated too many trampoline pages");
    }

#endif

    TRACER_PRINT_DEBUG_TRAMPOLINES("is afer 0\n");
    allocator->infos[allocator->next_free_index].address = address << 12;
    TRACER_PRINT_DEBUG_TRAMPOLINES("is afer 1\n");
    allocator->next_free_index += 1;
    TRACER_PRINT_DEBUG_TRAMPOLINES("is afer 2\n");
    PageInfo *info = &allocator->infos[allocator->next_free_index - 1];
    TRACER_PRINT_DEBUG_TRAMPOLINES("is afer 3\n");
    unlock_mutex(&lock);
    // TRACER_PRINT_DEBUG_TRAMPOLINES("Successfull allocation at 0x%lx",
    //                               address << 12);
    return info;
}

TrampolineInfo *find_trampoline_location_on_page(PageInfo *page_info,
                                                 long address_on_page,
                                                 UsedOpcodes *opcodes) {
    for (int i = 2; i < NUM_INVALID_OPCODES; i++) {
        for (int j = 2; j < NUM_INVALID_OPCODES; j++) {
            //	printf("finding location on page\n");
            // fast route if we know that the new address will be out of bounds
            // if we allow more than 16 pages per page, this does not work
            // anymore
            if (PAGES_PER_PAGE <= 16 &&
                invalid_opcodes[j] > (0x0F | (PAGES_PER_PAGE - 1) << 0x4)) {
                continue;
            }
            // search within the page
            long new_address = address_on_page + invalid_opcodes[i] +
                               (invalid_opcodes[j] << 8);
            // case that the new address in not even within the page
            if (new_address < page_info->address ||
                new_address + MAX_TRAMPOLINE_SIZE >=
                    page_info->address + 4096 * PAGES_PER_PAGE) {
                /*printf("returning null because the new address is not within"
                       "page boudns, upper limit is %lx, address on the page "
                       "is %lx, added"
                       "offset is %x and new address + max"
                       "tramp size is %"
                       "lx\n ",
                       page_info->address + 4096, address_on_page,
                       invalid_opcodes[i] + (invalid_opcodes[j] << 8),
                       new_address + MAX_TRAMPOLINE_SIZE);
                                */

                continue;
            }
            // check that spot does not interfere with any other allocations
            short new_offset = new_address - page_info->address;
            int usable = 1;

            lock_mutex(&lock);
            short start_A = new_offset;
            short end_A = new_offset + MAX_TRAMPOLINE_SIZE;
            for (int k = 0; k < MAX_TRAMPOLOINES_PER_PAGE; k++) {
                TrampolineInfo *current_info = &page_info->trampolines[k];
                if (current_info->start_offset == 0) {
                    // not used so it does not interfere

                    // pthread_mutex_unlock(&lock);
                    continue;
                }
                short start_B = current_info->start_offset;
                short end_B = current_info->start_offset + MAX_TRAMPOLINE_SIZE;

                if (start_B <= start_A && end_B >= start_A) {
                    usable = 0;
                }
                if (start_B <= end_A && end_B >= end_A) {
                    usable = 0;
                }
            }
            if (!usable) {
                unlock_mutex(&lock);
                continue;
            }
            page_info->trampolines[page_info->next_index].start_offset =
                new_offset;
            page_info->next_index += 1;
            opcodes->d = invalid_opcodes[j];
            opcodes->e = invalid_opcodes[i];
            TrampolineInfo *ret_val =
                &page_info->trampolines[page_info->next_index - 1];
            unlock_mutex(&lock);
            return ret_val;
        }
    }
    return NULL;
}
