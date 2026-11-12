#ifndef ZENKU_BOOT_INFO_H
#define ZENKU_BOOT_INFO_H

#include <types.h>
#include <compiler.h>

#define BOOT_MAGIC        0x5A454E4B55303031ULL /* "ZENKU001" */
#define BOOT_ABI_VERSION  1

#define BOOT_FLAG_FB      (1u << 0)
#define BOOT_FLAG_INITRD  (1u << 1)
#define BOOT_FLAG_FONT    (1u << 2)
#define BOOT_FLAG_UEFI    (1u << 3)
#define BOOT_FLAG_TEXT    (1u << 4)

/* Stage2 -> stage3 parameter block, physically at 0x6000. */
#define BOOT_PARAMS_ADDR  0x6000u
#define BOOT_PARAMS_MAGIC 0x3150425Au /* "ZBP1" */

struct boot_params {
    uint32_t magic;          /* 0x00 */
    uint32_t version;        /* 0x04 */
    uint64_t e820_addr;      /* 0x08 */
    uint32_t e820_count;     /* 0x10 */
    uint32_t sector_size;    /* 0x14 */
    uint64_t initrd_addr;    /* 0x18 */
    uint64_t initrd_size;    /* 0x20 */
    uint32_t font_addr;      /* 0x28 */
    uint16_t font_height;    /* 0x2C */
    uint16_t flags;          /* 0x2E */
    uint64_t fb_addr;        /* 0x30 */
    uint32_t fb_pitch;       /* 0x38 */
    uint32_t fb_width;       /* 0x3C */
    uint32_t fb_height;      /* 0x40 */
    uint8_t  fb_bpp;         /* 0x44 */
    uint8_t  fb_red_pos;     /* 0x45 */
    uint8_t  fb_red_size;    /* 0x46 */
    uint8_t  fb_green_pos;   /* 0x47 */
    uint8_t  fb_green_size;  /* 0x48 */
    uint8_t  fb_blue_pos;    /* 0x49 */
    uint8_t  fb_blue_size;   /* 0x4A */
    uint8_t  fb_rsvd_pos;    /* 0x4B */
    uint8_t  fb_rsvd_size;   /* 0x4C */
    uint8_t  pad[3];         /* 0x4D */
    uint32_t vbe_mode;       /* 0x50 */
    uint32_t boot_drive;     /* 0x54 */
    uint32_t reserved;       /* 0x58 */
} PACKED;

/* Physical page where stage3 writes the final boot_info for the kernel. */
#define BOOT_INFO_ADDR 0x7000u

struct boot_info {
    uint64_t magic;          /* 0x000 */
    uint32_t version;        /* 0x008 */
    uint32_t flags;          /* 0x00C */
    uint64_t e820_addr;      /* 0x010 */
    uint32_t e820_count;     /* 0x018 */
    uint32_t e820_entry_size;/* 0x01C */
    uint64_t fb_addr;        /* 0x020 */
    uint32_t fb_pitch;       /* 0x028 */
    uint32_t fb_width;       /* 0x02C */
    uint32_t fb_height;      /* 0x030 */
    uint8_t  fb_bpp;         /* 0x034 */
    uint8_t  fb_red_pos;     /* 0x035 */
    uint8_t  fb_red_size;    /* 0x036 */
    uint8_t  fb_green_pos;   /* 0x037 */
    uint8_t  fb_green_size;  /* 0x038 */
    uint8_t  fb_blue_pos;    /* 0x039 */
    uint8_t  fb_blue_size;   /* 0x03A */
    uint8_t  fb_rsvd_pos;    /* 0x03B */
    uint8_t  fb_rsvd_size;   /* 0x03C */
    uint8_t  pad[3];         /* 0x03D */
    uint64_t font_addr;      /* 0x040 */
    uint32_t font_height;    /* 0x048 */
    uint32_t font_width;     /* 0x04C */
    uint64_t initrd_addr;    /* 0x050 */
    uint64_t initrd_size;    /* 0x058 */
    uint64_t kernel_phys_base;/* 0x060 */
    uint64_t kernel_size;    /* 0x068 */
    uint64_t kernel_entry;   /* 0x070 */
    uint64_t rsdp_addr;      /* 0x078 */
    uint64_t boot_drive;     /* 0x080 */
    char     cmdline[256];   /* 0x088 */
} PACKED;

struct e820_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attrs;
} PACKED;

#define E820_TYPE_USABLE   1
#define E820_TYPE_RESERVED 2
#define E820_TYPE_ACPI     3
#define E820_TYPE_NVS      4
#define E820_TYPE_UNUSABLE 5

#endif /* ZENKU_BOOT_INFO_H */
