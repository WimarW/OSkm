#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/memory/paging.h"
#include "header/memory/swap.h"

/*
 * Tabel awal milik kernel. Pada 3.1.2–3.1.3, assembly loader akan
 * mengisi mapping awal dan mengaktifkan paging.
 */
__attribute__((aligned(0x1000))) struct PageDirectory _paging_kernel_page_directory;
__attribute__((aligned(0x1000))) struct PageTable _paging_kernel_page_table;

/*
 * State pengelolaan physical frame untuk 3.1.4 (Memory Manager).
 */
struct PageManagerState page_manager_state = {
    .page_frame_map = {
        [0 ... KERNEL_RESERVED_PAGE_FRAME_COUNT - 1] = true,
        [KERNEL_RESERVED_PAGE_FRAME_COUNT ... PAGE_FRAME_ALLOCATABLE_END - 1] = false,
#if PAGE_FRAME_ALLOCATABLE_END < PAGE_FRAME_MAX_COUNT
        [PAGE_FRAME_ALLOCATABLE_END ... PAGE_FRAME_MAX_COUNT - 1] = true,
#endif
    },
    .free_page_frame_count = PAGE_FRAME_ALLOCATABLE_END - KERNEL_RESERVED_PAGE_FRAME_COUNT,
};

/* First-fit: cari frame fisik kosong di rentang yang memang aman dipakai. */
static uint32_t paging_find_free_frame(void) {
    for (uint32_t i = KERNEL_RESERVED_PAGE_FRAME_COUNT;
         i < PAGE_FRAME_ALLOCATABLE_END; ++i) {
        if (!page_manager_state.page_frame_map[i]) {
            return i;
        }
    }
    return PAGE_FRAME_ALLOCATABLE_END; /* Tidak ditemukan */
}

/* Hanya mapping user yang dikelola allocator Chapter 3. */
static bool paging_valid_user_request(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t address = (uint32_t)(uintptr_t)virtual_addr;
    return page_dir == &_paging_kernel_page_directory &&
           address < USER_VIRTUAL_ADDRESS_LIMIT &&
           (address & (PAGE_FRAME_SIZE - 1u)) == 0;
}

/*
 * Helper dari kit untuk mengambil Page Table melalui recursive mapping.
 * Belum bisa digunakan sebelum recursive mapping diatur saat aktivasi paging.
 * Untuk tahap Chapter 3, Page Directory yang digunakan adalah milik kernel.
 */
struct PageTable *paging_get_page_table(
    struct PageDirectory *page_dir,
    uint32_t page_directory_index
) {
    if (page_dir != &_paging_kernel_page_directory ||
        page_directory_index >= RECURSIVE_PAGE_DIRECTORY_INDEX) {
        return NULL;
    }

    volatile struct PageDirectoryEntry *dir_entry = &page_dir->table[page_directory_index];
    struct PageTable *page_table = (struct PageTable *)(uintptr_t)(
        RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS + page_directory_index * PAGE_FRAME_SIZE
    );

    if (dir_entry->flag.present_bit) {
        return page_table;
    }

    uint32_t frame_index = paging_find_free_frame();
    if (frame_index < PAGE_FRAME_ALLOCATABLE_END) {
        page_manager_state.page_frame_map[frame_index] = true;
        --page_manager_state.free_page_frame_count;

        *dir_entry = (struct PageDirectoryEntry) {
            .flag = {
                .present_bit = true,
                .write_bit = true,
                .us_bit = true,
            },
            .page_table_base_address = frame_index,
        };
        /* Recursive alias sekarang menunjuk ke frame Page Table baru. */
        flush_single_tlb(page_table);
        memset(page_table, 0, PAGE_FRAME_SIZE);
        return page_table;
    }

    return NULL;
}

void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr,
    void *virtual_addr,
    struct PageTableEntryFlag flag
) {
    uint32_t directory_index = ((uint32_t)(uintptr_t)virtual_addr >> 22) & 0x3FF;
    uint32_t table_index = ((uint32_t)(uintptr_t)virtual_addr >> 12) & 0x3FF;

    struct PageTable *page_table = paging_get_page_table(page_dir, directory_index);
    if (page_table == NULL) {
        return;
    }

    page_table->table[table_index].flag = flag;
    page_table->table[table_index].physical_page_base_address =
        ((uint32_t)(uintptr_t)physical_addr >> 12);
    flush_single_tlb(virtual_addr);
}

void flush_single_tlb(void *virtual_addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_addr) : "memory");
}

/* --- 3.1.4 Memory Manager --- */

