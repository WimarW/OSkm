#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/text/framebuffer.h"
#include "header/stdlib/string.h"
#include "header/cpu/portio.h"

// Resolusi 80x25
#define FRAMEBUFFER_WIDTH 80
#define FRAMEBUFFER_HEIGHT 25

// void framebuffer_set_cursor(uint8_t r, uint8_t c) {

// }

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

// void framebuffer_clear(void) {
//     // TODO : Implement
// }
