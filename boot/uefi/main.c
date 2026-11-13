/*
 * Zenku UEFI boot loader.
 *
 * The firmware loads this PE32+ application from the El Torito FAT image,
 * hands it a GOP framebuffer and a SimpleFileSystem volume, and this code
 * reproduces the stage2/stage3 contract for the kernel:
 *
 *   - copy ROOT.EXT2 into RAM and read /kernel from it with the shared ext2
 *     reader, exactly like the BIOS stage3 loader;
 *   - build the E820 map expected by the kernel PMM;
 *   - install the same page tables (identity + direct map + higher half);
 *   - call ExitBootServices and jump to the kernel entry with the boot_info
 *     block in RDI.
 *
 * The UEFI path deliberately shares the kernel ABI, the ext2 driver, and the
 * ELF layout with the BIOS path. Only the firmware-facing half differs.
 */

#include <boot_info.h>
#include <compiler.h>
#include <ext2.h>
#include <kio.h>
#include <string.h>
#include <types.h>

#include "efi.h"

#define PHYS_MAP_BASE   0xFFFF800000000000ull
#define KERNEL_VMA      0xFFFFFFFF80000000ull
#define KERNEL_MAP_SIZE MB(512)
#define KERNEL_LOAD_MIN 0x00100000ull
#define KERNEL_LOAD_MAX 0x04000000ull
#define DIRECT_MAP_LIMIT GB(4)

#define PTE_PRESENT 0x001ull
#define PTE_WRITE   0x002ull
#define PTE_HUGE    0x080ull

#define PAGE_TABLE_PHYS 0x1000ull
#define PML4_PAGES      8
#define E820_PAGES      16
#define E820_MAX_ENTRIES 4096
#define FILE_CHUNK      MB(1)

#define EFI_SYSTEM_TABLE_SIGNATURE 0x5453595320494249ull

struct mem_bdev {
    struct bdev bdev;
    const uint8_t *base;
    uint64_t size;
};

struct elf64_ehdr {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} PACKED;

struct elf64_phdr {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} PACKED;

#define PT_LOAD   1
#define ET_EXEC   2
#define EM_X86_64 62

extern const uint8_t zenku_font8x16[4096];

static struct efi_system_table *g_st;
static struct efi_boot_services *g_bs;
static EFI_HANDLE g_image;
static struct efi_graphics_output *g_gop;

static struct ext2_fs g_fs;
static struct mem_bdev g_mem;
static struct elf64_phdr g_phdrs[32];

static uint64_t g_rootfs_phys;
static uint64_t g_rootfs_size;
static uint64_t g_kernel_phys;
static uint64_t g_kernel_size;
static uint64_t g_kernel_entry;
static uint64_t g_bi_phys;
static uint64_t g_e820_phys;
static uint64_t g_font_phys;
static uint64_t g_pt_phys;

static struct e820_entry *g_e820;
static uint32_t g_e820_count;

static void conout_print(const char *text)
{
    CHAR16 line[160];
    struct efi_simple_text_output *out;

    if (!g_st || !g_st->con_out) {
        return;
    }
    out = g_st->con_out;
    efi_ascii_to_utf16(text, line, sizeof(line) / sizeof(line[0]));
    out->output_string(out, line);
}

static void boot_log(const char *message)
{
    kprintf("uefi: %s\n", message);
    conout_print("zenku: ");
    conout_print(message);
    conout_print("\r\n");
}

static void boot_fail(const char *message)
{
    kprintf("uefi: FAIL %s\n", message);
    conout_print("zenku: FAIL ");
    conout_print(message);
    conout_print("\r\n");
}

static void *phys_ptr(uint64_t phys)
{
    return (void *)(uintptr_t)phys;
}

static int alloc_pages(uint64_t pages, uint64_t *out)
{
    EFI_PHYSICAL_ADDRESS address = 0;
    EFI_STATUS status;

    status = g_bs->allocate_pages(EFI_ALLOCATE_ANY_PAGES, EFI_LOADER_DATA,
                                  (UINTN)pages, &address);
    if (EFI_ERROR(status) || address == 0 ||
        address + pages * PAGE_SIZE > DIRECT_MAP_LIMIT) {
        return -1;
    }
    memset(phys_ptr(address), 0, (size_t)(pages * PAGE_SIZE));
    *out = address;
    return 0;
}

