global _start
extern main

section .text
_start:
    call main
    mov eax, 14
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    int 0x30
    jmp $