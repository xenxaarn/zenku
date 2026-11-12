; ---------------------------------------------------------------------------
; Zenku stage2 ISO9660 (level 1 + 8.3 names) reader
;
; Only what the boot path needs: the primary volume descriptor, the root
; directory, and lookup + loading of regular files.
; ---------------------------------------------------------------------------

; ---------------------------------------------------------------------------
; s2_iso_init: read the PVD and the root directory.
; Returns CF set on failure.
; ---------------------------------------------------------------------------
s2_iso_init:
    pushad
    push es
    push ds

    xor ax, ax
    mov ds, ax
    mov es, ax

    ; Read logical sector 16 (the PVD) into ISO_SCRATCH_ADDR.
    mov eax, 16
    mov cx, 1
    mov bx, ISO_SCRATCH_ADDR & 0xFFFF
    mov dx, ISO_SCRATCH_ADDR >> 4
    mov es, dx
    call s2_disk_read
    jc .fail

    ; Sector size must match the CD logical block size.
    cmp word [s2_sector_size], 2048
    jne .fail

    mov ax, ISO_SCRATCH_ADDR >> 4
    mov es, ax
    xor bx, bx

    cmp byte [es:bx], 1             ; primary volume descriptor
    jne .fail
    cmp dword [es:bx + 1], 0x30304443 ; "CD00"
    jne .fail
    cmp byte [es:bx + 5], '1'
    jne .fail

    ; Root directory record is at offset 156.
    mov eax, [es:bx + 156 + 2]      ; extent LBA (little endian)
    mov edx, [es:bx + 156 + 10]     ; data length (little endian)
    mov [iso_root_lba], eax
    mov [iso_root_size], edx

    ; Read the root directory into ISO_SCRATCH_ADDR + 2048.
    mov ecx, edx
    add ecx, 2047
    shr ecx, 11                     ; sectors
    cmp ecx, 8
    jbe .root_sectors_ok
    mov ecx, 8                      ; cap at 16 KiB
.root_sectors_ok:
    mov [iso_root_sectors], cx
    mov eax, [iso_root_lba]
    mov bx, 2048
    mov dx, ISO_SCRATCH_ADDR >> 4
    mov es, dx
    call s2_disk_read
    jc .fail

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
; s2_iso_find: find a file in the root directory.
;   SI = NUL-terminated 8.3 base name, uppercase, without ";1"
; On success: iso_found_lba and iso_found_size are set, CF clear.
; On failure: CF set.
; ---------------------------------------------------------------------------
s2_iso_find:
    pushad
    push es
    push ds

    xor ax, ax
    mov ds, ax
    mov ax, ISO_SCRATCH_ADDR >> 4
    mov es, ax
    movzx esi, si                  ; 16-bit callers may leave upper bits dirty
    mov edi, 2048
    mov dword [iso_scan_pos], 0

.scan:
    mov eax, [iso_scan_pos]
    cmp eax, [iso_root_size]
    jae .not_found
    cmp eax, ISO_SCRATCH_SIZE - 2048
    jae .not_found

    movzx ecx, byte [es:edi]
    test ecx, ecx
    jz .next_sector

    ; Directory record layout: name length at +32, name at +33.
    movzx edx, byte [es:edi + 32]
    lea ebp, [edi + 33]

    ; Effective name length, stripped of an optional ";1".
    cmp edx, 2
    jb .have_len
    cmp byte [es:ebp + edx - 2], ';'
    jne .have_len
    sub edx, 2
.have_len:
    ; Compare the target length.
    xor ebx, ebx
.len_loop:
    cmp byte [esi + ebx], 0
    je .len_done
    inc ebx
    jmp .len_loop
.len_done:
    cmp edx, ebx
    jne .advance

    ; Compare bytes.
    xor ecx, ecx
.cmp_loop:
    cmp ecx, ebx
    jae .match
    mov al, [es:ebp + ecx]
    cmp al, [esi + ecx]
    jne .advance
    inc ecx
    jmp .cmp_loop

.match:
    mov eax, [es:edi + 2]           ; extent LBA
    mov edx, [es:edi + 10]          ; file size
    mov [iso_found_lba], eax
    mov [iso_found_size], edx

    pop ds
    pop es
    popad
    clc
    ret

.advance:
    movzx eax, byte [es:edi]
    add [iso_scan_pos], eax
    add edi, eax
    jmp .scan

.next_sector:
    ; A zero length means padding to the next 2048-byte boundary.
    mov eax, [iso_scan_pos]
    or eax, 2047
    inc eax
    mov [iso_scan_pos], eax
    lea edi, [eax + 2048]
    jmp .scan

.not_found:
    pop ds
    pop es
    popad
    stc
    ret

; ---------------------------------------------------------------------------
; s2_iso_load: find and load a file to a linear address.
;   SI  = 8.3 base name
;   EDI = linear destination
; Returns CF set on failure.
; ---------------------------------------------------------------------------
s2_iso_load:
    push eax
    push edx
    call s2_iso_find
    jc .fail
    mov eax, [iso_found_lba]
    mov edx, [iso_found_size]
    call s2_load_file
    jc .fail
    pop edx
    pop eax
    clc
    ret
.fail:
    pop edx
    pop eax
    stc
    ret

; ---------------------------------------------------------------------------
; Data
; ---------------------------------------------------------------------------
iso_root_lba     dd 0
iso_root_size    dd 0
iso_root_sectors dw 0
iso_scan_pos     dd 0
iso_found_lba    dd 0
iso_found_size   dd 0
