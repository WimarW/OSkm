#include <stdint.h>
#include <stdbool.h>
#include "header/stdlib/string.h"
#include "header/cpu/gdt.h"
#include "header/cpu/idt.h"
#include "header/cpu/interrupt.h"
#include "header/kernel-entrypoint.h"
#include "header/text/framebuffer.h"
#include "header/driver/keyboard.h"
#include "header/driver/disk.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h"
#include "header/memory/swap.h"
#include "header/process/process.h"
#include "header/process/scheduler.h"
#include "header/text/loadscreen.h"
#include "header/driver/cmos.h"


void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    framebuffer_clear();

    splash_hide_cursor();
    splash_draw_nairo();
    splash_draw_progress(20, "Initializing interrupts...");
    splash_delay(150000000);

    initialize_filesystem_ext2();
    splash_draw_progress(50, "Mounted EXT2 filesystem");
    splash_delay(150000000);

    gdt_install_tss();
    set_tss_register();
    splash_draw_progress(75, "Configured TSS & memory");
    splash_delay(150000000);

    // Write shell into memory
    struct EXT2DriverRequest request = {
        .buf          = (uint8_t*) 0,
        .name         = "shell",
        .parent_inode = 1,
        .buffer_size  = 0x100000,
        .name_len     = 5,
    };

    // Set the kernel interrupt stack before starting the first user process.
    set_tss_kernel_current_stack();
    if (process_create_user_process(request) == PROCESS_CREATE_SUCCESS) {
        splash_draw_progress(100, "Starting NAIRO shell...");
        splash_delay(300000000);
        framebuffer_clear();
        framebuffer_set_cursor(0, 0);
        
        __asm__ volatile("cli" : : : "memory");
        scheduler_init();
        scheduler_switch_to_next_process();
    }

    while (true);
}

