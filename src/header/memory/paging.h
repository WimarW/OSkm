#ifndef _PAGING_H
#define _PAGING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
/**
 * Note: MB often referring to MiB in context of memory management
 * change this to be a small number to test if swap working properly
 * kernel will be tested with a small number of SYSTEM_MEMORY_MB
 */
 #define SYSTEM_MEMORY_MB     128

// Number of entries in a page directory
#define PAGE_ENTRY_COUNT     1024

// Page Frame (PF) Size: 4 KiB
#define PAGE_FRAME_SIZE      0x1000u

#define KERNEL_RESERVED_PAGE_FRAME_COUNT (0x400000u / PAGE_FRAME_SIZE)
#define RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS 0xFFC00000u
#define RECURSIVE_PAGE_DIRECTORY_INDEX 1023u

// Maximum usable page frame. Default count: 128 MiB / 4 KiB = 32768 frames
#define PAGE_FRAME_MAX_COUNT ((SYSTEM_MEMORY_MB << 20) / PAGE_FRAME_SIZE)

/* Chapter 3.1.4: batas layout yang digunakan panduan IF2130. */
#define USER_VIRTUAL_ADDRESS_LIMIT 0xC0000000u
#define USER_PHYSICAL_MEMORY_END 0x07FE0000u
/* Physical frames below 4 MiB and at/above 0x07FE0000 tidak dialokasikan. */
#define PAGE_FRAME_ALLOCATABLE_END \
    ((PAGE_FRAME_MAX_COUNT < (USER_PHYSICAL_MEMORY_END / PAGE_FRAME_SIZE)) ? \
     PAGE_FRAME_MAX_COUNT : (USER_PHYSICAL_MEMORY_END / PAGE_FRAME_SIZE))

#if PAGE_FRAME_ALLOCATABLE_END <= KERNEL_RESERVED_PAGE_FRAME_COUNT
#error "SYSTEM_MEMORY_MB must leave RAM above the first 4 MiB for user frames"
#endif

// kernel's  page directory, using 4 KiB pages
extern struct PageDirectory _paging_kernel_page_directory;
extern struct PageTable _paging_kernel_page_table;

/**
 * Lowest 8 bits of a Page Directory Entry for 4 KiB pages.
 * The page-size (PS) bit must remain 0 in this configuration.
 * Reference: Intel Manual Vol. 3A, Chapter 4 (32-bit paging).
 */
struct PageDirectoryEntryFlag {
    uint32_t present_bit          : 1; // Bit 0: entry exists
    uint32_t write_bit            : 1; // Bit 1: writable
    uint32_t us_bit               : 1; // Bit 2: user-accessible
    uint32_t pwt_bit              : 1; // Bit 3: write-through caching
    uint32_t pcd_bit              : 1; // Bit 4: disable caching
    uint32_t accessed_bit         : 1; // Bit 5: accessed by CPU
    uint32_t ignored_bit          : 1; // Bit 6: ignored for 4 KiB PDE
    uint32_t use_pagesize_4_mb    : 1; // Bit 7: must be 0 (4 KiB pages)
} __attribute__((packed));

/**
 * Lowest 8 bits of a Page Table Entry for 4 KiB pages.
 * The PTE uses dirty and PAT bits where the PDE has other meanings.
 * Reference: Intel Manual Vol. 3A, Chapter 4 (32-bit paging).
 */
struct PageTableEntryFlag {
    uint32_t present_bit          : 1; // Bit 0: entry exists
    uint32_t write_bit            : 1; // Bit 1: writable
    uint32_t us_bit               : 1; // Bit 2: user-accessible
    uint32_t pwt_bit              : 1; // Bit 3: write-through caching
    uint32_t pcd_bit              : 1; // Bit 4: disable caching
    uint32_t accessed_bit         : 1; // Bit 5: accessed by CPU
    uint32_t dirty_bit            : 1; // Bit 6: page has been written
    uint32_t page_attr_table      : 1; // Bit 7: PAT
} __attribute__((packed));

/**
 * Page Directory Entry, for page size 4 KiB.
 * Points to a Page Table (not directly to a physical data frame).
 * References: Intel Manual 3a - Ch 4 Paging - Figure 4-4 PDE: 4KB page
 *
 * Bits 0-7:   access and page-size flags
 * Bit 8:      ignored for a 4 KiB page directory entry
 * Bits 9-11:  available for software use
 * Bits 12-31: physical base address of the page table, divided by 4096
 */
struct PageDirectoryEntry {
    struct PageDirectoryEntryFlag flag;
    uint16_t global_page           : 1;  // Bit 8: ignored in a 4 KiB PDE
    uint16_t ignored               : 3;  // Bits 9-11: software available
    uint32_t page_table_base_address : 20; // Bits 12-31: physical table base >> 12
} __attribute__((packed));

