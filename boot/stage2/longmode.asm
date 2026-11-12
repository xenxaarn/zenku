; ---------------------------------------------------------------------------
; Zenku stage2 long-mode trampoline
;
; Builds identity page tables for the first 1 GiB at 0x1000..0x3FFF, enters
; IA-32e mode, and transfers control to stage3 at 0x20000.
; ---------------------------------------------------------------------------

s2_enter_long_mode:
    cli
    db 0x66
    lgdt [gdt_pointer]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump to the 32-bit code selector.
    db 0x66, 0xEA
    dd pm32_entry
    dw 0x18

BITS 32
pm32_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Zero the three page-table pages.
    mov edi, 0x1000
    xor eax, eax
    mov ecx, 0x3000 / 4
    rep stosd

    ; PML4[0] -> PDPT at 0x2000.
    mov dword [0x1000], 0x2000 | 0x03
    mov dword [0x1004], 0

    ; PDPT[0] -> PD at 0x3000.
    mov dword [0x2000], 0x3000 | 0x03
    mov dword [0x2004], 0

    ; PD: 512 x 2 MiB identity pages for 0..1 GiB.
    mov edi, 0x3000
    mov eax, 0x00000083            ; present | writable | PS
    mov ecx, 512
.fill_pd:
    mov [edi], eax
    mov dword [edi + 4], 0
    add eax, 0x200000
    add edi, 8
    loop .fill_pd

    ; PAE + CR3.
    mov eax, cr4
    or eax, 0x20
    mov cr4, eax
    mov eax, 0x1000
    mov cr3, eax

    ; EFER.LME.
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100
    wrmsr

    ; Paging + protection.
    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax

    jmp 0x20:lm64_entry

BITS 64
lm64_entry:
    mov ax, 0x28
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsp, 0x1F000
    mov rax, 0x20000
    jmp rax

BITS 16
