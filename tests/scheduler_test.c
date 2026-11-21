/* Linux/WSL test executable: use the real scheduler and process table without
 * libc or privileged instructions. Kernel-only process functions are discarded
 * by --gc-sections; the PCB table and current-process lookup remain real code.
 */
#include "header/process/scheduler.h"
#include "header/stdlib/string.h"

static struct PageDirectory test_directories[PROCESS_COUNT_MAX];

// Only replace the hardware boundary in the host logic tests. PIT/PIC startup
// is exercised by run_scheduler_boot_tests.py and irq0_smoke.py inside QEMU.
void activate_timer_interrupt(void) {
}

static void report(const char *text) {
    uint32_t length = 0;
    while (text[length]) ++length;
    uint32_t syscall_number = 4; /* Linux i386 write, test executable only */
    __asm__ volatile("int $0x80" : "+a"(syscall_number)
                     : "b"(1), "c"(text), "d"(length) : "memory", "cc");
}

__attribute__((noreturn)) static void exit_test(uint32_t status) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(status) : "memory", "cc");
    __builtin_unreachable();
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        report("FAIL: "); report(__func__); report(": " #condition "\n"); \
        exit_test(1); \
    } \
} while (0)

static void reset_fixture(void) {
    memset(_process_list, 0, sizeof(_process_list));
    process_manager_state.active_process_count = 0;
    scheduler_init();
}

static struct ProcessControlBlock *make_process(uint32_t index, PROCESS_STATE state) {
    struct ProcessControlBlock *pcb = &_process_list[index];
    pcb->metadata.pid = index + 1;
    pcb->metadata.state = state;
    pcb->context.page_directory_virtual_addr = &test_directories[index];
    ++process_manager_state.active_process_count;
    return pcb;
}

static void empty_table_returns_null(void) {
    reset_fixture();
    CHECK(scheduler_select_next_process() == NULL);
    CHECK(scheduler_select_next_process() == NULL);
}

static void single_ready_process_keeps_its_turn(void) {
    reset_fixture();
    struct ProcessControlBlock *only = make_process(5, PROCESS_STATE_READY);
    for (uint32_t i = 0; i < 3; ++i) {
        CHECK(scheduler_select_next_process() == only);
        CHECK(only->metadata.state == PROCESS_STATE_READY);
    }
    CHECK(process_get_current_running_pcb_pointer() == NULL);
}

static void three_processes_take_turns(void) {
    reset_fixture();
    struct ProcessControlBlock *a = make_process(0, PROCESS_STATE_READY);
    struct ProcessControlBlock *b = make_process(1, PROCESS_STATE_READY);
    struct ProcessControlBlock *c = make_process(2, PROCESS_STATE_READY);
    CHECK(scheduler_select_next_process() == a);
    CHECK(scheduler_select_next_process() == b);
    CHECK(scheduler_select_next_process() == c);
    CHECK(scheduler_select_next_process() == a);
}

static void inactive_slots_are_skipped(void) {
    reset_fixture();
    make_process(0, PROCESS_STATE_INACTIVE);
    struct ProcessControlBlock *a = make_process(4, PROCESS_STATE_READY);
    make_process(5, PROCESS_STATE_INACTIVE);
    struct ProcessControlBlock *b = make_process(9, PROCESS_STATE_READY);
    CHECK(scheduler_select_next_process() == a);
    CHECK(scheduler_select_next_process() == b);
    CHECK(scheduler_select_next_process() == a);
}

static void last_slot_wraps_to_first(void) {
    reset_fixture();
    struct ProcessControlBlock *first = make_process(0, PROCESS_STATE_READY);
    struct ProcessControlBlock *last = make_process(PROCESS_COUNT_MAX - 1, PROCESS_STATE_READY);
    CHECK(scheduler_select_next_process() == first);
    CHECK(scheduler_select_next_process() == last);
    CHECK(scheduler_select_next_process() == first);
}

static void running_process_can_be_selected_again(void) {
    reset_fixture();
    struct ProcessControlBlock *current = make_process(3, PROCESS_STATE_RUNNING);
    struct ProcessControlBlock *other = make_process(8, PROCESS_STATE_READY);
    CHECK(scheduler_select_next_process() == current);
    CHECK(scheduler_select_next_process() == other);
    CHECK(scheduler_select_next_process() == current);
    other->metadata.state = PROCESS_STATE_INACTIVE;
    CHECK(scheduler_select_next_process() == current);
}

