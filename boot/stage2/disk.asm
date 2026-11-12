; ---------------------------------------------------------------------------
; Zenku stage2 BIOS disk access
; ---------------------------------------------------------------------------

; Query the BIOS for the sector size of the boot drive.
; Returns AX = sector size (2048 for CD, 512 for emulated HDD).
s2_disk_init:
    push ebx
    push ecx
    push edx
    push si
    push ds
    push es

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov word [drive_params], 26
    mov si, drive_params
    mov dl, [s2_boot_drive]
    mov ah, 0x48
    int 0x13
    jc .fallback

    mov ax, [drive_params + 24]
    test ax, ax
    jz .fallback
    jmp .done
.fallback:
    mov ax, 2048                   ; CD-ROM default
.done:
    mov [s2_sector_size], ax
    mov [boot_params + BP_SECTOR_SIZE], ax

    pop es
    pop ds
    pop si
    pop edx
    pop ecx
    pop ebx
    ret

; ---------------------------------------------------------------------------
; s2_disk_read
;   EAX = 32-bit LBA
;   CX  = sector count (1..127)
;   ES:BX = destination buffer
; Returns CF set on failure.
; ---------------------------------------------------------------------------
s2_disk_read:
    push ax
    push cx
    push dx
    push si
    push ds

    xor dx, dx
    mov ds, dx

    mov [dap_count], cx
    mov [dap_off], bx
    mov [dap_seg], es
    mov [dap_lba], eax
    mov dword [dap_lba + 4], 0

    mov cx, 3
.retry:
    push cx
    mov si, dap
    mov dl, [s2_boot_drive]
    mov ah, 0x42
    int 0x13
    pop cx
    jnc .done
    ; reset and retry
    push cx
    xor ah, ah
    mov dl, [s2_boot_drive]
    int 0x13
    pop cx
    loop .retry
    stc
    jmp .out
.done:
    clc
.out:
    pop ds
    pop si
    pop dx
    pop cx
    pop ax
    ret

; ---------------------------------------------------------------------------
; s2_load_file
;   EAX   = starting LBA
;   EDX   = byte length
;   EDI   = linear destination (requires unreal mode)
; Returns CF set on failure.
; ---------------------------------------------------------------------------
s2_load_file:
    pushad
    push ds
    push es

    mov [load_lba], eax
    mov [load_left], edx
    mov [load_dest], edi

.loop:
    mov eax, [load_left]
    test eax, eax
    jz .success

    ; chunk = min(left, DISK_CHUNK_BYTES)
    mov edx, eax
    cmp edx, DISK_CHUNK_BYTES
    jbe .chunk_ok
    mov edx, DISK_CHUNK_BYTES
.chunk_ok:
    mov [load_chunk], edx

    ; sectors = ceil(chunk / sector_size), bounded to 127
    mov eax, edx
    movzx ecx, word [s2_sector_size]
    add eax, ecx
    dec eax
    xor edx, edx
    div ecx
    cmp eax, 127
    jbe .sectors_ok
    mov eax, 127
.sectors_ok:
    mov [load_sectors], eax

    mov eax, [load_lba]
    mov cx, [load_sectors]
    mov bx, 0
    mov dx, DISK_BOUNCE_SEG
    mov es, dx
    call s2_disk_read
    jc .fail

    ; Copy the chunk out of the bounce buffer through flat FS.
    call s2_reload_flat_fs
    mov ecx, [load_chunk]
    mov edi, [load_dest]
    shr ecx, 2
    jz .after_copy
    mov ax, DISK_BOUNCE_SEG
    mov ds, ax
    xor si, si
.copy:
    mov eax, [si]
    mov [fs:edi], eax
    add si, 4
    add edi, 4
    dec ecx
    jnz .copy

.after_copy:
    ; Handle a tail that is not a multiple of four bytes. Compute the source
    ; offset while DS is still zero, before switching to the bounce segment.
    xor ax, ax
    mov ds, ax
    mov ecx, [load_chunk]
    mov si, cx
    and si, ~3
    and ecx, 3
    jz .no_tail
    mov ax, DISK_BOUNCE_SEG
    mov ds, ax
.tail:
    mov al, [si]
    mov [fs:edi], al
    inc si
    inc edi
    dec ecx
    jnz .tail
.no_tail:
    ; Restore DS before touching stage2 variables again.
    xor ax, ax
    mov ds, ax

    mov eax, [load_chunk]
    sub [load_left], eax
    add [load_dest], eax

    movzx eax, word [load_sectors]
    add [load_lba], eax

    jmp .loop

.success:
    pop es
    pop ds
    popad
    clc
    ret
.fail:
    pop es
    pop ds
    popad
    stc
    ret

; ---------------------------------------------------------------------------
; Data
; ---------------------------------------------------------------------------
s2_sector_size dw 2048
drive_params   times 26 db 0

dap:
    db 0x10, 0
dap_count: dw 0
dap_off:   dw 0
dap_seg:   dw 0
dap_lba:   dq 0

load_lba     dd 0
load_left    dd 0
load_dest    dd 0
load_chunk   dd 0
load_sectors dw 0
