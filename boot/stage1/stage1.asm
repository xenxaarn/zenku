; ---------------------------------------------------------------------------
; Zenku stage1
;
; El Torito no-emulation boot image, loaded by the BIOS at 0000:7C00.
; The image is exactly 2048 bytes. Offsets 8..63 are the El Torito Boot Info
; Table and are patched by xorriso (-boot-info-table) inside the ISO; they
; must stay zero in the build artifact.
;
; This phase only proves the media path:
;   - initialize COM1 (115200 8N1)
;   - print a banner to COM1 and to the BIOS console
;   - report the BIOS boot drive
;   - write isa-debug-exit in TEST_EXIT builds, otherwise halt
;
; Phase 1 extends this file to load stage2 from the boot image extent using
; the patched bi_file LBA.
; ---------------------------------------------------------------------------

BITS 16
ORG 0x7C00

COM1        equ 0x3F8
DEBUG_EXIT  equ 0x501

start:
    jmp short entry
    nop

    times 8 - ($ - $$) db 0

    ; El Torito Boot Info Table (56 bytes):
    ;   +0  bi_pvd      LBA of the primary volume descriptor
    ;   +4  bi_file     LBA of this boot image
    ;   +8  bi_length   boot image length in bytes
    ;   +12 bi_csum     checksum
    ;   +16 reserved
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
    call serial_puts
    mov si, msg_banner
    call bios_puts

    mov si, msg_drive
    call serial_puts
    mov al, [boot_drive]
    call serial_put_hex8
    mov si, msg_crlf
    call serial_puts

    call serial_flush

%ifdef TEST_EXIT
    mov dx, DEBUG_EXIT
    mov al, 0x10
    out dx, al
%endif

.halt:
    cli
    hlt
    jmp .halt

; ---------------------------------------------------------------------------
; Serial (16550-compatible COM1)
; ---------------------------------------------------------------------------

serial_init:
    push ax
    push dx

    mov dx, COM1 + 1          ; IER: disable UART interrupts
    xor al, al
    out dx, al

    mov dx, COM1 + 3          ; LCR: enable DLAB
    mov al, 0x80
    out dx, al

    mov dx, COM1 + 0          ; DLL = 1 -> 115200 baud
    mov al, 1
    out dx, al

    mov dx, COM1 + 1          ; DLM = 0
    xor al, al
    out dx, al

    mov dx, COM1 + 3          ; LCR: 8 data bits, no parity, 1 stop
    mov al, 0x03
    out dx, al

    mov dx, COM1 + 2          ; FCR: enable and clear FIFOs
    mov al, 0xC7
    out dx, al

    mov dx, COM1 + 4          ; MCR: DTR + RTS
    mov al, 0x03
    out dx, al

    pop dx
    pop ax
    ret

; AL = character
serial_putc:
    push ax
    push dx
    mov ah, al
.wait_thre:
    mov dx, COM1 + 5
    in al, dx
    test al, 0x20             ; transmitter holding register empty
    jz .wait_thre
    mov dx, COM1
    mov al, ah
    out dx, al
    pop dx
    pop ax
    ret

; SI = NUL-terminated string
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

; AL = byte, print as two uppercase hex digits
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

; Wait until the transmitter is completely empty before halting/exiting.
serial_flush:
    push ax
    push dx
.wait_temt:
    mov dx, COM1 + 5
    in al, dx
    test al, 0x40             ; transmitter empty
    jz .wait_temt
    pop dx
    pop ax
    ret

; ---------------------------------------------------------------------------
; BIOS console
; ---------------------------------------------------------------------------

; SI = NUL-terminated string
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

boot_drive  db 0
msg_banner  db "ZENKU STAGE1 OK", 13, 10, 0
msg_drive   db "boot drive: 0x", 0
msg_crlf    db 13, 10, 0

; ---------------------------------------------------------------------------
; Boot image layout
; ---------------------------------------------------------------------------

times 510 - ($ - $$) db 0
dw 0xAA55
times 2048 - ($ - $$) db 0
