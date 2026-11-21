bits 32
global process_context_switch

; Must match Context in process.h. scheduler.h checks these C offsets at build
; time. struc defines offsets only; it does not allocate a context in memory.
struc Context
    .edi:            resd 1
    .esi:            resd 1
    .ebp:            resd 1
    .esp:            resd 1
    .ebx:            resd 1
    .edx:            resd 1
    .ecx:            resd 1
    .eax:            resd 1
    .gs:             resd 1
    .fs:             resd 1
    .es:             resd 1
    .ds:             resd 1
    .eip:            resd 1
    .eflags:         resd 1
    .page_directory: resd 1
endstruc

; Fixed user-mode selectors from gdt.h, also checked in scheduler.h.
USER_CODE_SELECTOR equ 0x1B
USER_DATA_SELECTOR equ 0x23

section .text
process_context_switch:
    cli
    ; i386 cdecl passes Context BY VALUE: [esp] is the return address, and
    ; [esp+4] begins the struct itself. It is not a pointer argument to load.
    lea eax, [esp+4]

    ; Build a ring-0 -> ring-3 iret frame on the current kernel stack.
    ; At iret: [esp] EIP, [esp+4] CS, [esp+8] EFLAGS,
    ;          [esp+12] user ESP, [esp+16] user SS.
    push dword USER_DATA_SELECTOR
    push dword [ss:eax + Context.esp]
    push dword [ss:eax + Context.eflags]
    push dword USER_CODE_SELECTOR
    push dword [ss:eax + Context.eip]

    ; SS still refers to the kernel stack segment until iret. Use it explicitly
    ; for context reads: restoring DS may install a valid null data selector,
    ; which cannot be used to read the remaining context through DS.
    mov ds, word [ss:eax + Context.ds]
    mov es, word [ss:eax + Context.es]
    mov fs, word [ss:eax + Context.fs]
    mov gs, word [ss:eax + Context.gs]

    mov edi, [ss:eax + Context.edi]
    mov esi, [ss:eax + Context.esi]
    mov ebp, [ss:eax + Context.ebp]
    mov ebx, [ss:eax + Context.ebx]
    mov edx, [ss:eax + Context.edx]
    mov ecx, [ss:eax + Context.ecx]
    ; Restore EAX last because it holds the context address for all prior reads.
    mov eax, [ss:eax + Context.eax]

    ; iret restores EIP/EFLAGS and the user SS/ESP atomically. The C caller
    ; selects the page directory beforehand; this routine does not change CR3.
    iret

section .note.GNU-stack noalloc noexec nowrite progbits
