; ---------------------------------------------------------------------------
; Zenku stage2 GDT and unreal-mode entry
;
; The GDT lives in the stage2 image. Linear addresses are valid because stage2
; runs with the code segment based at 0x9000 (ORG 0x9000).
; ---------------------------------------------------------------------------

align 16
gdt_start:
    dq 0x0000000000000000          ; 0x00 null
gdt_code16:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x9A                        ; present, ring 0, code, readable
    db 0x8F                        ; G=1, D=0: 16-bit code for unreal mode
    db 0x00
gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92                        ; present, ring 0, data, writable
    db 0xCF
    db 0x00
gdt_code32:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x9A
    db 0xCF                        ; G=1, D=1: 32-bit code
    db 0x00
gdt_code64:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x9A
    db 0xAF                        ; G=1, L=1
    db 0x00
gdt_data64:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92
    db 0xCF                        ; G=1, D/B=1, L=0 for data
    db 0x00
gdt_end:

gdt_pointer:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ---------------------------------------------------------------------------
; Enter unreal mode: FS and GS keep a flat 4 GiB data descriptor after the CPU
; returns to real mode. DS/ES/SS are reset to 0 for BIOS compatibility.
; ---------------------------------------------------------------------------
s2_enable_unreal:
    pushad
    cli
    db 0x66
    lgdt [gdt_pointer]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x08:.protected
.protected:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov eax, cr0
    and eax, ~1
    mov cr0, eax
    jmp 0x0000:.real
.real:
    ; Only DS/ES/SS are reset to zero. FS/GS must NOT be reloaded here: in
    ; real mode a selector load rebuilds the hidden base as selector << 4,
    ; which would turn the flat descriptor into base 0x100.
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    sti
    popad
    ret

; Reload the flat FS/GS descriptor. Loading a selector while the CPU is in
; real mode would rebuild the hidden cache from selector << 4, so re-enter
; unreal mode to restore a base-0, 4 GiB data descriptor.
s2_reload_flat_fs:
    jmp s2_enable_unreal
