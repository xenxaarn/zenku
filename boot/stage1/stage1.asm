; ---------------------------------------------------------------------------
; Zenku stage1
;
; El Torito no-emulation boot image. The first 2048 bytes are loaded by the
; BIOS at 0000:7C00. Stage2 is appended at file offset 2048 and is loaded to
; physical 0x9000 by this code.
;
; Offsets 8..63 are the El Torito Boot Info Table, patched by xorriso inside
; the ISO:
;   +0  bi_pvd      LBA of the ISO primary volume descriptor
;   +4  bi_file     LBA of this boot image
;   +8  bi_length   boot image length
;   +12 bi_csum     checksum
;
; The build script passes STAGE2_BYTES. The ISO LBA of the boot image plus one
; is the first stage2 sector; BIOS sector geometry is discovered with INT 13h
; AH=48h so both 2048-byte CD and 512-byte emulated sectors work.
; ---------------------------------------------------------------------------

BITS 16
ORG 0x7C00

%ifndef STAGE2_BYTES
%define STAGE2_BYTES 65536
%endif

COM1            equ 0x3F8
STAGE2_SEGMENT  equ 0x0900
STAGE2_ADDR     equ 0x9000
CHUNK_BYTES     equ 16384

start:
    jmp short entry
    nop
    times 8 - ($ - $$) db 0

    ; El Torito Boot Info Table (56 bytes, patched in the ISO)
    times 56 db 0

entry:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    call serial_init

    mov si, msg_banner
    call puts

    mov si, msg_drive
    call serial_puts
    mov al, [boot_drive]
    call serial_put_hex8
    mov si, msg_crlf
    call serial_puts

    call get_sector_size

    mov si, msg_loading
    call puts

    call load_stage2
    jc fatal_boot

    mov si, msg_jump
    call puts

    mov dl, [boot_drive]
    jmp 0x0000:STAGE2_ADDR

fatal_boot:
    mov si, msg_error
    call puts
.halt:
    cli
    hlt
    jmp .halt

; ---------------------------------------------------------------------------
; Read the stage2 payload from the boot image extent.
; ---------------------------------------------------------------------------
load_stage2:
    ; byte offset of stage2 = (bi_file * 2048) + 2048
    mov eax, [0x7C00 + 12]
    test eax, eax
    jnz .have_file_lba
    mov eax, 1                 ; direct/raw boot fallback
.have_file_lba:
    shl eax, 11
    add eax, 2048

    ; LBA = byte offset >> sector_shift
    movzx ecx, byte [sector_shift]
    shr eax, cl
    mov [cur_lba], eax
    mov dword [cur_lba + 4], 0

    mov dword [bytes_left], STAGE2_BYTES
    mov word [dest_seg], STAGE2_SEGMENT

.read_loop:
    mov eax, [bytes_left]
    test eax, eax
    jz .done

    ; chunk = min(bytes_left, 16384)
    mov edx, eax
    cmp edx, CHUNK_BYTES
    jbe .chunk_ok
    mov edx, CHUNK_BYTES
.chunk_ok:

    ; sectors = ceil(chunk / sector_size)
    mov eax, edx
    movzx ecx, word [sector_size]
    add eax, ecx
    dec eax
    movzx ecx, byte [sector_shift]
    shr eax, cl
    mov [dap_count], ax

    mov eax, [cur_lba]
    mov [dap_lba], eax
    mov dword [dap_lba + 4], 0
    mov ax, [dest_seg]
    mov [dap_seg], ax
    mov word [dap_off], 0

    mov cx, 3
.retry:
    push cx
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    pop cx
    jnc .read_ok
    ; reset the disk and try again
    push cx
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
    pop cx
    loop .retry
    stc
    ret

.read_ok:
    ; verify the stage2 header before trusting the transfer
    cmp dword [STAGE2_ADDR + 4], 0x3254535A ; "ZST2"
    jne .bad
    cmp word [STAGE2_ADDR + 8], 1
    jne .bad

    sub [bytes_left], edx

    movzx eax, word [dap_count]
    add [cur_lba], eax
    adc dword [cur_lba + 4], 0

    cmp edx, CHUNK_BYTES
    jne .done

    add word [dest_seg], 0x0400
    jmp .read_loop

