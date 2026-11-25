#include <stdint.h>

static void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile(
        "int $0x30"
        :
        : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx)
        : "memory"
    );
}

int main(void) {
    const char *msg = "Hello World!\n";
    uint32_t len = 0;
    while (msg[len] != '\0') len++;
    syscall(6, (uint32_t)msg, len, 0xF);
    return 0;
}