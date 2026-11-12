; ---------------------------------------------------------------------------
; Zenku stage3 entry (64-bit C loader)
;
; Entered from stage2 in long mode with identity paging for the first 1 GiB.
; Sets up a stack, clears .bss, and calls into C.
; ---------------------------------------------------------------------------

BITS 64

section .text.entry
global _start
extern stage3_main
extern __bss_start
extern __bss_end

_start:
    cli
    cld
    mov rsp, 0x1F000
    xor rbp, rbp

    ; Clear .bss (the flat binary only contains initialized sections).
    mov rdi, __bss_start
    mov rcx, __bss_end
    sub rcx, rdi
    xor eax, eax
    rep stosb

    ; RDI = physical boot_params block left by stage2.
    mov rdi, 0x6000
    call stage3_main

.halt:
    cli
    hlt
    jmp .halt

; ---------------------------------------------------------------------------
; stage3_jump_to_kernel(uint64_t entry, uint64_t boot_info_phys)
;
; Installs no new state beyond flushing the TLB; stage3 has already added the
; higher-half and direct-map entries to the active PML4.
; ---------------------------------------------------------------------------
global stage3_jump_to_kernel
stage3_jump_to_kernel:
    mov rax, rdi
    mov rdi, rsi

    mov rcx, cr3
    mov cr3, rcx

    jmp rax

.halt2:
    cli
    hlt
    jmp .halt2
