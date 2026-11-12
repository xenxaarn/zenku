; ---------------------------------------------------------------------------
; Zenku stage2 memory services: A20, E820, BIOS font
; ---------------------------------------------------------------------------

; Enable A20 through the fast A20 gate (port 0x92).
s2_enable_a20:
    push ax
    in al, 0x92
    or al, 0x02
    and al, 0xFE                   ; do not reset the machine
    out 0x92, al
    pop ax
    ret

; ---------------------------------------------------------------------------
; E820: write 24-byte entries to E820_ADDR, store the count in boot_params.
; ---------------------------------------------------------------------------
s2_e820:
    pushad
    push es

    xor ax, ax
    mov es, ax
    mov di, E820_ADDR
    xor ebx, ebx
    xor bp, bp                     ; entry count

    mov dword [boot_params + BP_E820_COUNT], 0

.loop:
    mov eax, 0xE820
    mov edx, 0x534D4150            ; 'SMAP'
    mov ecx, 24
    mov dword [es:di + 20], 1      ; assume ACPI 3 attributes are valid
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .fail

    ; Skip zero-length and our own low-memory scratch areas later; the kernel
    ; is responsible for interpreting types, but zero-length entries are junk.
    mov eax, [es:di + 4]
    or eax, [es:di + 8]
    jz .next

    add di, 24
    inc bp
.next:
    test ebx, ebx
    jz .done
    cmp bp, 128
    jb .loop
.done:
    mov [boot_params + BP_E820_COUNT], bp
    mov dword [boot_params + BP_E820_ADDR], E820_ADDR
    mov dword [boot_params + BP_E820_ADDR + 4], 0
    pop es
    popad
    clc
    ret
.fail:
    mov [boot_params + BP_E820_COUNT], bp
    pop es
    popad
    stc
    ret

; ---------------------------------------------------------------------------
; Copy the BIOS 8x16 font (IVT vector 0x1F) to FONT_ADDR.
; Returns CF set if no font pointer is available.
; ---------------------------------------------------------------------------
s2_copy_font:
    pushad
    push es
    push ds

    xor ax, ax
    mov es, ax
    mov si, 0x007C                 ; IVT entry 0x1F
    mov ax, [es:si]                ; offset
    mov bx, [es:si + 2]            ; segment
    test ax, ax
    jz .fail
    test bx, bx
    jz .fail

    call s2_reload_flat_fs
    mov ds, bx
    mov si, ax
    mov edi, FONT_ADDR
    mov ecx, 4096 / 4
.copy:
    mov eax, [si]
    mov [fs:edi], eax
    add si, 4
    add edi, 4
    dec ecx
    jnz .copy

    mov dword [boot_params + BP_FONT_ADDR], FONT_ADDR
    mov dword [boot_params + BP_FONT_ADDR + 4], 0
    mov word [boot_params + BP_FONT_HEIGHT], 16
    or word [boot_params + BP_FLAGS], BOOT_FLAG_FONT
    pop ds
    pop es
    popad
    clc
    ret
.fail:
    mov word [boot_params + BP_FONT_HEIGHT], 0
    pop ds
    pop es
    popad
    stc
    ret
