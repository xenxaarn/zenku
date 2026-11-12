; ---------------------------------------------------------------------------
; Zenku stage2 entry
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

%include "include/boot_offsets.inc"

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
    mov sp, 0x7C00                 ; 16-bit stack; stage1 area is dead now
    sti

    mov [s2_boot_drive], dl
    movzx eax, dl
    mov [boot_params + BP_BOOT_DRIVE], eax

    call s2_clear_boot_params
    mov dword [boot_params + BP_MAGIC], BOOT_PARAMS_MAGIC
    mov dword [boot_params + BP_VERSION], BOOT_PARAMS_VERSION

    call s2_serial_init

    mov si, msg_s2_banner
    call s2_puts

    mov si, msg_s2_drive
    call s2_serial_puts
    mov al, [s2_boot_drive]
    call s2_serial_hex8
    mov si, msg_crlf
    call s2_serial_puts

    ; --- A20 + unreal mode ------------------------------------------------
    call s2_enable_a20
    call s2_enable_unreal
    mov si, msg_unreal
    call s2_serial_puts

    ; --- disk geometry -----------------------------------------------------
    call s2_disk_init
    mov si, msg_sector
    call s2_serial_puts
    mov ax, [s2_sector_size]
    call s2_serial_hex16
    mov si, msg_crlf
    call s2_serial_puts

    ; --- E820 --------------------------------------------------------------
    call s2_e820
    jc .e820_fail
    mov si, msg_e820
    call s2_serial_puts
    movzx eax, word [boot_params + BP_E820_COUNT]
    call s2_serial_hex32
    mov si, msg_crlf
    call s2_serial_puts
    jmp .font

.e820_fail:
    mov si, msg_e820_fail
    call s2_serial_puts

.font:
    ; --- BIOS font ---------------------------------------------------------
    call s2_copy_font
    jc .font_fail
    mov si, msg_font_ok
    call s2_serial_puts
    jmp .iso
.font_fail:
    mov si, msg_font_fail
    call s2_serial_puts

.iso:
    ; --- ISO9660 -----------------------------------------------------------
    call s2_iso_init
    jc .iso_fail
    mov si, msg_iso_ok
    call s2_serial_puts

    ; DEBUG: first byte and first STAGE3 name byte in the root directory
    mov ax, 0x6080
    mov es, ax
    ; Locate STAGE3.BIN.
    mov si, iso_name_stage3
    call s2_iso_find
    jc .stage3_missing
    mov si, msg_stage3
    call s2_serial_puts
    mov eax, [iso_found_lba]
    call s2_serial_hex32
    mov si, msg_size
    call s2_serial_puts
    mov eax, [iso_found_size]
    call s2_serial_hex32
    mov si, msg_crlf
    call s2_serial_puts

    cmp dword [iso_found_size], STAGE3_MAX_BYTES
    ja .stage3_too_big

    mov eax, [iso_found_lba]
    mov edx, [iso_found_size]
    mov edi, STAGE3_ADDR
    call s2_load_file
    jc .stage3_load_fail
    mov si, msg_stage3_loaded
    call s2_serial_puts

    ; Locate ROOT.EXT2 but do not load it yet in this milestone.
    mov si, iso_name_root
    call s2_iso_find
    jc .root_missing
    mov si, msg_root
    call s2_serial_puts
    mov eax, [iso_found_lba]
    call s2_serial_hex32
    mov si, msg_size
    call s2_serial_puts
    mov eax, [iso_found_size]
    call s2_serial_hex32
    mov si, msg_crlf
    call s2_serial_puts

    cmp dword [iso_found_size], EXT2_MAX_BYTES
    ja .root_too_big
    mov eax, [iso_found_lba]
    mov edx, [iso_found_size]
    mov edi, EXT2_ADDR
    call s2_load_file
    jc .root_load_fail
    mov dword [boot_params + BP_INITRD_ADDR], EXT2_ADDR
    mov dword [boot_params + BP_INITRD_ADDR + 4], 0
    mov [boot_params + BP_INITRD_SIZE], edx
    mov dword [boot_params + BP_INITRD_SIZE + 4], 0
    or word [boot_params + BP_FLAGS], BOOT_FLAG_INITRD
    mov si, msg_root_loaded
    call s2_serial_puts

    ; Video mode last: it ends BIOS teletype output.
    call s2_vbe_init
    jc .vbe_fail
    mov si, msg_vbe_ok
    call s2_serial_puts
    mov eax, [boot_params + BP_FB_WIDTH]
    call s2_serial_hex32
    mov si, msg_vbe_x
    call s2_serial_puts
    mov eax, [boot_params + BP_FB_HEIGHT]
    call s2_serial_hex32
    mov si, msg_vbe_bpp
    call s2_serial_puts
    movzx eax, byte [boot_params + BP_FB_BPP]
    call s2_serial_hex32
    mov si, msg_crlf
    call s2_serial_puts
    jmp .done

