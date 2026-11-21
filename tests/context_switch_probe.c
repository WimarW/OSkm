/* Linked into the temporary test kernel only, never the normal OS build.
 * GDB redirects the context-switch entry here after process/TSS/paging setup.
 * The C compiler must pass this struct by value using the real i386 ABI.
 */
#include "header/process/scheduler.h"

uint32_t context_switch_test_flags = 0x247;
uint32_t context_switch_test_null_segments = 0;
uint32_t context_switch_test_eip = 0;

__attribute__((noreturn)) void context_switch_test_enter(void) {
    struct Context ctx = _process_list[0].context;
    ctx.cpu.general.eax = 0x11111111;
    ctx.cpu.general.ebx = 0x22222222;
    ctx.cpu.general.ecx = 0x33333333;
    ctx.cpu.general.edx = 0x44444444;
    ctx.cpu.stack.ebp = 0x55555555;
    ctx.cpu.index.esi = 0x66666666;
    ctx.cpu.index.edi = 0x77777777;
    ctx.cpu.stack.esp -= 0x100; /* Still inside the allocated 64 KiB user stack. */
    ctx.eflags = context_switch_test_flags;
    ctx.eip = context_switch_test_eip;
    if (context_switch_test_null_segments) {
        ctx.cpu.segment.ds = 0;
        ctx.cpu.segment.es = 0;
        ctx.cpu.segment.fs = 0;
        ctx.cpu.segment.gs = 0;
    }
    process_context_switch(ctx);
}
