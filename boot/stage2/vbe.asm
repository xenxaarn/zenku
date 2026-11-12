; ---------------------------------------------------------------------------
; Zenku stage2 VBE setup
;
; Tries a short list of packed-pixel linear-framebuffer modes and records the
; chosen mode in boot_params. Must be the last BIOS video operation.
; ---------------------------------------------------------------------------

; ---------------------------------------------------------------------------
; s2_vbe_init: set a graphics mode and fill the framebuffer fields.
; Returns CF set if no suitable linear-framebuffer mode was found.
; ---------------------------------------------------------------------------
s2_vbe_init:
    pushad
    push es
    push ds

    xor ax, ax
    mov ds, ax

    ; Get controller info to confirm VBE 2.0+ and a linear framebuffer.
    mov ax, 0x5900
    mov es, ax
    xor di, di
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .fail
    mov ax, 0x5900
    mov es, ax
    cmp word [es:0], 0x0200
    jb .fail

    mov si, vbe_mode_list
.next_mode:
    lodsw
    test ax, ax
    jz .fail
    mov [vbe_try_mode], ax

    ; Mode information.
    mov ax, 0x5700
    mov es, ax
    xor di, di
    mov cx, [vbe_try_mode]
    mov ax, 0x4F01
    int 0x10
    cmp ax, 0x004F
    jne .next_mode

    mov ax, 0x5700
    mov es, ax
    mov ax, [es:0]
    test ax, 0x0001                 ; mode supported
    jz .next_mode
    test ax, 0x0010                 ; graphics mode
    jz .next_mode
    test ax, 0x0080                 ; linear framebuffer available
    jz .next_mode
    cmp byte [es:25], 32            ; require 32 bpp for the desktop
    jne .next_mode

    ; Set the mode with the linear framebuffer bit.
    mov bx, [vbe_try_mode]
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .next_mode

    ; Re-read the mode information after the mode set.
    mov ax, 0x5700
    mov es, ax
    xor di, di
    mov cx, [vbe_try_mode]
    mov ax, 0x4F01
    int 0x10
    cmp ax, 0x004F
    jne .fail

    mov ax, 0x5700
    mov es, ax
    mov eax, [es:40]                ; physical framebuffer base
    mov [boot_params + BP_FB_ADDR], eax
    mov dword [boot_params + BP_FB_ADDR + 4], 0
    movzx eax, word [es:16]         ; bytes per scan line
    mov [boot_params + BP_FB_PITCH], eax
    movzx eax, word [es:18]         ; width
    mov [boot_params + BP_FB_WIDTH], eax
    movzx eax, word [es:20]         ; height
    mov [boot_params + BP_FB_HEIGHT], eax
    mov al, [es:25]                 ; bpp
    mov [boot_params + BP_FB_BPP], al

    ; VBE 2.0 mode info stores each channel as (size, position) byte pairs:
    ; red 31/32, green 33/34, blue 35/36, reserved 37/38.
    mov al, [es:31]
    mov [boot_params + BP_FB_RED_SIZE], al
    mov al, [es:32]
    mov [boot_params + BP_FB_RED_POS], al
    mov al, [es:33]
    mov [boot_params + BP_FB_GREEN_SIZE], al
    mov al, [es:34]
    mov [boot_params + BP_FB_GREEN_POS], al
    mov al, [es:35]
    mov [boot_params + BP_FB_BLUE_SIZE], al
    mov al, [es:36]
    mov [boot_params + BP_FB_BLUE_POS], al
    mov al, [es:37]
    mov [boot_params + BP_FB_RSVD_SIZE], al
    mov al, [es:38]
    mov [boot_params + BP_FB_RSVD_POS], al

    mov ax, [vbe_try_mode]
    movzx eax, ax
    mov [boot_params + BP_VBE_MODE], eax

    or word [boot_params + BP_FLAGS], BOOT_FLAG_FB

    pop ds
    pop es
    popad
    clc
    ret

.fail:
    pop ds
    pop es
    popad
    stc
    ret

; ---------------------------------------------------------------------------
; vbe_mask_to_pos_size: EAX = channel mask.
; Returns AL = lowest set bit, AH = number of set bits.
; ---------------------------------------------------------------------------
vbe_mask_to_pos_size:
    push ebx
    push ecx
    push edx

    mov ebx, eax
    xor ecx, ecx                   ; position
    test eax, eax
    jz .zero
.find_pos:
    test eax, 1
    jnz .found
    shr eax, 1
    inc ecx
    jmp .find_pos
.found:
    ; EAX has been shifted right until bit 0 is the lowest set bit; count the
    ; run of set bits from there.
    xor edx, edx                   ; count
.count:
    test eax, 1
    jz .count_done
    inc edx
    shr eax, 1
    jmp .count
.count_done:
    mov al, cl
    mov ah, dl
    jmp .out
.zero:
    xor ax, ax
.out:
    pop edx
    pop ecx
    pop ebx
    ret

; Candidate modes, most preferred first. Terminated by 0.
vbe_mode_list:
    dw 0x144                       ; Bochs/QEMU 1024x768x32
    dw 0x145                       ; Bochs/QEMU 1280x1024x32
    dw 0x143                       ; Bochs/QEMU 800x600x32
    dw 0x118                       ; VESA 1024x768x32
    dw 0x11B                       ; VESA 1280x1024x32
    dw 0x112                       ; VESA 640x480x24
    dw 0

vbe_try_mode dw 0
