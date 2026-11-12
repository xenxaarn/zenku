; ---------------------------------------------------------------------------
; GDT/TSS load helpers (x86_64)
; ---------------------------------------------------------------------------

BITS 64

section .text

; void gdt_load(const struct gdt_ptr *gdtr)
global gdt_load
gdt_load:
    lgdt [rdi]

    ; Reload CS with a far return through the new kernel code selector.
    push qword 0x08
    lea rax, [rel .flush]
    push rax
    retfq

.flush:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

; void tss_load(uint16_t selector)
global tss_load
tss_load:
    xor eax, eax
    mov ax, di
    ltr ax
    ret
