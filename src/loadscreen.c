#include <stdint.h>
#include <stddef.h>
#include "header/text/framebuffer.h"
#include "header/cpu/portio.h"
#include "header/text/loadscreen.h"

static uint8_t splash_color_from_key(char key) {
    switch (key) {
        case 'C': return 0x0B; // Light Cyan (glowing crystal body)
        case 'c': return 0x03; // Cyan (outer facets / cheek fins)
        case 'Y': return 0x0E; // Yellow (face mask & floating limbs)
        case 'M': return 0x0D; // Light Magenta (happy eyes & horn highlight)
        case 'm': return 0x05; // Magenta (horn shadow)
        case 'W': return 0x0F; // Bright White
        default:  return 0x07;
    }
}

void splash_hide_cursor(void) {
    out(CURSOR_PORT_CMD, 0x0A);
    out(CURSOR_PORT_DATA, 0x20); // Bit 5 disables VGA hardware cursor
}

static void splash_draw_text(uint8_t row, uint8_t col, const char *str, uint8_t fg, uint8_t bg) {
    if (row >= FRAMEBUFFER_HEIGHT) return;
    for (uint8_t i = 0; str[i] != '\0' && (col + i) < FRAMEBUFFER_WIDTH; i++) {
        framebuffer_write(row, col + i, str[i], fg, bg);
    }
}

void splash_draw_nairo(void) {
    // 4 rows x 37 cols
    static const char *title[] = {
        " _   _     _     ___   ____     ___  ",
        "| \\ | |   / \\   |_ _| |  _ \\   / _ \\ ",
        "|  \\| |  / _ \\   | |  | |_) | | | | |",
        "|_| \\_| /_/ \\_\\ |___| |_| \\_\\  \\___/ ",
    };

    // 13 rows x 32 cols
    static const char *mascot_art[] = {
        "                _..--'          ",
        "             .-'   .'           ",
        "          .-'     /        /|   ",
        "        .'       |        / /   ",
        "      .'         `----.  |/     ",
        "     /  .---.   .---.  \\        ",
        "    <  / /^\\ \\_/ /^\\ \\  >       ",
        "     \\ |             | /        ",
        "      \\ `---./ \\.---' /         ",
        "  /\\   `.           .'    |\\    ",
        " <  >    `._     _.'      | \\   ",
        "  \\/     |\\ `._.' /|      \\  \\  ",
        "         \\/       \\/       `-'  ",
    };

    static const char *mascot_col[] = {
        "                CCCCCC          ",
        "             CCC   CC           ",
        "          CCC     C        Mm   ",
        "        CC       C        M m   ",
        "      CC         CCCCCC  Mm     ",
        "     c  YYYYY   YYYYY  c        ",
        "    c  Y MMM YYY MMM Y  c       ",
        "     c Y             Y c        ",
        "      C YYYYYY YYYYYY C         ",
        "  YY   CC           CC    YY    ",
        " Y  C    CCC     CCC      Y Y   ",
        "  CC     YY CCCCC YY      Y  C  ",
        "         CC       CC       CCC  ",
    };

    const uint8_t title_col = (FRAMEBUFFER_WIDTH - 37) / 2;
    for (size_t r = 0; r < 4; r++) {
        uint8_t fg = (r < 2) ? 0x0B : 0x0F; // Cyan-to-white vertical gradient
        splash_draw_text(1 + r, title_col, title[r], fg, 0x00);
    }

    const uint8_t mascot_row = 6;
    const uint8_t mascot_col_offset = (FRAMEBUFFER_WIDTH - 32) / 2;
    for (size_t r = 0; r < 13; r++) {
        for (uint8_t c = 0; mascot_art[r][c] != '\0'; c++) {
            char ch = mascot_art[r][c];
            if (ch == ' ') continue;
            uint8_t fg = splash_color_from_key(mascot_col[r][c]);
            framebuffer_write(mascot_row + r, mascot_col_offset + c, ch, fg, 0x00);
        }
    }
}

void splash_draw_progress(uint8_t percent, const char *status) {
    const uint8_t bar_width = 36;
    const uint8_t bar_col = (FRAMEBUFFER_WIDTH - (bar_width + 2)) / 2;
    uint8_t filled = (uint8_t)((percent * bar_width) / 100);

    // Clear status line (row 20) and write centered message
    for (uint8_t c = 0; c < FRAMEBUFFER_WIDTH; c++) {
        framebuffer_write(20, c, ' ', 0x07, 0x00);
    }
    uint8_t len = 0;
    while (status[len] != '\0') len++;
    splash_draw_text(20, (FRAMEBUFFER_WIDTH - len) / 2, status, 0x0F, 0x00);

    // Draw CP437 progress bar (row 21)
    framebuffer_write(21, bar_col, '[', 0x0F, 0x00);
    for (uint8_t i = 0; i < bar_width; i++) {
        if (i < filled) {
            framebuffer_write(21, bar_col + 1 + i, '\xDB', 0x0B, 0x00); // solid cyan block
        } else {
            framebuffer_write(21, bar_col + 1 + i, '\xB0', 0x08, 0x00); // shaded gray block
        }
    }
    framebuffer_write(21, bar_col + 1 + bar_width, ']', 0x0F, 0x00);
}

void splash_delay(uint32_t loops) {
    for (volatile uint32_t i = 0; i < loops; i++) {
        __asm__ volatile("nop");
    }
}

void splash_show_loading_screen(void) {
    framebuffer_clear();
    splash_hide_cursor();
    splash_draw_nairo();

    const char *steps[] = {
        "Initializing kernel...",
        "Mounting EXT2 filesystem...",
        "Setting up paging & TSS...",
        "Starting NAIRO shell...",
    };

    for (uint8_t pct = 0; pct <= 100; pct += 5) {
        uint8_t step_idx = pct / 26; // maps 0..100 -> 0..3
        splash_draw_progress(pct, steps[step_idx]);
        splash_delay(4000000);       // adjust based on your QEMU speed
    }

    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
}