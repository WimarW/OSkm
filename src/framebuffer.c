#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/text/framebuffer.h"
#include "header/stdlib/string.h"
#include "header/cpu/portio.h"

#define SHELL_HEIGHT (FRAMEBUFFER_HEIGHT - 1) // rows 0..23 for shell, row 24 reserved for clock

static uint8_t cursor_row = 0;
static uint8_t cursor_col = 0;

void framebuffer_set_cursor(uint8_t r, uint8_t c) {
    // 0x3D4 --> Index Register I/o Port
    // 0x3D5 --> Data Register I/O Port
    uint16_t pos = r * FRAMEBUFFER_WIDTH + c;
    
    // Enable Cursor
    // Start Register
    out(0x3D4, 0x0A);
    out(0x3D5, (in(0x3D5) & 0xC0) | 14); // Change 14 to 0 if Want rectangle cursor

    // End Register
    out(0x3D4, 0x0B);
    out(0x3D5, (in(0x3D5) & 0xE0) | 15);

    // Cursor Positioning
    // Cursor Location Low Byte
    out(0x3D4, 0x0F);
    out(0x3D5, (uint8_t) (pos & 0xFF));

    // Cursor Location High Byte
    out(0x3D4, 0x0E);
    out(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));
}


void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    //1 vga cel = 2B --> Multiply Index calculation by 2
    uint16_t index = (row * FRAMEBUFFER_WIDTH + col) * 2;
    
    // 4 Bit Kiri --> bg
    // 4 Bit Kanan --> fg
    uint8_t color = (bg << 4) | (fg & 0x0F);
    // Byte 1 Stores Char
    // Byte 2 Stores Color
    FRAMEBUFFER_MEMORY_OFFSET[index] = c;
    FRAMEBUFFER_MEMORY_OFFSET[index+1] = color;

}

void framebuffer_clear(void) {
    size_t total_bytes = FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT * 2;
    memset(FRAMEBUFFER_MEMORY_OFFSET, 0x00, total_bytes);
    cursor_row = 0;
    cursor_col = 0;
}

char framebuffer_read(uint8_t row, uint8_t col) {
    uint16_t index = (row * FRAMEBUFFER_WIDTH + col) * 2;
    uint8_t *fb = (uint8_t *) FRAMEBUFFER_MEMORY_OFFSET;
    
    return fb[index];
}

void framebuffer_scroll(void) {
    size_t line_bytes = FRAMEBUFFER_WIDTH * 2;
    for (uint8_t r = 1; r < SHELL_HEIGHT; r++) {
        memcpy(
            (void*)(FRAMEBUFFER_MEMORY_OFFSET + (r - 1) * line_bytes),
            (void*)(FRAMEBUFFER_MEMORY_OFFSET + r * line_bytes),
            line_bytes
        );
    }
    for (uint8_t c = 0; c < FRAMEBUFFER_WIDTH; c++) {
        framebuffer_write(SHELL_HEIGHT - 1, c, ' ', 0x07, 0x00);
    }
}

void framebuffer_putchar(char c, uint32_t color) {
    uint8_t fg = color & 0x0F;
    uint8_t bg = (color >> 4) & 0x0F;

    if (c == '\n'){
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\r'){
        cursor_col = 0;
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
        }
    } else {
        framebuffer_write(cursor_row, cursor_col, c, fg, bg);
        cursor_col++;
    }

    if (cursor_col >= FRAMEBUFFER_WIDTH) {
        cursor_col = 0;
        cursor_row++;
    }

    while (cursor_row >= SHELL_HEIGHT) {
        framebuffer_scroll();
        cursor_row = SHELL_HEIGHT - 1;
    }

    framebuffer_set_cursor(cursor_row, cursor_col);
}

void framebuffer_puts(const char *str, size_t count, uint32_t color) {
    for (size_t i = 0; i < count; i++) {
        framebuffer_putchar(str[i], color);
    }
}