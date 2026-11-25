#include "header/process/process.h"
#include "header/memory/paging.h"
#include "header/stdlib/string.h"
#include "header/cpu/gdt.h"

#define USER_STACK_PAGE_COUNT        16u // 64 KiB user stack (0xBFFF0000 - 0xBFFFFFFF)
#define USER_STACK_BASE_VIRTUAL_ADDR (USER_VIRTUAL_ADDRESS_LIMIT - USER_STACK_PAGE_COUNT * PAGE_FRAME_SIZE)
#define USER_STACK_TOP_VIRTUAL_ADDR  (USER_VIRTUAL_ADDRESS_LIMIT - 4u)

// process_manager_state init
struct ProcessManagerState process_manager_state = {
    .active_process_count = 0,
};
// init process list
struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX] = {0};

static uint32_t ceil_div(uint32_t a, uint32_t b) {
    if (b == 0) {
        return 0;
    }
    return (a / b) + ((a % b) != 0);
}
// returns the index of the first found inactive process in _process_list
static int32_t process_list_get_inactive_index(void) {
    for (int32_t i = 0; i < PROCESS_COUNT_MAX; ++i) {
        if (_process_list[i].metadata.state == PROCESS_STATE_INACTIVE) {
            return i;
        }
    }
    return -1;
}

// Generates a pid sequentially
static uint32_t process_generate_new_pid(void) {
    static uint32_t last_pid = 0;
    return ++last_pid;
}

struct ProcessControlBlock* process_get_current_running_pcb_pointer(void) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; ++i) {
        if (_process_list[i].metadata.state == PROCESS_STATE_RUNNING) {
            return &_process_list[i];
        }
    }
    return NULL;
}

