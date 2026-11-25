#include <stdint.h>
#include <stdbool.h>

struct CMOSTime {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

#define SCREEN_ROWS 25
#define SCREEN_COLS 80
#define CLOCK_COLOR 0xF

static void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile(
        "int $0x30"
        :
        : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx)
        : "memory"
    );
}

static void sys_get_time(struct CMOSTime *t) {
    syscall(9, (uint32_t)t, 0, 0);
}

static void sys_putc_at(uint32_t row, uint32_t col, char c, uint32_t color) {
    syscall(14, (row << 16) | col, (uint32_t)(uint8_t)c, color);
}

int main(void) {
    uint8_t last_second = 0xFF;

    while (true) {
        struct CMOSTime t;
        sys_get_time(&t);

        if (t.second == last_second) continue;
        last_second = t.second;

        char s[8] = {
            (char)('0' + t.hour / 10),   (char)('0' + t.hour % 10),   ':',
            (char)('0' + t.minute / 10), (char)('0' + t.minute % 10), ':',
            (char)('0' + t.second / 10), (char)('0' + t.second % 10),
        };
        for (uint32_t i = 0; i < 8; i++) {
            sys_putc_at(SCREEN_ROWS - 1, SCREEN_COLS - 8 + i, s[i], CLOCK_COLOR);
        }
    }
}