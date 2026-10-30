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
<<<<<<< HEAD
=======
#include "header/memory/swap.h"
>>>>>>> bee075ee230fcedb86cbaf7a543368651a341cee

void test_ext2_integration(void) {
    // 1. Read test
    char read_buf[512];
    struct EXT2DriverRequest read_req = {
        .buf = read_buf,
        .name = "laur",
        .name_len = 4,
        .parent_inode = 1,
        .buffer_size = sizeof(read_buf),
        .is_directory = false
    };
    __attribute__((unused)) int8_t read_ret = read(read_req);

    // 2. Write test
    char test_data[] = "Integration test payload";
    struct EXT2DriverRequest write_req = {
        .buf = test_data,
        .name = "test.txt",
        .name_len = 8,
        .parent_inode = 1,
        .buffer_size = sizeof(test_data),
        .is_directory = false
    };
    __attribute__((unused)) int8_t write_ret = write(&write_req);

    // 3. Delete test
    struct EXT2DriverRequest del_req = {
        .name = "test.txt",
        .name_len = 8,
        .parent_inode = 1,
        .is_directory = false
    };
    __attribute__((unused)) int8_t del_ret = delete(del_req);
}

void run_kernel_disk_tests(void) {
    initialize_filesystem_ext2();

    // 1. Edge Case: Boundary File (2047 Bytes)
    static uint8_t buf_2047[2047];
    for (int i = 0; i < 2047; i++) buf_2047[i] = (uint8_t)(0xA0 + (i % 16));

    struct EXT2DriverRequest req1 = {
        .buf = buf_2047,
        .name = "edge2047",
        .name_len = 8,
        .parent_inode = 2, // Root directory inode
        .buffer_size = 2047
    };
    write(&req1);

    static uint8_t buf_indirect[24577];
    for (int i = 0; i < 24577; i++) buf_indirect[i] = (uint8_t)(i & 0xFF);

    struct EXT2DriverRequest req2 = {
        .buf = buf_indirect,
        .name = "ind24577",
        .name_len = 8,
        .parent_inode = 2,
        .buffer_size = 24577
    };
    write(&req2);
}


void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
<<<<<<< HEAD
    // keyboard_state_activate();
    // keyboard_configuration();
    initialize_filesystem_ext2();
    gdt_install_tss();
    set_tss_register();

    // Allocate first 4 MiB virtual memory
    for (uint32_t addr = 0; addr < 0x400000; addr += PAGE_FRAME_SIZE) {
        paging_allocate_user_page_frame(&_paging_kernel_page_directory, (uint8_t*) addr);
    }

    // Write shell into memory
    struct EXT2DriverRequest request = {
        .buf          = (uint8_t*) 0,
        .name         = "shell",
        .parent_inode = 1,
        .buffer_size  = 0x100000,
        .name_len     = 5,
    };
    read(request);

    // Set TSS $esp pointer and jump into shell
    set_tss_kernel_current_stack();
    kernel_execute_user_program((uint8_t*) 0);
=======
    keyboard_state_activate();

    keyboard_configuration();
>>>>>>> bee075ee230fcedb86cbaf7a543368651a341cee

    while (true);
}

