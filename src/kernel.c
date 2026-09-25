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
    
    framebuffer_clear();
    int row = 0, col = 0;
    
    int row_lengths[FRAMEBUFFER_HEIGHT] = {0}; 
    int max_row = 0; 
    
    framebuffer_set_cursor(row, col);
    keyboard_state_activate();
    
    while (true) {
        char c;
        get_keyboard_buffer(&c);
        if (c) {
            if (c == '\b') {
                if (col > 0) {
                    col--;
                    for (int c_idx = col; c_idx < row_lengths[row] - 1; c_idx++) {
                        char next_c = framebuffer_read(row, c_idx + 1);
                        framebuffer_write(row, c_idx, next_c, 0xF, 0);
                    }
                    framebuffer_write(row, row_lengths[row] - 1, ' ', 0xF, 0);
                    row_lengths[row]--;
                } 
                else if (row > 0) {
                    int target_row = row - 1;
                    int target_col = row_lengths[target_row];
                    int curr_len = row_lengths[row];

                    if (target_col + curr_len <= FRAMEBUFFER_WIDTH) {
                        for (int i = 0; i < curr_len; i++) {
                            char ch = framebuffer_read(row, i);
                            framebuffer_write(target_row, target_col + i, ch, 0xF, 0);
                        }
                        row_lengths[target_row] += curr_len;

                        for (int r = row; r < FRAMEBUFFER_HEIGHT - 1; r++) {
                            int len_below = row_lengths[r + 1];
                            for (int c_idx = 0; c_idx < len_below; c_idx++) {
                                char char_below = framebuffer_read(r + 1, c_idx);
                                framebuffer_write(r, c_idx, char_below, 0xF, 0);
                            }
                            for (int c_idx = len_below; c_idx < FRAMEBUFFER_WIDTH; c_idx++) {
                                framebuffer_write(r, c_idx, ' ', 0xF, 0);
                            }
                            row_lengths[r] = len_below;
                        }

                        for (int c_idx = 0; c_idx < FRAMEBUFFER_WIDTH; c_idx++) {
                            framebuffer_write(FRAMEBUFFER_HEIGHT - 1, c_idx, ' ', 0xF, 0);
                        }
                        row_lengths[FRAMEBUFFER_HEIGHT - 1] = 0;

                        row = target_row;
                        col = target_col;
                        
                        if (max_row > 0) max_row--;
                    }
                }
            } else if (c == '\n') {
                if (row < FRAMEBUFFER_HEIGHT - 1) {
                    for (int r = FRAMEBUFFER_HEIGHT - 1; r > row + 1; r--) {
                        int len_above = row_lengths[r - 1];
                        for (int c_idx = 0; c_idx < len_above; c_idx++) {
                            char char_above = framebuffer_read(r - 1, c_idx);
                            framebuffer_write(r, c_idx, char_above, 0xF, 0);
                        }
                        for (int c_idx = len_above; c_idx < FRAMEBUFFER_WIDTH; c_idx++) {
                            framebuffer_write(r, c_idx, ' ', 0xF, 0);
                        }
                        row_lengths[r] = len_above;
                    }

                    int dest_c = 0;
                    for (int src_c = col; src_c < row_lengths[row]; src_c++) {
                        char char_to_move = framebuffer_read(row, src_c);
                        framebuffer_write(row + 1, dest_c, char_to_move, 0xF, 0);
                        dest_c++;
                    }
                    for (int c_clear = dest_c; c_clear < FRAMEBUFFER_WIDTH; c_clear++) {
                        framebuffer_write(row + 1, c_clear, ' ', 0xF, 0);
                    }
                    row_lengths[row + 1] = dest_c;

                    for (int c_clear = col; c_clear < row_lengths[row]; c_clear++) {
                        framebuffer_write(row, c_clear, ' ', 0xF, 0);
                    }
                    row_lengths[row] = col;

                    row++;
                    col = 0;
                    
                    if (max_row < FRAMEBUFFER_HEIGHT - 1) max_row++;
                } else {
                    for (int c_clear = col; c_clear < row_lengths[row]; c_clear++) {
                        framebuffer_write(row, c_clear, ' ', 0xF, 0);
                    }
                    row_lengths[row] = col;
                    col = 0;
                }
            } else if ((uint8_t)c == EXT_SCANCODE_LEFT) {
                if (col > 0) {
                    col--;
                } else if (row > 0) {
                    row--;
                    col = row_lengths[row];
                }
            } else if ((uint8_t)c == EXT_SCANCODE_RIGHT) {
                if (col < row_lengths[row]) {
                    col++;
                } 
                else if (row < max_row) {
                    row++;
                    col = 0;
                }
            } else if ((uint8_t)c == EXT_SCANCODE_UP) {
                if (row > 0) {
                    row--;
                    if (col > row_lengths[row]) {
                        col = row_lengths[row];
                    }
                }
            } else if ((uint8_t)c == EXT_SCANCODE_DOWN) {
                if (row < max_row) {
                    row++;
                    if (col > row_lengths[row]) {
                        col = row_lengths[row];
                    }
                }
            } else {
                framebuffer_write(row, col, c, 0xF, 0);
                col++;
                
                if (col > row_lengths[row]) {
                    row_lengths[row] = col;
                }
                
                if (col >= FRAMEBUFFER_WIDTH) {
                    if (row < FRAMEBUFFER_HEIGHT - 1) {
                        col = 0;
                        row++;
                        if (row > max_row) max_row = row;
                    } else {
                        col = FRAMEBUFFER_WIDTH - 1;
                    }
                }
            }
            framebuffer_set_cursor(row, col);
        }
    }

    __asm__("int $0x4");
    while (true);
}