BITS 64

section .text.entry
global _start
extern kmain

; Entered from stage3 with:
;   RDI = physical address of struct boot_info
;   paging = stage3 layout (identity + direct map + kernel high half)
_start:
    cli
    cld
    lea rsp, [rel boot_stack_top]
    xor rbp, rbp

    ; Convert the physical boot_info pointer through the direct map.
    mov rax, 0xFFFF800000000000
    add rdi, rax

    call kmain

.halt:
    cli
    hlt
    jmp .halt

section .bss
global boot_stack_top
align 16
boot_stack:
    resb 16384
boot_stack_top:
