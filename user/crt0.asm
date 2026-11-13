; ---------------------------------------------------------------------------
; Zenku user crt0
;
; The kernel enters user code with argc in rdi and argv in rsi.
; ---------------------------------------------------------------------------

BITS 64

section .text.entry

global _start
extern main

_start:
    xor ebp, ebp
    and rsp, -16
    call main

    mov rdi, rax
    mov rax, 60                 ; exit(status)
    syscall

.hang:
    hlt
    jmp .hang