int32_t process_create_user_process(struct EXT2DriverRequest request) {
    int32_t retcode = PROCESS_CREATE_SUCCESS;
    struct PageDirectory *new_page_dir = NULL;
    struct PageDirectory *prev_page_dir = NULL;
    struct ProcessControlBlock *new_pcb = NULL;
    
    // Ensure process count doesn't go over max
    if (process_manager_state.active_process_count >= PROCESS_COUNT_MAX) {
        retcode = PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
        goto exit_cleanup;
    }
    
    // Ensure entrypoint & executable buffer stay below kernel space and stack page
    uint32_t exec_base = (uint32_t)(uintptr_t)request.buf;
    if (exec_base >= KERNEL_VIRTUAL_ADDRESS_BASE ||
        (exec_base & (PAGE_FRAME_SIZE - 1u)) != 0 ||
        request.buffer_size == 0) {
        retcode = PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
        goto exit_cleanup;
    }

    uint32_t exec_frame_count = ceil_div(request.buffer_size, PAGE_FRAME_SIZE);
    uint32_t page_frame_count_needed = exec_frame_count + USER_STACK_PAGE_COUNT;

    if (exec_base + exec_frame_count * PAGE_FRAME_SIZE > USER_STACK_BASE_VIRTUAL_ADDR) {
        retcode = PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
        goto exit_cleanup;
    }
    if (!paging_allocate_check((page_frame_count_needed + 2u) * PAGE_FRAME_SIZE) ||
        page_frame_count_needed > PROCESS_PAGE_FRAME_COUNT_MAX) {
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }
    
    int32_t p_index = process_list_get_inactive_index();
    if (p_index < 0) {
        retcode = PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
        goto exit_cleanup;
    }
    
    new_pcb = &(_process_list[p_index]);
    memset(new_pcb, 0, sizeof(struct ProcessControlBlock));

    // Copy name into kernel PCB buffer before switching CR3 (safe for future exec syscalls)
    uint32_t name_len = request.name_len;
    if (name_len >= PROCESS_NAME_LENGTH_MAX) {
        name_len = PROCESS_NAME_LENGTH_MAX - 1;
    }
    if (request.name != NULL && name_len > 0) {
        memcpy(new_pcb->metadata.process_name, request.name, name_len);
    }
    new_pcb->metadata.process_name[name_len] = '\0';
    request.name = new_pcb->metadata.process_name;
    request.name_len = (uint8_t)name_len;

    // Virtual Address
    new_page_dir = paging_create_new_page_directory();
    if (new_page_dir == NULL) {
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }

    prev_page_dir = paging_get_current_page_directory_addr();
    paging_use_page_directory(new_page_dir);
    
    for (uint32_t i = 0; i < exec_frame_count; ++i) {
        void *vaddr = (void *)(uintptr_t)(exec_base + i * PAGE_FRAME_SIZE);
        if (!paging_allocate_user_page_frame(new_page_dir, vaddr)) {
            retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
            goto exit_cleanup;
        }
        new_pcb->memory.virtual_addr_used[new_pcb->memory.page_frame_used_count++] = vaddr;
    }
    
    for (uint32_t i = 0; i < USER_STACK_PAGE_COUNT; ++i) {
        void *stack_vaddr = (void *)(uintptr_t)(USER_STACK_BASE_VIRTUAL_ADDR + i * PAGE_FRAME_SIZE);
        if (!paging_allocate_user_page_frame(new_page_dir, stack_vaddr)) {
            retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
            goto exit_cleanup;
        }
        new_pcb->memory.virtual_addr_used[new_pcb->memory.page_frame_used_count++] = stack_vaddr;
    }

    // Load Executable
    int8_t fs_status = read(request);
    paging_use_page_directory(prev_page_dir);
    prev_page_dir = NULL;
    if (fs_status != 0) {
        retcode = PROCESS_CREATE_FAIL_FS_READ_FAILURE;
        goto exit_cleanup;
    }

    // Context Initialization
    uint32_t user_data_selector = GDT_USER_DATA_SEGMENT_SELECTOR | 0x3u;
    new_pcb->context.cpu.segment.ds = user_data_selector;
    new_pcb->context.cpu.segment.es = user_data_selector;
    new_pcb->context.cpu.segment.fs = user_data_selector;
    new_pcb->context.cpu.segment.gs = user_data_selector;
    new_pcb->context.cpu.stack.esp  = USER_STACK_TOP_VIRTUAL_ADDR;
    new_pcb->context.cpu.stack.ebp  = USER_STACK_TOP_VIRTUAL_ADDR;
    new_pcb->context.eip            = exec_base;
    new_pcb->context.eflags         = CPU_EFLAGS_BASE_FLAG | CPU_EFLAGS_FLAG_INTERRUPT_ENABLE;
    new_pcb->context.page_directory_virtual_addr = new_page_dir;
    
    // Process Metadata
    new_pcb->metadata.pid   = process_generate_new_pid();
    new_pcb->metadata.state = PROCESS_STATE_READY;
    ++process_manager_state.active_process_count;
    return PROCESS_CREATE_SUCCESS;

exit_cleanup:
    if (prev_page_dir != NULL) {
        paging_use_page_directory(prev_page_dir);
    }
    if (new_page_dir != NULL) {
        paging_free_page_directory(new_page_dir);
    }
    if (new_pcb != NULL) {
        memset(new_pcb, 0, sizeof(struct ProcessControlBlock));
    }
    return retcode;
}

bool process_destroy(uint32_t pid) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; ++i) {
        struct ProcessControlBlock *pcb = &_process_list[i];
        if (pcb->metadata.state != PROCESS_STATE_INACTIVE && pcb->metadata.pid == pid) {
            struct PageDirectory *pd = pcb->context.page_directory_virtual_addr;
            if (pd == paging_get_current_page_directory_addr()) {
                paging_use_page_directory(&_paging_kernel_page_directory);
            }
            if (pd != NULL) {
                paging_free_page_directory(pd);
            }
            memset(pcb, 0, sizeof(struct ProcessControlBlock));
            --process_manager_state.active_process_count;
            return true;
        }
    }
    return false;
}

uint32_t process_get_active_list(struct ProcessInfo *out_list, uint32_t max_count) {
    if (out_list == NULL || max_count == 0) {
        return 0;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX && count < max_count; ++i) {
        struct ProcessControlBlock *pcb = &_process_list[i];
        if (pcb->metadata.state != PROCESS_STATE_INACTIVE) {
            out_list[count].pid = pcb->metadata.pid;
            out_list[count].state = pcb->metadata.state;
            memcpy(out_list[count].process_name, pcb->metadata.process_name, PROCESS_NAME_LENGTH_MAX);
            out_list[count].process_name[PROCESS_NAME_LENGTH_MAX - 1] = '\0';
            ++count;
        }
    }
    return count;
}