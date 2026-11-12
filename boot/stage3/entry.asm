; ---------------------------------------------------------------------------
; Zenku stage3 entry (temporary 64-bit milestone)
;
; Runs in long mode with identity paging. This placeholder proves the mode
; switch before the C ext2/ELF loader replaces it.
; ---------------------------------------------------------------------------

BITS 64
ORG 0x20000

start:
    cld
    mov rsp, 0x1F000

    lea rsi, [msg_banner]
    call serial_puts
    call serial_flush

%ifdef TEST_EXIT
    mov dx, 0x501
    mov al, 0x10
    out dx, al
%endif

.halt:
    cli
    hlt
    jmp .halt

; RSI = NUL-terminated string
serial_puts:
    push rax
    push rdx
.next:
    lodsb
    test al, al
    jz .done
    call serial_putc
    jmp .next
.done:
    pop rdx
    pop rax
    ret

; AL = character
serial_putc:
    push rax
    push rdx
    mov ah, al
.wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x20
    jz .wait
    mov dx, 0x3F8
    mov al, ah
    out dx, al
    pop rdx
    pop rax
    ret

; Wait for the UART transmitter to drain before exiting QEMU.
serial_flush:
    push rax
    push rdx
.wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x40
    jz .wait
    pop rdx
    pop rax
    ret

msg_banner db "ZENKU STAGE3 OK (long mode)", 13, 10, 0
