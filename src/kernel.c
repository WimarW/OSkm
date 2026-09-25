#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/cpu/idt.h"
#include "header/cpu/interrupt.h"
#include "header/kernel-entrypoint.h"
#include "header/text/framebuffer.h"
#include "header/driver/keyboard.h"

void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    // Framebuffer Test
    framebuffer_clear();
    // framebuffer_write(3, 8,  'H', 0, 0xF);
    // framebuffer_write(3, 9,  'a', 0, 0xF);
    // framebuffer_write(3, 10, 'i', 0, 0xF);
    // framebuffer_write(3, 11, '!', 0, 0xF);
    int row = 0, col = 0;
    framebuffer_set_cursor(row, col);
    keyboard_state_activate();
    while (true) {
        char c;
        get_keyboard_buffer(&c);
        if (c) {
            if (c == '\b') {
                if (col > 0) {
                    col--;
                    framebuffer_write(row, col, ' ', 0xF, 0);
                } else if (row > 0) {
                    row--;
                    col = FRAMEBUFFER_WIDTH - 1;
                    framebuffer_write(row, col, ' ', 0xF, 0);
                }
            } else if (c == '\n') {
                if (row < FRAMEBUFFER_HEIGHT - 1) {
                    row++;
                }
                col = 0;
            } else if ((uint8_t)c == EXT_SCANCODE_LEFT) {
                if (col > 0) {
                    col--;
                } else if (row > 0) {
                    row--;
                    col = FRAMEBUFFER_WIDTH - 1;
                }
            }else if( c == '\n'){
                row++;
                col = 0;
            }else if((uint8_t) c == EXT_SCANCODE_RIGHT && col < FRAMEBUFFER_WIDTH  ){
                if(col == FRAMEBUFFER_WIDTH - 1){
                    row++;
                    col = 0;
                }else{
                    col++;
                }
            }else if((uint8_t) c == EXT_SCANCODE_LEFT && col > 0){
                if(col == 1){
                    row--;
                    col = FRAMEBUFFER_WIDTH;
                }else{
                    col--;
                }
            }else{
                framebuffer_write(row, col, c, 0xF, 0);
                if(col > FRAMEBUFFER_WIDTH -1){
                    row++;
                    col = 0;
                }else{
                    col++;
                }
            }
           framebuffer_set_cursor(row, col);
        }
    }


    // Interrupt Test
    __asm__("int $0x4");

    while (true);
}