.root_too_big:
    mov si, msg_root_big
    call s2_serial_puts
    jmp .done
.root_load_fail:
    mov si, msg_root_load_fail
    call s2_serial_puts
    jmp .done
.vbe_fail:
    mov si, msg_vbe_fail
    call s2_serial_puts

.stage3_missing:
    mov si, msg_stage3_missing
    call s2_serial_puts
    jmp .done
.stage3_too_big:
    mov si, msg_stage3_big
    call s2_serial_puts
    jmp .done
.stage3_load_fail:
    mov si, msg_stage3_load_fail
    call s2_serial_puts
    jmp .done
.root_missing:
    mov si, msg_root_missing
    call s2_serial_puts
    jmp .done
.iso_fail:
    mov si, msg_iso_fail
    call s2_serial_puts

.done:
    call s2_serial_flush
    call s2_enter_long_mode

.halt:
    cli
    hlt
    jmp .halt

; ---------------------------------------------------------------------------
; Zero the boot_params block.
; ---------------------------------------------------------------------------
s2_clear_boot_params:
    pushad
    push es
    xor ax, ax
    mov es, ax
    mov di, BOOT_PARAMS_ADDR
    mov cx, BOOT_PARAMS_SIZE
    xor al, al
    rep stosb
    pop es
    popad
    ret

; ---------------------------------------------------------------------------
; Data
; ---------------------------------------------------------------------------
s2_boot_drive db 0

msg_s2_banner      db "ZENKU STAGE2 OK", 13, 10, 0
msg_s2_drive       db "stage2: boot drive 0x", 0
msg_unreal         db "stage2: unreal mode ready", 13, 10, 0
msg_sector         db "stage2: sector size 0x", 0
msg_e820           db "stage2: e820 entries 0x", 0
msg_e820_fail      db "stage2: e820 failed", 13, 10, 0
msg_font_ok        db "stage2: font copied", 13, 10, 0
msg_font_fail      db "stage2: no BIOS font", 13, 10, 0
msg_iso_ok         db "stage2: ISO9660 mounted", 13, 10, 0
msg_iso_fail       db "stage2: ISO9660 mount failed", 13, 10, 0
msg_stage3         db "stage2: STAGE3.BIN lba 0x", 0
msg_root           db "stage2: ROOT.EXT2 lba 0x", 0
msg_size           db " size 0x", 0
msg_stage3_loaded  db "stage2: stage3 loaded at 0x20000", 13, 10, 0
msg_stage3_missing db "stage2: STAGE3.BIN not found", 13, 10, 0
msg_stage3_big     db "stage2: STAGE3.BIN too large", 13, 10, 0
msg_stage3_load_fail db "stage2: STAGE3.BIN load failed", 13, 10, 0
msg_root_missing   db "stage2: ROOT.EXT2 not found", 13, 10, 0
msg_root_loaded    db "stage2: ROOT.EXT2 loaded at 0x04000000", 13, 10, 0
msg_root_big       db "stage2: ROOT.EXT2 too large", 13, 10, 0
msg_root_load_fail db "stage2: ROOT.EXT2 load failed", 13, 10, 0
msg_vbe_ok         db "stage2: VBE mode 0x", 0
msg_vbe_x          db "x", 0
msg_vbe_bpp        db " bpp 0x", 0
msg_vbe_fail       db "stage2: no suitable VBE mode", 13, 10, 0
msg_crlf           db 13, 10, 0

iso_name_stage3    db "STAGE3.BIN", 0
iso_name_root      db "ROOT.EXT2", 0

; ---------------------------------------------------------------------------
; Includes
; ---------------------------------------------------------------------------
%include "boot/stage2/console.asm"
%include "boot/stage2/gdt.asm"
%include "boot/stage2/mem.asm"
%include "boot/stage2/disk.asm"
%include "boot/stage2/iso9660.asm"
%include "boot/stage2/vbe.asm"
%include "boot/stage2/longmode.asm"