/* Cek apakah cukup frame data untuk amount byte, dibulatkan ke atas 4 KiB. */
bool paging_allocate_check(uint32_t amount) {
    uint32_t needed = amount / PAGE_FRAME_SIZE;
    if ((amount % PAGE_FRAME_SIZE) != 0) {
        ++needed;
    }
    return needed <= page_manager_state.free_page_frame_count;
}

/* Pasang satu virtual page user ke satu physical frame kosong. */
bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    if (!paging_valid_user_request(page_dir, virtual_addr)) {
        return false;
    }

    uint32_t address = (uint32_t)(uintptr_t)virtual_addr;
    uint32_t directory_index = address >> 22;
    uint32_t table_index = (address >> 12) & 0x3FFu;
    bool page_table_exists = page_dir->table[directory_index].flag.present_bit;

    /* Membuat Page Table baru juga butuh satu frame fisik tambahan. */
    uint32_t frames_needed = page_table_exists ? 1u : 2u;
    if (!swap_ensure_free_frames(frames_needed)) {
        return false;
    }

    struct PageTable *page_table = paging_get_page_table(page_dir, directory_index);
    if (page_table == NULL || page_table->table[table_index].flag.present_bit) {
        return false; /* Tidak ada frame / virtual page sudah terpakai */
    }

    uint32_t frame_index = paging_find_free_frame();
    if (frame_index == PAGE_FRAME_ALLOCATABLE_END) {
        return false;
    }

    page_manager_state.page_frame_map[frame_index] = true;
    --page_manager_state.free_page_frame_count;

    page_table->table[table_index] = (struct PageTableEntry) {
        .flag = {
            .present_bit = true,
            .write_bit = true,
            .us_bit = true,
        },
        .physical_page_base_address = frame_index,
    };
    flush_single_tlb(virtual_addr);
    return true;
}

static void paging_release_table_if_empty(
    struct PageDirectory *page_dir, uint32_t directory_index, struct PageTable *page_table
) {
    for (uint32_t i = 0; i < PAGE_ENTRY_COUNT; ++i) {
        if (page_table->table[i].flag.present_bit || page_table->table[i].ignored) return;
    }
    uint32_t table_frame = page_dir->table[directory_index].page_table_base_address;
    if (table_frame >= KERNEL_RESERVED_PAGE_FRAME_COUNT &&
        table_frame < PAGE_FRAME_ALLOCATABLE_END &&
        page_manager_state.page_frame_map[table_frame]) {
        page_dir->table[directory_index] = (struct PageDirectoryEntry) {0};
        void *recursive_addr = (void *)(uintptr_t)(
            RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS + directory_index * PAGE_FRAME_SIZE
        );
        flush_single_tlb(recursive_addr);
        page_manager_state.page_frame_map[table_frame] = false;
        ++page_manager_state.free_page_frame_count;
    }
}

/* Lepas satu virtual page user, kembalikan frame ke daftar frame kosong. */
bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    if (!paging_valid_user_request(page_dir, virtual_addr)) {
        return false;
    }

    uint32_t address = (uint32_t)(uintptr_t)virtual_addr;
    uint32_t directory_index = address >> 22;
    uint32_t table_index = (address >> 12) & 0x3FFu;
    if (!page_dir->table[directory_index].flag.present_bit) {
        return false; /* Page Table belum dibuat */
    }

    struct PageTable *page_table = paging_get_page_table(page_dir, directory_index);
    if (page_table == NULL) {
        return false;
    }

    volatile struct PageTableEntry *entry = &page_table->table[table_index];
    if (!entry->flag.present_bit) {
        if (entry->ignored != SWAP_PTE_MARKER) {
            return false; /* Virtual page belum dialokasikan */
        }
        swap_release_slot(entry->physical_page_base_address);
        page_table->table[table_index] = (struct PageTableEntry) {0};
        flush_single_tlb(virtual_addr);
        paging_release_table_if_empty(page_dir, directory_index, page_table);
        return true;
    }

    uint32_t frame_index = page_table->table[table_index].physical_page_base_address;
    if (frame_index < KERNEL_RESERVED_PAGE_FRAME_COUNT ||
        frame_index >= PAGE_FRAME_ALLOCATABLE_END ||
        !page_manager_state.page_frame_map[frame_index]) {
        return false; /* Tidak boleh membebaskan frame reserved/tidak valid */
    }

    /* Hapus mapping sebelum fisik frame bisa digunakan kembali. */
    page_table->table[table_index] = (struct PageTableEntry) {0};
    flush_single_tlb(virtual_addr);
    page_manager_state.page_frame_map[frame_index] = false;
    ++page_manager_state.free_page_frame_count;

    paging_release_table_if_empty(page_dir, directory_index, page_table);
    return true;
}