#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include "header/process/process.h"
#include "header/cpu/gdt.h"

/* The assembly layout in context-switch.s is an i386 ABI contract. Stop the
 * build if the C struct or fixed user selectors no longer match that layout.
 */
_Static_assert(sizeof(struct CPURegister) == 48, "CPURegister size");
_Static_assert(sizeof(struct Context) == 60, "Context size");
_Static_assert(offsetof(struct Context, cpu.index.edi) == 0, "Context EDI offset");
_Static_assert(offsetof(struct Context, cpu.index.esi) == 4, "Context ESI offset");
_Static_assert(offsetof(struct Context, cpu.stack.ebp) == 8, "Context EBP offset");
_Static_assert(offsetof(struct Context, cpu.stack.esp) == 12, "Context ESP offset");
_Static_assert(offsetof(struct Context, cpu.general.ebx) == 16, "Context EBX offset");
_Static_assert(offsetof(struct Context, cpu.general.edx) == 20, "Context EDX offset");
_Static_assert(offsetof(struct Context, cpu.general.ecx) == 24, "Context ECX offset");
_Static_assert(offsetof(struct Context, cpu.general.eax) == 28, "Context EAX offset");
_Static_assert(offsetof(struct Context, cpu.segment.gs) == 32, "Context GS offset");
_Static_assert(offsetof(struct Context, cpu.segment.fs) == 36, "Context FS offset");
_Static_assert(offsetof(struct Context, cpu.segment.es) == 40, "Context ES offset");
_Static_assert(offsetof(struct Context, cpu.segment.ds) == 44, "Context DS offset");
_Static_assert(offsetof(struct Context, eip) == 48, "Context EIP offset");
_Static_assert(offsetof(struct Context, eflags) == 52, "Context EFLAGS offset");
_Static_assert(offsetof(struct Context, page_directory_virtual_addr) == 56, "Context PD offset");
_Static_assert((GDT_USER_CODE_SEGMENT_SELECTOR | 3u) == 0x1B, "User code selector");
_Static_assert((GDT_USER_DATA_SEGMENT_SELECTOR | 3u) == 0x23, "User data selector");

/**
 * Restore a user context by value and transfer to ring 3 using iret; never returns.
 * The caller must activate ctx's page directory before invoking this routine.
 */
__attribute__((noreturn)) extern void process_context_switch(struct Context ctx);

/**
 * Reset the Round Robin cursor and activate the timer. Existing PCBs are kept.
 * Call once at startup with interrupts disabled, before the first user process.
 */
void scheduler_init(void);

/** Copy a snapshot into the RUNNING process's PCB; do nothing if none is running. */
void scheduler_save_context_to_current_running_pcb(struct Context ctx);

/**
 * Select the next runnable PCB and advance the Round Robin cursor.
 * READY and RUNNING processes can be chosen; inactive/incomplete PCBs are skipped.
 * Returns NULL if no runnable PCB exists. Does not change process states, CR3,
 * or CPU registers: the context-switch caller performs those operations later.
 * Scheduler operations must be serialized (interrupts disabled during switching).
 */
struct ProcessControlBlock *scheduler_select_next_process(void);

/**
 * Select a process, update READY/RUNNING states, activate its address space and
 * restore its context. Disables interrupts until iret restores the target flags.
 * The caller saves any interrupted process context and ACKs any IRQ beforehand.
 * If no process is runnable, idle in kernel mode with hardware interrupts enabled.
 */
__attribute__((noreturn)) void scheduler_switch_to_next_process(void);

#endif
