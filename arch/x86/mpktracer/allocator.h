#pragma once
// TODO: this is not ideal, as it will just break once we run out of pages, but
// for now it should be fine...
#define MAX_PAGES 100000
// i have absolutely no idea if this is sufficient or far too many...
#define MAX_TRAMPOLOINES_PER_PAGE 50
#define TRAMPOLINE_STACK_SIZE (1 << 14)
// defines how many pages are allocated at once
#define PAGES_PER_PAGE 2 // the initial allocator had a value of one
#define BITS_FOR_TRAMP_PAGE 1 // the log2 of PAGES_PER_PAGE

#define MAX_TRAMPOLINE_SIZE 820
// the trampolines may fall into this region, but we still have to check for existing mappings
#define KERNEL_TRAMP_ALLOC_START 0xffffffff00000000
#define KERNEL_TRAMP_ALLOC_END 0xfffffffffeffffff
typedef struct {
    short start_offset; // the offset from the beginning of the page of the
} TrampolineInfo;

typedef struct {
    char d;
    char e;

} UsedOpcodes;

typedef struct {
    // bit mask for each allocated page
    TrampolineInfo trampolines[MAX_TRAMPOLOINES_PER_PAGE];
    long address;
    int next_index;

} PageInfo;

typedef struct allocator_ {
    int next_free_index;
    PageInfo infos[MAX_PAGES];
} Allocator;

typedef long PageAddress;

#ifdef TRACER_USERSPACE
void init_allocator();
#else 
void init_allocator(void);
#endif
PageInfo *allocate(PageAddress address, Allocator *allocator);
// returns NULL if the page is not allocated
PageInfo *get_page_info(PageAddress address, Allocator *allocator);

TrampolineInfo *find_trampoline_location_on_page(PageInfo *page_info,
 
                                                 long address_on_page,
                                                 UsedOpcodes *opcodes);