static int alloc_pages_at(uint64_t address, uint64_t pages)
{
    EFI_PHYSICAL_ADDRESS target = address;
    EFI_STATUS status;

    status = g_bs->allocate_pages(EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA,
                                  (UINTN)pages, &target);
    if (EFI_ERROR(status) || target != address) {
        return -1;
    }
    memset(phys_ptr(address), 0, (size_t)(pages * PAGE_SIZE));
    return 0;
}

static int locate_protocol(const struct efi_guid *guid, void **out)
{
    return EFI_ERROR(g_bs->locate_protocol((struct efi_guid *)guid, NULL, out))
           ? -1 : 0;
}

/* ------------------------------------------------------------------------- */
/* Filesystem                                                                */
/* ------------------------------------------------------------------------- */

static int mem_bdev_read(struct bdev *dev, uint64_t block, void *buf,
                         uint32_t count)
{
    struct mem_bdev *mem = (struct mem_bdev *)dev->ctx;
    uint64_t offset = block * dev->block_size;
    uint64_t length = (uint64_t)count * dev->block_size;

    if (offset + length > mem->size || offset + length < offset) {
        return -1;
    }
    memcpy(buf, mem->base + offset, (size_t)length);
    return 0;
}

static int open_rootfs(void)
{
    struct efi_guid loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    struct efi_guid sfs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    struct efi_guid info_guid = EFI_FILE_INFO_GUID;
    struct efi_loaded_image *loaded = NULL;
    struct efi_simple_file_system *sfs = NULL;
    struct efi_file *root = NULL;
    struct efi_file *file = NULL;
    struct efi_file_info info;
    CHAR16 name[32];
    UINTN info_size = 1024;
    UINTN chunk;
    uint64_t offset = 0;
    uint64_t pages;

    if (EFI_ERROR(g_bs->handle_protocol(g_image, &loaded_guid,
                                        (void **)&loaded)) || !loaded) {
        boot_fail("loaded-image protocol unavailable");
        return -1;
    }
    if (EFI_ERROR(g_bs->handle_protocol(loaded->device_handle, &sfs_guid,
                                        (void **)&sfs)) || !sfs) {
        boot_fail("simple-file-system protocol unavailable");
        return -1;
    }
    if (EFI_ERROR(sfs->open_volume(sfs, &root)) || !root) {
        boot_fail("cannot open the EFI volume");
        return -1;
    }

    efi_ascii_to_utf16("\\ROOT.EXT2", name, sizeof(name) / sizeof(name[0]));
    if (EFI_ERROR(root->open(root, &file, name, EFI_FILE_MODE_READ, 0))) {
        boot_fail("ROOT.EXT2 is missing from the EFI image");
        root->close(root);
        return -1;
    }

    memset(&info, 0, sizeof(info));
    /* GetInfo copies the variable-length file name as well, so give the
     * firmware more room than sizeof(struct efi_file_info) provides. */
    if (EFI_ERROR(file->get_info(file, &info_guid, &info_size, &info)) ||
        info.file_size < 1024) {
        boot_fail("cannot read ROOT.EXT2 size");
        file->close(file);
        root->close(root);
        return -1;
    }
    if (info.file_size > MB(128)) {
        boot_fail("ROOT.EXT2 is unexpectedly large");
        file->close(file);
        root->close(root);
        return -1;
    }

    pages = (info.file_size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (alloc_pages(pages, &g_rootfs_phys) != 0) {
        boot_fail("out of memory for ROOT.EXT2");
        file->close(file);
        root->close(root);
        return -1;
    }

    g_rootfs_size = info.file_size;
    while (offset < g_rootfs_size) {
        chunk = (UINTN)(g_rootfs_size - offset);
        if (chunk > FILE_CHUNK) {
            chunk = FILE_CHUNK;
        }
        if (EFI_ERROR(file->read(file, &chunk,
                                 phys_ptr(g_rootfs_phys + offset))) ||
            chunk == 0) {
            boot_fail("short read from ROOT.EXT2");
            file->close(file);
            root->close(root);
            return -1;
        }
        offset += chunk;
    }

    file->close(file);
    root->close(root);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Graphics                                                                  */
/* ------------------------------------------------------------------------- */

static int setup_gop(void)
{
    struct efi_guid guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    struct efi_gop_mode_info *info;
    UINTN size;
    uint32_t best = 0;
    bool found = false;

    if (locate_protocol(&guid, (void **)&g_gop) != 0 || !g_gop) {
        boot_fail("no GOP framebuffer");
        return -1;
    }

    for (uint32_t mode = 0; mode < g_gop->mode->max_mode; mode++) {
        if (EFI_ERROR(g_gop->query_mode(g_gop, mode, &size, &info))) {
            continue;
        }
        if (info->pixel_format == EFI_PIXEL_BLT_ONLY) {
            continue;
        }
        if (info->horizontal_resolution == 1024 &&
            info->vertical_resolution == 768) {
            best = mode;
            found = true;
            break;
        }
        if (!found && info->horizontal_resolution >= 800) {
            best = mode;
            found = true;
        }
    }
    if (!found) {
        boot_fail("no usable GOP mode");
        return -1;
    }
    if (EFI_ERROR(g_gop->set_mode(g_gop, best))) {
        boot_fail("GOP set_mode failed");
        return -1;
    }
    if (!g_gop->mode->info ||
        g_gop->mode->info->pixel_format == EFI_PIXEL_BLT_ONLY) {
        boot_fail("GOP mode has no linear framebuffer");
        return -1;
    }
    if (g_gop->mode->frame_buffer_base == 0 ||
        g_gop->mode->frame_buffer_base >= DIRECT_MAP_LIMIT) {
        boot_fail("GOP framebuffer is outside the direct map");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Kernel loading                                                            */
/* ------------------------------------------------------------------------- */

static int read_kernel_headers(struct elf64_ehdr *eh, struct ext2_inode *inode)
{
    uint32_t ino;
    uint32_t ph_bytes;

    if (ext2_lookup(&g_fs, "/kernel", &ino) != 0) {
        boot_fail("kernel not found in ROOT.EXT2");
        return -1;
    }
    if (ext2_read_inode(&g_fs, ino, inode) != 0 || !ext2_is_file(inode)) {
        boot_fail("kernel is not a regular file");
        return -1;
    }
    if (ext2_inode_size(&g_fs, inode) < sizeof(*eh) ||
        ext2_read(&g_fs, inode, 0, eh, sizeof(*eh)) != (int64_t)sizeof(*eh)) {
        boot_fail("short ELF header");
        return -1;
    }
    if (memcmp(eh->ident, "\x7f" "ELF", 4) != 0 ||
        eh->ident[4] != 2 || eh->ident[5] != 1) {
        boot_fail("kernel is not ELF64 little-endian");
        return -1;
    }
    if (eh->type != ET_EXEC || eh->machine != EM_X86_64) {
        boot_fail("kernel is not an x86_64 executable");
        return -1;
    }
    if (eh->phentsize != sizeof(struct elf64_phdr) || eh->phnum == 0 ||
        eh->phnum > ARRAY_SIZE(g_phdrs)) {
        boot_fail("unsupported program header table");
        return -1;
    }

    ph_bytes = (uint32_t)eh->phnum * sizeof(struct elf64_phdr);
    if (ext2_read(&g_fs, inode, eh->phoff, g_phdrs, ph_bytes) !=
        (int64_t)ph_bytes) {
        boot_fail("short program header table");
        return -1;
    }
    return 0;
}

static uint64_t segment_phys(const struct elf64_phdr *ph)
{
    return ph->paddr ? ph->paddr : ph->vaddr - KERNEL_VMA;
}

static int load_kernel(void)
{
    struct elf64_ehdr eh;
    struct ext2_inode inode;
    uint64_t phys_min = UINT64_MAX;
    uint64_t phys_max = 0;
    uint64_t pages;

    if (read_kernel_headers(&eh, &inode) != 0) {
        return -1;
    }

    for (uint16_t i = 0; i < eh.phnum; i++) {
        const struct elf64_phdr *ph = &g_phdrs[i];
        uint64_t dst;
        uint64_t end;

        if (ph->type != PT_LOAD || ph->memsz == 0) {
            continue;
        }
        if (ph->filesz > ph->memsz) {
            boot_fail("ELF segment file size exceeds memory size");
            return -1;
        }
        dst = segment_phys(ph);
        end = dst + ph->memsz;
        if ((dst & (PAGE_SIZE - 1)) || dst < KERNEL_LOAD_MIN ||
            end > KERNEL_LOAD_MAX || end < dst) {
            boot_fail("ELF segment outside the kernel window");
            return -1;
        }
        if (dst < phys_min) {
            phys_min = dst;
        }
        if (end > phys_max) {
            phys_max = end;
        }
    }
    if (phys_min == UINT64_MAX) {
        boot_fail("kernel has no loadable segments");
        return -1;
    }
    if (eh.entry < KERNEL_VMA || eh.entry >= KERNEL_VMA + KERNEL_MAP_SIZE) {
        boot_fail("kernel entry is outside the kernel mapping");
        return -1;
    }

    pages = (phys_max - phys_min + PAGE_SIZE - 1) / PAGE_SIZE;
    if (alloc_pages_at(phys_min, pages) != 0) {
        boot_fail("cannot reserve the kernel physical range");
        return -1;
    }

    for (uint16_t i = 0; i < eh.phnum; i++) {
        const struct elf64_phdr *ph = &g_phdrs[i];
        uint64_t dst;

        if (ph->type != PT_LOAD || ph->memsz == 0) {
            continue;
        }
        dst = segment_phys(ph);
        memset(phys_ptr(dst), 0, (size_t)ph->memsz);
        if (ph->filesz &&
            ext2_read(&g_fs, &inode, ph->offset, phys_ptr(dst), ph->filesz) !=
            (int64_t)ph->filesz) {
            boot_fail("short kernel segment read");
            return -1;
        }
    }

    g_kernel_phys = phys_min;
    g_kernel_size = phys_max - phys_min;
    g_kernel_entry = eh.entry;
    kprintf("uefi: kernel phys 0x%llx size 0x%llx entry 0x%llx\n",
            g_kernel_phys, g_kernel_size, g_kernel_entry);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* E820 map                                                                  */
/* ------------------------------------------------------------------------- */

static uint32_t efi_type_to_e820(uint32_t type)
{
    switch (type) {
    case EFI_CONVENTIONAL_MEMORY:
    case EFI_BOOT_SERVICES_CODE:
    case EFI_BOOT_SERVICES_DATA:
    case EFI_LOADER_CODE:
    case EFI_LOADER_DATA:
        return E820_TYPE_USABLE;
    case EFI_ACPI_RECLAIM_MEMORY:
        return E820_TYPE_ACPI;
    case EFI_ACPI_MEMORY_NVS:
        return E820_TYPE_NVS;
    case EFI_UNUSABLE_MEMORY:
        return E820_TYPE_UNUSABLE;
    default:
        return E820_TYPE_RESERVED;
    }
}

static void e820_push(uint64_t base, uint64_t length, uint32_t type)
{
    struct e820_entry *last;

    if (length == 0 || base >= DIRECT_MAP_LIMIT ||
        g_e820_count >= E820_MAX_ENTRIES) {
        return;
    }
    if (base + length > DIRECT_MAP_LIMIT) {
        length = DIRECT_MAP_LIMIT - base;
    }

    last = g_e820_count ? &g_e820[g_e820_count - 1] : NULL;
    if (last && last->type == type && last->base + last->length == base) {
        last->length += length;
        return;
    }

    g_e820[g_e820_count].base = base;
    g_e820[g_e820_count].length = length;
    g_e820[g_e820_count].type = type;
    g_e820[g_e820_count].attrs = 0;
    g_e820_count++;
}

static void e820_build(const struct efi_memory_descriptor *map, UINTN count,
                       UINTN descriptor_size)
{
    g_e820_count = 0;

    for (UINTN i = 0; i < count; i++) {
        const struct efi_memory_descriptor *desc =
            (const struct efi_memory_descriptor *)
            ((const uint8_t *)map + i * descriptor_size);

        e820_push(desc->physical_start,
                  desc->number_of_pages * PAGE_SIZE,
                  efi_type_to_e820(desc->type));
    }
}

static void e820_reserve(uint64_t base, uint64_t length)
{
    static struct e820_entry result[E820_MAX_ENTRIES];
    uint64_t end;
    uint32_t count = 0;

    if (length == 0) {
        return;
    }
    base &= ~(PAGE_SIZE - 1);
    end = (base + length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uint32_t i = 0; i < g_e820_count && count < E820_MAX_ENTRIES; i++) {
        struct e820_entry *entry = &g_e820[i];
        uint64_t start = entry->base;
        uint64_t limit = entry->base + entry->length;

        if (limit <= base || start >= end) {
            result[count++] = *entry;
            continue;
        }
        if (start < base) {
            result[count].base = start;
            result[count].length = base - start;
            result[count].type = entry->type;
            result[count].attrs = entry->attrs;
            count++;
        }
        if (limit > end && count < E820_MAX_ENTRIES) {
            result[count].base = end;
            result[count].length = limit - end;
            result[count].type = entry->type;
            result[count].attrs = entry->attrs;
            count++;
        }
    }

    memcpy(g_e820, result, (size_t)count * sizeof(result[0]));
    g_e820_count = count;
}

static void e820_reserve_loader_memory(void)
{
    e820_reserve(g_bi_phys, PAGE_SIZE);
    e820_reserve(g_e820_phys, E820_PAGES * PAGE_SIZE);
    e820_reserve(g_font_phys, PAGE_SIZE);
    e820_reserve(g_pt_phys, PML4_PAGES * PAGE_SIZE);
    e820_reserve(g_kernel_phys, g_kernel_size);
    e820_reserve(g_rootfs_phys, g_rootfs_size);
}

/* ------------------------------------------------------------------------- */
/* Page tables and final hand-off                                            */
/* ------------------------------------------------------------------------- */

static void build_page_tables(uint64_t base)
{
    uint64_t *pml4 = (uint64_t *)(uintptr_t)base;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(base + PAGE_SIZE);
    uint64_t *kernel_pdpt = (uint64_t *)(uintptr_t)(base + 6 * PAGE_SIZE);
    uint64_t *kernel_pd = (uint64_t *)(uintptr_t)(base + 7 * PAGE_SIZE);

    memset(pml4, 0, PML4_PAGES * PAGE_SIZE);

    /* Identity map and direct map share the same 4 GiB of 2 MiB pages. */
    for (unsigned p = 0; p < 4; p++) {
        uint64_t *table = (uint64_t *)(uintptr_t)(base + (2 + p) * PAGE_SIZE);

        for (unsigned j = 0; j < 512; j++) {
            table[j] = ((uint64_t)p * GB(1) + (uint64_t)j * MB(2)) |
                       PTE_PRESENT | PTE_WRITE | PTE_HUGE;
        }
        pdpt[p] = (uint64_t)(uintptr_t)table | PTE_PRESENT | PTE_WRITE;
    }
    pml4[0] = (uint64_t)(uintptr_t)pdpt | PTE_PRESENT | PTE_WRITE;
    pml4[256] = (uint64_t)(uintptr_t)pdpt | PTE_PRESENT | PTE_WRITE;

    /* The kernel window maps the first 512 MiB at 0xFFFFFFFF80000000. */
    for (unsigned j = 0; j < 256; j++) {
        kernel_pd[j] = ((uint64_t)j * MB(2)) | PTE_PRESENT | PTE_WRITE |
                       PTE_HUGE;
    }
    kernel_pdpt[510] = (uint64_t)(uintptr_t)kernel_pd |
                       PTE_PRESENT | PTE_WRITE;
    pml4[511] = (uint64_t)(uintptr_t)kernel_pdpt | PTE_PRESENT | PTE_WRITE;
}

static int prepare_boot_info(void)
{
    struct boot_info *bi;
    struct efi_gop_mode_info *info;
    struct efi_pixel_bitmask mask;
    uint32_t red_pos = 0;
    uint32_t red_size = 8;
    uint32_t green_pos = 8;
    uint32_t green_size = 8;
    uint32_t blue_pos = 16;
    uint32_t blue_size = 8;
    uint64_t pages;

    info = g_gop->mode->info;
    mask = info->pixel_information;

    if (info->pixel_format == EFI_PIXEL_BGR_RESERVED_8BIT_PER_COLOR) {
        red_pos = 16;
        blue_pos = 0;
    } else if (info->pixel_format == EFI_PIXEL_BIT_MASK) {
        if (__builtin_popcount(mask.red_mask) != 8 ||
            __builtin_popcount(mask.green_mask) != 8 ||
            __builtin_popcount(mask.blue_mask) != 8) {
            boot_fail("GOP pixel format is not 8:8:8");
            return -1;
        }
        red_pos = __builtin_ctz(mask.red_mask);
        green_pos = __builtin_ctz(mask.green_mask);
        blue_pos = __builtin_ctz(mask.blue_mask);
    }

    pages = (sizeof(struct boot_info) + PAGE_SIZE - 1) / PAGE_SIZE;
    if (alloc_pages(pages, &g_bi_phys) != 0 ||
        alloc_pages(E820_PAGES, &g_e820_phys) != 0 ||
        alloc_pages(1, &g_font_phys) != 0 ||
        alloc_pages(PML4_PAGES, &g_pt_phys) != 0) {
        boot_fail("out of memory for boot tables");
        return -1;
    }

    memcpy(phys_ptr(g_font_phys), zenku_font8x16, 4096);

    bi = (struct boot_info *)phys_ptr(g_bi_phys);
    memset(bi, 0, sizeof(*bi));
    bi->magic = BOOT_MAGIC;
    bi->version = BOOT_ABI_VERSION;
    bi->flags = BOOT_FLAG_FB | BOOT_FLAG_FONT | BOOT_FLAG_UEFI;
    bi->e820_addr = g_e820_phys;
    bi->e820_entry_size = sizeof(struct e820_entry);
    bi->fb_addr = g_gop->mode->frame_buffer_base;
    bi->fb_pitch = info->pixels_per_scan_line * 4;
    bi->fb_width = info->horizontal_resolution;
    bi->fb_height = info->vertical_resolution;
    bi->fb_bpp = 32;
    bi->fb_red_pos = (uint8_t)red_pos;
    bi->fb_red_size = (uint8_t)red_size;
    bi->fb_green_pos = (uint8_t)green_pos;
    bi->fb_green_size = (uint8_t)green_size;
    bi->fb_blue_pos = (uint8_t)blue_pos;
    bi->fb_blue_size = (uint8_t)blue_size;
    bi->font_addr = g_font_phys;
    bi->font_height = 16;
    bi->font_width = 8;
    bi->initrd_addr = g_rootfs_phys;
    bi->initrd_size = g_rootfs_size;
    bi->kernel_phys_base = g_kernel_phys;
    bi->kernel_size = g_kernel_size;
    bi->kernel_entry = g_kernel_entry;
    bi->boot_drive = 0;

    build_page_tables(g_pt_phys);
    kprintf("uefi: framebuffer %ux%u pitch %u at 0x%llx\n",
            bi->fb_width, bi->fb_height, bi->fb_pitch, bi->fb_addr);
    return 0;
}

static void jump_to_kernel(uint64_t pml4_phys, uint64_t entry,
                           uint64_t boot_info_phys) NORETURN;

static void jump_to_kernel(uint64_t pml4_phys, uint64_t entry,
                           uint64_t boot_info_phys)
{
    __asm__ volatile(
        "cli\n\t"
        "mov %0, %%cr3\n\t"
        "mov %1, %%rdi\n\t"
        "jmp *%2\n\t"
        :
        : "r"(pml4_phys), "r"(boot_info_phys), "r"(entry)
        : "memory");

    __builtin_unreachable();
}

/* ------------------------------------------------------------------------- */
/* Entry point                                                               */
/* ------------------------------------------------------------------------- */

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, struct efi_system_table *st)
{
    struct boot_info *bi;
    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    UINTN descriptor_count;
    UINTN map_capacity;
    uint32_t descriptor_version = 0;
    struct efi_memory_descriptor *map = NULL;
    EFI_STATUS status;

    g_st = st;
    g_bs = st ? st->boot_services : NULL;
    g_image = image;

    if (!st || !g_bs || st->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE) {
        return EFI_ERROR_BIT | 1;
    }

    st->con_out->reset(st->con_out, false);
    g_bs->set_watchdog_timer(0, 0, 0, NULL);
    serial_init();

    kprintf("\nZENKU UEFI OK\n");
    conout_print("zenku: UEFI loader starting\r\n");

    if (open_rootfs() != 0) {
        return EFI_ERROR_BIT | 2;
    }
    boot_log("ROOT.EXT2 loaded");

    g_mem.base = (const uint8_t *)phys_ptr(g_rootfs_phys);
    g_mem.size = g_rootfs_size;
    g_mem.bdev.ctx = &g_mem;
    g_mem.bdev.block_size = 512;
    g_mem.bdev.block_count = g_rootfs_size / 512;
    g_mem.bdev.read = mem_bdev_read;

    if (ext2_mount(&g_fs, &g_mem.bdev) != 0) {
        boot_fail("ext2 mount failed");
        return EFI_ERROR_BIT | 3;
    }
    kprintf("uefi: ext2 %u-byte blocks, %u groups, %u inodes\n",
            g_fs.block_size, g_fs.group_count, g_fs.inodes_count);

    if (load_kernel() != 0) {
        return EFI_ERROR_BIT | 4;
    }
    if (setup_gop() != 0) {
        return EFI_ERROR_BIT | 5;
    }
    boot_log("GOP framebuffer ready");

    if (prepare_boot_info() != 0) {
        return EFI_ERROR_BIT | 6;
    }
    boot_log("page tables ready");

    /* Size the UEFI memory map and allocate a buffer with room to spare. */
    status = g_bs->get_memory_map(&map_size, NULL, &map_key, &descriptor_size,
                                  &descriptor_version);
    if (status != EFI_SUCCESS && (status & ~EFI_ERROR_BIT) != 5) {
        boot_fail("GetMemoryMap sizing failed");
        return EFI_ERROR_BIT | 7;
    }
    map_capacity = map_size + descriptor_size * 8;
    if (EFI_ERROR(g_bs->allocate_pool(EFI_LOADER_DATA, map_capacity,
                                      (void **)&map))) {
        boot_fail("out of memory for the UEFI memory map");
        return EFI_ERROR_BIT | 8;
    }

    for (unsigned attempt = 0; attempt < 4; attempt++) {
        map_size = map_capacity;
        if (EFI_ERROR(g_bs->get_memory_map(&map_size, map, &map_key,
                                           &descriptor_size,
                                           &descriptor_version))) {
            boot_fail("GetMemoryMap failed");
            return EFI_ERROR_BIT | 9;
        }
        descriptor_count = map_size / descriptor_size;

        g_e820 = (struct e820_entry *)phys_ptr(g_e820_phys);
        e820_build(map, descriptor_count, descriptor_size);
        e820_reserve_loader_memory();

        bi = (struct boot_info *)phys_ptr(g_bi_phys);
        bi->e820_addr = g_e820_phys;
        bi->e820_count = g_e820_count;
        bi->e820_entry_size = sizeof(struct e820_entry);

        if (EFI_SUCCESS == g_bs->exit_boot_services(image, map_key)) {
            break;
        }
        if (attempt == 3) {
            boot_fail("ExitBootServices failed");
            return EFI_ERROR_BIT | 10;
        }
    }

    kprintf("uefi: e820 %u entries, firmware services exited\n", g_e820_count);
    kprintf("uefi: jumping to kernel 0x%llx\n", g_kernel_entry);

    /* The kernel ABI fixes VMM_PML4_PHYS at 0x1000; install the template. */
    memcpy(phys_ptr(PAGE_TABLE_PHYS), phys_ptr(g_pt_phys), PAGE_SIZE);
    jump_to_kernel(PAGE_TABLE_PHYS, g_kernel_entry, g_bi_phys);
}
