#ifndef _SWAP_H
#define _SWAP_H
#define SWAP_DRIVE         1
#define SWAP_BLOCKS_PER_PAGE 8        // 4096 / 512
#define SWAP_SLOT_LBA(slot) ((slot) * SWAP_BLOCKS_PER_PAGE)
#define SWAP_DRIVE            1
#define SWAP_BLOCKS_PER_PAGE  8
#define SWAP_SLOT_LBA(slot)   ((slot) * SWAP_BLOCKS_PER_PAGE)
#define SWAP_PTE_MARKER       1u

#include <stdint.h>
#include <stdbool.h>
#include "header/memory/paging.h"

#define PAGE_FRAME_ON_SWAP_MAX_COUNT 10

struct SwapManager {
    bool     swap_used[PAGE_FRAME_ON_SWAP_MAX_COUNT];
    uint32_t free_count;
} __attribute__((packed));

extern struct SwapManager swap_manager;

bool swap_ensure_free_frames(uint32_t needed);
bool swap_in(struct PageDirectory *page_dir, void *virtual_addr);
void swap_out(struct PageDirectory *page_dir, void *virtual_addr);
void swap_release_slot(uint32_t slot);

static inline uint32_t read_cr2(void) {
    uint32_t fault_address;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));
    return fault_address;
}

void handle_page_fault(uint32_t error_code);

#endif