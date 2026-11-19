#include <stdint.h>
#include <stdbool.h>
#include "header/driver/disk.h"
#include "header/memory/paging.h"
#include "header/memory/swap.h"

struct SwapManager swap_manager = {
    .free_count = PAGE_FRAME_ON_SWAP_MAX_COUNT,
};

static uint32_t swap_find_free_slot(void) {
    for (uint32_t i = 0; i < PAGE_FRAME_ON_SWAP_MAX_COUNT; i++) {
        if (!swap_manager.swap_used[i]) return i;
    }
    return PAGE_FRAME_ON_SWAP_MAX_COUNT;
}

void swap_release_slot(uint32_t slot) {
    if (slot < PAGE_FRAME_ON_SWAP_MAX_COUNT && swap_manager.swap_used[slot]) {
        swap_manager.swap_used[slot] = false;
        swap_manager.free_count++;
    }
}

void swap_out(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t address = (uint32_t)(uintptr_t)virtual_addr;
    if (page_dir != paging_get_current_page_directory_addr() ||
        address >= USER_VIRTUAL_ADDRESS_LIMIT ||
        (address & (PAGE_FRAME_SIZE - 1u)) != 0) {
        return;
    }

    uint32_t dir_idx = address >> 22;
    uint32_t tbl_idx = (address >> 12) & 0x3FFu;
    if (!page_dir->table[dir_idx].flag.present_bit) return;

    uint32_t slot = swap_find_free_slot();
    if (slot == PAGE_FRAME_ON_SWAP_MAX_COUNT) return;

    struct PageTable *pt = paging_get_page_table(page_dir, dir_idx);
    volatile struct PageTableEntry *entry = &pt->table[tbl_idx];
    if (!entry->flag.present_bit) return;

    uint32_t frame = entry->physical_page_base_address;
    if (frame < KERNEL_RESERVED_PAGE_FRAME_COUNT || frame >= PAGE_FRAME_ALLOCATABLE_END) return;

    write_blocks_drive(virtual_addr, SWAP_SLOT_LBA(slot), SWAP_BLOCKS_PER_PAGE, SWAP_DRIVE);

    swap_manager.swap_used[slot] = true;
    swap_manager.free_count--;

    *entry = (struct PageTableEntry) {
        .flag = { .present_bit = false, .write_bit = true, .us_bit = true },
        .ignored = SWAP_PTE_MARKER,
        .physical_page_base_address = slot,
    };
    flush_single_tlb(virtual_addr);

    page_manager_state.page_frame_map[frame] = false;
    page_manager_state.free_page_frame_count++;
}

bool swap_in(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t address = (uint32_t)(uintptr_t)virtual_addr & ~(PAGE_FRAME_SIZE - 1u);
    if (page_dir != paging_get_current_page_directory_addr() ||
        address >= USER_VIRTUAL_ADDRESS_LIMIT) {
        return false;
    }

    uint32_t dir_idx = address >> 22;
    uint32_t tbl_idx = (address >> 12) & 0x3FFu;
    if (!page_dir->table[dir_idx].flag.present_bit) return false;

    struct PageTable *pt = paging_get_page_table(page_dir, dir_idx);
    volatile struct PageTableEntry *entry = &pt->table[tbl_idx];
    if (entry->flag.present_bit || entry->ignored != SWAP_PTE_MARKER) return false;

    uint32_t slot = entry->physical_page_base_address;
    void *page = (void *)(uintptr_t)address;

    if (!paging_allocate_user_page_frame(page_dir, page)) return false;

    read_blocks_drive(page, SWAP_SLOT_LBA(slot), SWAP_BLOCKS_PER_PAGE, SWAP_DRIVE);

    swap_manager.swap_used[slot] = false;
    swap_manager.free_count++;
    return true;
}

static uint32_t clock_hand = 0;

static bool swap_choose_victim(struct PageDirectory *pd, uint32_t *victim_addr) {
    const uint32_t total = (USER_VIRTUAL_ADDRESS_LIMIT >> 22) * PAGE_ENTRY_COUNT;

    for (uint32_t scanned = 0; scanned < total * 2; ) {
        uint32_t dir_idx = clock_hand / PAGE_ENTRY_COUNT;
        uint32_t tbl_idx = clock_hand % PAGE_ENTRY_COUNT;

        if (!pd->table[dir_idx].flag.present_bit) {
            uint32_t skip = PAGE_ENTRY_COUNT - tbl_idx;
            clock_hand = (clock_hand + skip) % total;
            scanned += skip;
            continue;
        }

        struct PageTable *pt = paging_get_page_table(pd, dir_idx);
        volatile struct PageTableEntry *entry = &pt->table[tbl_idx];
        uint32_t addr = (dir_idx << 22) | (tbl_idx << 12);
        clock_hand = (clock_hand + 1) % total;
        scanned++;

        if (!entry->flag.present_bit) continue;
        if (entry->flag.accessed_bit) {
            entry->flag.accessed_bit = false;
            flush_single_tlb((void *)(uintptr_t)addr);
            continue;
        }
        *victim_addr = addr;
        return true;
    }
    return false;
}

bool swap_ensure_free_frames(uint32_t needed) {
    struct PageDirectory *pd = paging_get_current_page_directory_addr();
    while (page_manager_state.free_page_frame_count < needed) {
        uint32_t before = page_manager_state.free_page_frame_count;
        uint32_t victim;
        if (swap_manager.free_count == 0 || !swap_choose_victim(pd, &victim)) return false;
        swap_out(pd, (void *)(uintptr_t)victim);
        if (page_manager_state.free_page_frame_count == before) return false;
    }
    return true;
}

static bool swap_page_is_swapped(struct PageDirectory *pd, uint32_t page) {
    if (page >= USER_VIRTUAL_ADDRESS_LIMIT) return false;
    uint32_t dir_idx = page >> 22;
    uint32_t tbl_idx = (page >> 12) & 0x3FFu;
    if (!pd->table[dir_idx].flag.present_bit) return false;
    struct PageTable *pt = paging_get_page_table(pd, dir_idx);
    volatile struct PageTableEntry *entry = &pt->table[tbl_idx];
    return !entry->flag.present_bit && entry->ignored == SWAP_PTE_MARKER;
}

void handle_page_fault(uint32_t error_code) {
    struct PageDirectory *pd = paging_get_current_page_directory_addr();
    uint32_t page = read_cr2() & ~(PAGE_FRAME_SIZE - 1u);

    if ((error_code & 1u) || !swap_page_is_swapped(pd, page)) {
        while (true);   // sementara: pelanggaran proteksi atau halaman tak pernah dialokasikan
    }
    if (!swap_ensure_free_frames(1)) while (true);
    if (!swap_in(pd, (void *)(uintptr_t)page)) while (true);
}