/**
 * Page Table Entry, for page size 4 KiB.
 * Points to a physical 4 KiB frame.
 * References: Intel Manual 3a - Ch 4 Paging - Figure 4-4 PDE: 4KB page
 *
 * Bits 0-7:   access, dirty and PAT flags
 * Bit 8:      global page flag
 * Bits 9-11:  available for software use
 * Bits 12-31: physical base address of the frame, divided by 4096
 */
struct PageTableEntry {
    struct PageTableEntryFlag flag;
    uint16_t global_page             : 1;  // Bit 8: global mapping
    uint16_t ignored                 : 3;  // Bits 9-11: software available
    uint32_t physical_page_base_address : 20; // Bits 12-31: physical frame base >> 12
} __attribute__((packed));
/**
 * Page Directory, contain array of PageDirectoryEntry.
 * Note: This data structure is volatile (can be modified from outside this code, check "C volatile keyword").
 * MMU operation, TLB hit & miss also affecting this data structure (dirty, accessed bit, etc).
 *
 * Warning: Address must be aligned in 4 KB (listed on Intel Manual), use __attribute__((aligned(0x1000))),
 * unaligned definition of PageDirectory will cause triple fault
 *
 * @param table Fixed-width array of PageDirectoryEntry with size PAGE_ENTRY_COUNT
 */
struct PageDirectory {
    volatile struct PageDirectoryEntry table[PAGE_ENTRY_COUNT];
} __attribute__((packed, aligned(0x1000)));

/**
 *  Page Table, contain array of PageTableEntry
 */
struct PageTable {
    volatile struct PageTableEntry table[PAGE_ENTRY_COUNT];
} __attribute__((packed, aligned(0x1000)));

/* x86 32-bit, 4 KiB page-table layout checks (3.1.1). */
_Static_assert(sizeof(struct PageDirectoryEntryFlag) == 1, "PDE flags must be 8 bits");
_Static_assert(sizeof(struct PageTableEntryFlag) == 1, "PTE flags must be 8 bits");
_Static_assert(sizeof(struct PageDirectoryEntry) == 4, "PDE must be 32 bits");
_Static_assert(sizeof(struct PageTableEntry) == 4, "PTE must be 32 bits");
_Static_assert(sizeof(struct PageDirectory) == PAGE_FRAME_SIZE, "Page Directory must be 4 KiB");
_Static_assert(sizeof(struct PageTable) == PAGE_FRAME_SIZE, "Page Table must be 4 KiB");
_Static_assert(__alignof__(struct PageDirectory) == PAGE_FRAME_SIZE, "Page Directory alignment must be 4 KiB");
_Static_assert(__alignof__(struct PageTable) == PAGE_FRAME_SIZE, "Page Table alignment must be 4 KiB");

/**
 * Containing page manager states.
 *
 * @param page_frame_map Keeping track empty space. True when the page frame is currently used
 * ...
 */
struct PageManagerState {
    bool     page_frame_map[PAGE_FRAME_MAX_COUNT];
    uint32_t free_page_frame_count;
    // TODO: Add if needed ...
} __attribute__((packed));

/* State allocator global didefinisikan di paging.c. */
extern struct PageManagerState page_manager_state;

/**
 * Edit page directory with respective parameter
 *
 * @param page_dir      Page directory to update
 * @param physical_addr Physical address to map
 * @param virtual_addr  Virtual address to map
 * @param flag          Page entry flags
 */
void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr,
    void *virtual_addr,
    struct PageTableEntryFlag flag
);

/**
 * Get the requested page table from respective parameter
 * @param page_dir      Page directory to read
 * @param page_directory_index page table's position/index on page directory
 * note: sudah diberikan implementasinya pada paging.c dengan pendekatan fractal memory maping alias recursive memory mapping.
 * read: https://wiki.osdev.org/Fractal_Page_Mapping
 */
struct PageTable *paging_get_page_table(struct PageDirectory *page_dir, uint32_t page_directory_index);

/**
 * Invalidate page that contain virtual address in parameter
 *
 * @param virtual_addr Virtual address to flush
 */
void flush_single_tlb(void *virtual_addr);





/* --- Memory Management --- */
/**
 * Check whether a certain amount of physical memory is available
 *
 * @param amount Requested amount of physical memory in bytes
 * @return       Return true when there's enough free memory available
 */
bool paging_allocate_check(uint32_t amount);

/**
 * Allocate single user page frame in page directory
 *
 * @param page_dir     Page directory to update
 * @param virtual_addr Virtual address to be allocated
 * @return             true jika berhasil, false jika gagal
 */
bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr);

/**
 * Deallocate single user page frame in page directory
 *
 * @param page_dir      Page directory to update
 * @param virtual_addr  Virtual address to be deallocated
 * @return              true jika berhasil, false jika gagal
 */
bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr);

#endif