.done:
    clc
    ret
.bad:
    stc
    ret

; ---------------------------------------------------------------------------
; Query BIOS geometry and derive the sector shift.
; ---------------------------------------------------------------------------
get_sector_size:
    mov word [drive_params], 26
    mov si, drive_params
    mov dl, [boot_drive]
    mov ah, 0x48
    int 0x13
    jc .fail

    mov ax, [drive_params + 24]
    cmp ax, 2048
    je .cd
    cmp ax, 512
    je .hdd
.fail:
    mov si, msg_geom_error
    call puts
    cli
    hlt
    jmp $
.cd:
    mov word [sector_size], 2048
    mov byte [sector_shift], 11
    ret
.hdd:
    mov word [sector_size], 512
    mov byte [sector_shift], 9
    ret

; ---------------------------------------------------------------------------
; Console: serial + BIOS teletype
; ---------------------------------------------------------------------------

; SI = string
puts:
    push si
    call serial_puts
    pop si
    call bios_puts
    ret

serial_init:
    push ax
    push dx

    mov dx, COM1 + 1
    xor al, al
    out dx, al
    mov dx, COM1 + 3
    mov al, 0x80
    out dx, al
    mov dx, COM1 + 0
    mov al, 1
    out dx, al
    mov dx, COM1 + 1
    xor al, al
    out dx, al
    mov dx, COM1 + 3
    mov al, 0x03
    out dx, al
    mov dx, COM1 + 2
    mov al, 0xC7
    out dx, al
    mov dx, COM1 + 4
    mov al, 0x03
    out dx, al

    pop dx
    pop ax
    ret

; AL = char
serial_putc:
    push ax
    push dx
    mov ah, al
.wait:
    mov dx, COM1 + 5
    in al, dx
    test al, 0x20
    jz .wait
    mov dx, COM1
    mov al, ah
    out dx, al
    pop dx
    pop ax
    ret

; AL = byte, two uppercase hex digits on serial
serial_put_hex8:
    push ax
    push bx
    mov bl, al
    shr al, 4
    call .nibble
    mov al, bl
    and al, 0x0F
    call .nibble
    pop bx
    pop ax
    ret
.nibble:
    cmp al, 10
    jb .digit
    add al, 'A' - 10
    jmp .emit
.digit:
    add al, '0'
.emit:
    call serial_putc
    ret

; SI = string
serial_puts:
    push ax
    push si
.next:
    lodsb
    test al, al
    jz .done
    call serial_putc
    jmp .next
.done:
    pop si
    pop ax
    ret

; SI = string
bios_puts:
    push ax
    push bx
    push si
.next:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .next
.done:
    pop si
    pop bx
    pop ax
    ret

; ---------------------------------------------------------------------------
; Data
; ---------------------------------------------------------------------------

boot_drive   db 0
sector_size  dw 0
sector_shift db 0
bytes_left   dd 0
cur_lba      dq 0
dest_seg     dw 0

dap:
    db 0x10, 0
dap_count: dw 0
dap_off:   dw 0
dap_seg:   dw 0
dap_lba:   dq 0

drive_params: times 26 db 0

msg_banner     db "ZENKU STAGE1 OK", 13, 10, 0
msg_drive      db "boot drive: 0x", 0
msg_loading    db "loading stage2...", 13, 10, 0
msg_jump       db "jumping to stage2", 13, 10, 0
msg_crlf       db 13, 10, 0
msg_error      db "STAGE1 ERROR: stage2 load failed", 13, 10, 0
msg_geom_error db "STAGE1 ERROR: unsupported BIOS geometry", 13, 10, 0

; ---------------------------------------------------------------------------
; Boot image layout
;
; The BIOS loads the whole 2048-byte El Torito no-emulation boot image at
; 0000:7C00, so stage1 code may use the full first sector. The classic MBR
; signature at offset 510 is not required and is deliberately omitted.
; ---------------------------------------------------------------------------

times 2048 - ($ - $$) db 0
