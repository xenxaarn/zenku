; Zenku stage2 console helpers (16-bit real mode).
; All routines preserve the segment registers they do not explicitly change.

; ---------------------------------------------------------------------------
; Serial (COM1, 115200 8N1)
; ---------------------------------------------------------------------------
s2_serial_init:
    push ax
    push dx

    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8
    mov al, 1
    out dx, al
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x03
    out dx, al
    mov dx, 0x3FA
    mov al, 0xC7
    out dx, al
    mov dx, 0x3FC
    mov al, 0x03
    out dx, al

    pop dx
    pop ax
    ret

; AL = character
s2_serial_putc:
    push ax
    push dx
    mov ah, al
.wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x20
    jz .wait
    mov dx, 0x3F8
    mov al, ah
    out dx, al
    pop dx
    pop ax
    ret

; SI = NUL-terminated string
s2_serial_puts:
    push ax
    push si
.next:
    lodsb
    test al, al
    jz .done
    call s2_serial_putc
    jmp .next
.done:
    pop si
    pop ax
    ret

; AL = byte
s2_serial_hex8:
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
    call s2_serial_putc
    ret

; AX = word, four hex digits
s2_serial_hex16:
    push ax
    mov al, ah
    call s2_serial_hex8
    pop ax
    call s2_serial_hex8
    ret

; EAX = dword, eight hex digits
s2_serial_hex32:
    push eax
    push bx
    mov ebx, eax
    shr eax, 16
    call s2_serial_hex16
    mov eax, ebx
    call s2_serial_hex16
    pop bx
    pop eax
    ret

s2_serial_flush:
    push ax
    push dx
.wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x40
    jz .wait
    pop dx
    pop ax
    ret

; ---------------------------------------------------------------------------
; BIOS teletype (usable until a VBE mode is set)
; ---------------------------------------------------------------------------
; SI = NUL-terminated string
s2_bios_puts:
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

; SI = string, mirror to serial and BIOS console
s2_puts:
    push si
    call s2_serial_puts
    pop si
    call s2_bios_puts
    ret
