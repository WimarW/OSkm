#include "header/process/scheduler.h"

// Next PCB slot to inspect, rather than a PID (PIDs need not match table indices).
static uint32_t next_search_index = 0;

void scheduler_init(void) {
    next_search_index = 0;
    // Startup keeps IF clear until the first process's iret restores its flags.
    activate_timer_interrupt();
}

void scheduler_save_context_to_current_running_pcb(struct Context ctx) {
    struct ProcessControlBlock *running = process_get_current_running_pcb_pointer();
    if (running != NULL) {
        running->context = ctx;
    }
}

struct ProcessControlBlock *scheduler_select_next_process(void) {
    // Inspect each slot at most once, wrapping from the last slot back to zero.
    for (uint32_t offset = 0; offset < PROCESS_COUNT_MAX; ++offset) {
        uint32_t index = (next_search_index + offset) % PROCESS_COUNT_MAX;
        struct ProcessControlBlock *candidate = &_process_list[index];
        bool runnable = candidate->metadata.state == PROCESS_STATE_READY ||
                        candidate->metadata.state == PROCESS_STATE_RUNNING;

        if (runnable && candidate->metadata.pid != 0 &&
            candidate->context.page_directory_virtual_addr != NULL) {
            // The old RUNNING process is eligible too: with one process, it
            // must be able to receive another turn after the scan wraps.
            next_search_index = (index + 1) % PROCESS_COUNT_MAX;
            return candidate;
        }
    }
    return NULL;
}

__attribute__((noreturn)) void scheduler_switch_to_next_process(void) {
    // Keep PCB states and the address-space transition atomic on this CPU.
    __asm__ volatile("cli" : : : "memory");
    struct ProcessControlBlock *running = process_get_current_running_pcb_pointer();
    struct ProcessControlBlock *next = scheduler_select_next_process();

    if (next == NULL) {
        // No user context is available. Sleep in the kernel while still allowing
        // hardware interrupts; kernel-mode IRQ0 ACKs and returns to this loop.
        while (true) {
            __asm__ volatile("sti; hlt" : : : "memory");
        }
    }

    if (running != NULL && running != next) {
        running->metadata.state = PROCESS_STATE_READY;
    }
    next->metadata.state = PROCESS_STATE_RUNNING;

    // Both the kernel stack and PCB remain mapped in the shared higher half.
    // Activate user memory before restoring its instruction/stack pointers.
    paging_use_page_directory(next->context.page_directory_virtual_addr);
    process_context_switch(next->context);
}
