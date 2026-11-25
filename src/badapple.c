#include <stdint.h>
#include <stdbool.h>

struct ReadRequest {
    void    *buf;
    char    *name;
    uint8_t  name_len;
    uint32_t parent_inode;
    uint32_t buffer_size;
    bool     is_directory;
} __attribute__((packed));

#define COLS        80
#define ROWS        25
#define FRAME_BYTES 500
#define DATA_MAX    160000

static uint8_t data[DATA_MAX];
static char    screen[COLS * ROWS];

static void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile(
        "int $0x30"
        :
        : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx)
        : "memory"
    );
}

static char sys_getc(void) {
    char c = 0;
    syscall(4, (uint32_t)&c, 0, 0);
    return c;
}

static uint32_t sys_ticks(void) {
    uint32_t t = 0;
    syscall(15, (uint32_t)&t, 0, 0);
    return t;
}

static void sys_draw(const char *buf, uint32_t color) {
    syscall(16, (uint32_t)buf, color, 0);
}

static void sys_clear(void) {
    syscall(8, 0, 0, 0);
}

static void sys_print(const char *s) {
    uint32_t len = 0;
    while (s[len] != '\0') len++;
    syscall(6, (uint32_t)s, len, 0xF);
}

int main(void) {
    int8_t ret = -1;
    struct ReadRequest req = {
        .buf          = data,
        .name         = "badapple.bin",
        .name_len     = 12,
        .parent_inode = 1,
        .buffer_size  = DATA_MAX,
        .is_directory = false,
    };
    syscall(0, (uint32_t)&req, (uint32_t)&ret, 0);
    if (ret != 0) {
        sys_print("badapple: cannot load badapple.bin\n");
        return 1;
    }

    uint32_t count = *(uint32_t *)data;
    uint32_t fps   = *(uint32_t *)(data + 4);
    const uint8_t *frames = data + 8;

    uint32_t start = sys_ticks();
    uint32_t shown = 0xFFFFFFFFu;

    while (true) {
        char k = sys_getc();
        if (k == 0x1B || k == 0x03) break;
        uint32_t frame = (sys_ticks() - start) * fps / 1000u;
        if (frame >= count) break;
        if (frame == shown) continue;
        shown = frame;

        const uint8_t *fr = frames + frame * FRAME_BYTES;
        for (uint32_t row = 0; row < ROWS; row++) {
            for (uint32_t col = 0; col < COLS; col++) {
                uint32_t top = (row * 2) * COLS + col;
                uint32_t bot = (row * 2 + 1) * COLS + col;
                bool t = (fr[top / 8] & (1u << (top % 8))) != 0;
                bool b = (fr[bot / 8] & (1u << (bot % 8))) != 0;
                screen[row * COLS + col] = t ? (b ? (char)0xDB : (char)0xDF)
                                             : (b ? (char)0xDC : ' ');
            }
        }
        sys_draw(screen, 0xF);
    }

    sys_clear();
    syscall(6, (uint32_t)"user@os:", 8, 0x0A);
    syscall(6, (uint32_t)"/",        1, 0x0B);
    syscall(6, (uint32_t)"$ ",       2, 0x0A);
    return 0;
}