static void incomplete_pcbs_are_not_selected(void) {
    reset_fixture();
    make_process(0, PROCESS_STATE_READY)->metadata.pid = 0;
    make_process(1, PROCESS_STATE_READY)->context.page_directory_virtual_addr = NULL;
    struct ProcessControlBlock *valid = make_process(2, PROCESS_STATE_READY);
    CHECK(scheduler_select_next_process() == valid);
    valid->metadata.state = PROCESS_STATE_INACTIVE;
    CHECK(scheduler_select_next_process() == NULL);
}

static void selection_observes_state_changes(void) {
    reset_fixture();
    struct ProcessControlBlock *a = make_process(1, PROCESS_STATE_READY);
    struct ProcessControlBlock *b = make_process(6, PROCESS_STATE_READY);
    CHECK(scheduler_select_next_process() == a);
    a->metadata.state = PROCESS_STATE_INACTIVE;
    CHECK(scheduler_select_next_process() == b);
    b->metadata.state = PROCESS_STATE_INACTIVE;
    CHECK(scheduler_select_next_process() == NULL);
    a->metadata.state = PROCESS_STATE_READY;
    CHECK(scheduler_select_next_process() == a);
}

static void init_resets_cursor_without_erasing_processes(void) {
    reset_fixture();
    struct ProcessControlBlock *a = make_process(0, PROCESS_STATE_READY);
    struct ProcessControlBlock *b = make_process(7, PROCESS_STATE_READY);
    a->context.eip = 0x12345678;
    CHECK(scheduler_select_next_process() == a);
    CHECK(scheduler_select_next_process() == b);
    CHECK(scheduler_select_next_process() == a);
    scheduler_init();
    CHECK(scheduler_select_next_process() == a);
    CHECK(a->metadata.state == PROCESS_STATE_READY);
    CHECK(b->metadata.state == PROCESS_STATE_READY);
    CHECK(a->context.eip == 0x12345678);
    CHECK(process_manager_state.active_process_count == 2);
}

static void saved_context_belongs_to_running_process(void) {
    reset_fixture();
    struct ProcessControlBlock *waiting = make_process(0, PROCESS_STATE_READY);
    struct ProcessControlBlock *running = make_process(4, PROCESS_STATE_RUNNING);
    struct Context waiting_before = waiting->context;
    struct Context snapshot;
    memset(&snapshot, 0x5A, sizeof(snapshot));
    snapshot.eip = 0x1234;
    snapshot.eflags = 0x202;
    snapshot.cpu.stack.esp = 0xBFFFFF00;
    snapshot.page_directory_virtual_addr = &test_directories[4];
    CHECK(scheduler_select_next_process() == waiting);
    scheduler_save_context_to_current_running_pcb(snapshot);
    CHECK(memcmp(&running->context, &snapshot, sizeof(snapshot)) == 0);
    CHECK(memcmp(&waiting->context, &waiting_before, sizeof(waiting_before)) == 0);
    CHECK(running->metadata.state == PROCESS_STATE_RUNNING);
    CHECK(process_manager_state.active_process_count == 2);
}

static void save_without_running_process_leaves_table_unchanged(void) {
    reset_fixture();
    make_process(2, PROCESS_STATE_READY);
    struct ProcessControlBlock before[PROCESS_COUNT_MAX];
    memcpy(before, _process_list, sizeof(before));
    struct Context snapshot;
    memset(&snapshot, 0x5A, sizeof(snapshot));
    scheduler_save_context_to_current_running_pcb(snapshot);
    CHECK(memcmp(before, _process_list, sizeof(before)) == 0);
}

#define RUN(test) do { test(); report("PASS: " #test "\n"); } while (0)

__attribute__((noreturn)) void _start(void) {
    RUN(empty_table_returns_null);
    RUN(single_ready_process_keeps_its_turn);
    RUN(three_processes_take_turns);
    RUN(inactive_slots_are_skipped);
    RUN(last_slot_wraps_to_first);
    RUN(running_process_can_be_selected_again);
    RUN(incomplete_pcbs_are_not_selected);
    RUN(selection_observes_state_changes);
    RUN(init_resets_cursor_without_erasing_processes);
    RUN(saved_context_belongs_to_running_process);
    RUN(save_without_running_process_leaves_table_unchanged);
    exit_test(0);
}
