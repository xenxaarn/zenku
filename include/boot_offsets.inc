; ---------------------------------------------------------------------------
; Zenku boot ABI offsets for NASM
;
; Keep in sync with include/boot_info.h.
; ---------------------------------------------------------------------------

%define BOOT_PARAMS_ADDR        0x6000
%define boot_params             BOOT_PARAMS_ADDR
%define BOOT_PARAMS_MAGIC       0x3150425A      ; "ZBP1"
%define BOOT_PARAMS_VERSION     1

%define BP_MAGIC                0x00
%define BP_VERSION              0x04
%define BP_E820_ADDR            0x08
%define BP_E820_COUNT           0x10
%define BP_SECTOR_SIZE          0x14
%define BP_INITRD_ADDR          0x18
%define BP_INITRD_SIZE          0x20
%define BP_FONT_ADDR            0x28
%define BP_FONT_HEIGHT          0x2C
%define BP_FLAGS                0x2E
%define BP_FB_ADDR              0x30
%define BP_FB_PITCH             0x38
%define BP_FB_WIDTH             0x3C
%define BP_FB_HEIGHT            0x40
%define BP_FB_BPP               0x44
%define BP_FB_RED_POS           0x45
%define BP_FB_RED_SIZE          0x46
%define BP_FB_GREEN_POS         0x47
%define BP_FB_GREEN_SIZE        0x48
%define BP_FB_BLUE_POS          0x49
%define BP_FB_BLUE_SIZE         0x4A
%define BP_FB_RSVD_POS          0x4B
%define BP_FB_RSVD_SIZE         0x4C
%define BP_VBE_MODE             0x50
%define BP_BOOT_DRIVE           0x54
%define BP_RESERVED             0x58
%define BOOT_PARAMS_SIZE        0x5C

%define BOOT_FLAG_FB            (1 << 0)
%define BOOT_FLAG_INITRD        (1 << 1)
%define BOOT_FLAG_FONT          (1 << 2)

%define E820_ADDR               0x5000
%define E820_MAX_ENTRIES        128

%define STAGE3_ADDR             0x20000
%define STAGE3_MAX_BYTES        0x40000         ; 0x20000..0x5FFFF
%define ISO_SCRATCH_ADDR        0x60000
%define ISO_SCRATCH_SIZE        0x10000
%define FONT_ADDR               0x70000
%define DISK_BOUNCE_ADDR        0x80000
%define DISK_BOUNCE_SEG         0x8000
%define DISK_CHUNK_BYTES        0x8000          ; 32 KiB per BIOS transfer

%define EXT2_ADDR               0x04000000
%define EXT2_MAX_BYTES          0x02000000      ; 32 MiB

%define STAGE2_STACK_TOP        0x1F000

%define GDT_ADDR                0x5600
%define VBE_MODE_INFO_ADDR      0x5700
