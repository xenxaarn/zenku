; ---------------------------------------------------------------------------
; Zenku stage2 entry (Phase 1 work in progress)
;
; Header layout (fixed, inspected by stage1):
;   +0  jmp short entry
;   +2  nop
;   +3  pad
;   +4  "ZST2"
;   +8  u16 version
;   +10 u16 pad
;   +12 u32 size in bytes (patched by scripts/build-boot.sh)
;   +16 u32 reserved
;   +20 entry
; ---------------------------------------------------------------------------

BITS 16
ORG 0x9000

stage2_header:
    jmp short s2_entry
    nop
    db 0
    dd 0x3254535A          ; "ZST2"
    dw 1
    dw 0
    dd 0                   ; patched with the real size
    dd 0

s2_entry:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [s2_boot_drive], dl

    call s2_serial_init

    mov si, msg_s2_banner
    call s2_puts

    mov si, msg_s2_drive
    call s2_serial_puts
    mov al, [s2_boot_drive]
    call s2_serial_hex8
    mov si, msg_s2_crlf
    call s2_serial_puts

    call s2_serial_flush

%ifdef TEST_EXIT
    mov dx, 0x501
    mov al, 0x10
    out dx, al
%endif

.halt:
    cli
    hlt
    jmp .halt

; ---------------------------------------------------------------------------
; Data
; ---------------------------------------------------------------------------
s2_boot_drive db 0
msg_s2_banner db "ZENKU STAGE2 OK", 13, 10, 0
msg_s2_drive  db "stage2: boot drive 0x", 0
msg_s2_crlf   db 13, 10, 0

; ---------------------------------------------------------------------------
; Includes
; ---------------------------------------------------------------------------
%include "boot/stage2/console.asm"
