/*
 * Zenku stage3: read the ext2 root image, load /kernel, build the kernel
 * address space, and hand control to the higher-half kernel.
 */

#include <boot_info.h>
#include <ext2.h>
#include <io.h>
#include <kio.h>
#include <string.h>
#include <types.h>

#define PHYS_MAP_BASE   0xFFFF800000000000ull
#define KERNEL_VMA      0xFFFFFFFF80000000ull
#define KERNEL_MAP_SIZE MB(512)
#define KERNEL_LOAD_MIN 0x00100000ull
#define KERNEL_LOAD_MAX 0x04000000ull   /* stop before the ext2 image */

#define PTE_PRESENT 0x001ull
#define PTE_WRITE   0x002ull
#define PTE_HUGE    0x080ull

#define PAGE_TABLE_PHYS 0x1000ull

#define EXIT_MOUNT   0x21
#define EXIT_LOOKUP  0x22
#define EXIT_ELF     0x23
#define EXIT_LOAD    0x24
#define EXIT_PAGING  0x25

struct mem_bdev {
    struct bdev bdev;
    const uint8_t *base;
    uint64_t size;
};

struct elf64_ehdr {
    uint8_t  ident[16];
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

static struct ext2_fs g_fs;
static struct mem_bdev g_mem;
static struct elf64_phdr g_phdrs[32];

static uint64_t g_physmap_pdpt[512] ALIGNED(4096);
static uint64_t g_physmap_pd[4][512] ALIGNED(4096);
static uint64_t g_kernel_pdpt[512] ALIGNED(4096);
static uint64_t g_kernel_pd[512] ALIGNED(4096);

extern void stage3_jump_to_kernel(uint64_t entry, uint64_t boot_info_phys) NORETURN;

static void stage3_fail(unsigned code, const char *message)
{
    kprintf("stage3: FAIL %s (code 0x%x)\n", message, code);
    outb(0x501, (uint8_t)code);
    cpu_halt();
}

static int mem_bdev_read(struct bdev *dev, uint64_t block, void *buf,
                         uint32_t count)
{
    struct mem_bdev *mem = (struct mem_bdev *)dev->ctx;
    uint64_t offset = block * dev->block_size;
    uint64_t length = (uint64_t)count * dev->block_size;

    if (offset + length > mem->size) {
        return -1;
    }
    memcpy(buf, mem->base + offset, length);
    return 0;
}

static void normalize_e820(struct boot_info *bi, const struct boot_params *bp)
{
    struct e820_entry *dst = (struct e820_entry *)(uintptr_t)BOOT_E820_NORM_ADDR;
    const uint8_t *src;
    uint32_t count = bp->e820_count;

    if (!bp->e820_addr) {
        bi->e820_addr = 0;
        bi->e820_count = 0;
        bi->e820_entry_size = sizeof(struct e820_entry);
        return;
    }
    if (count > 128) {
        count = 128;
    }

    /* stage2 stores 24-byte ACPI 3.0 entries, matching struct e820_entry. */
    src = (const uint8_t *)(uintptr_t)bp->e820_addr;
    for (uint32_t i = 0; i < count; i++) {
        memcpy(&dst[i], src + (uint64_t)i * sizeof(struct e820_entry),
               sizeof(struct e820_entry));
    }

    bi->e820_addr = BOOT_E820_NORM_ADDR;
    bi->e820_count = count;
    bi->e820_entry_size = sizeof(struct e820_entry);
}

static void fill_boot_info(struct boot_info *bi, const struct boot_params *bp,
                           uint64_t phys_base, uint64_t size,
                           uint64_t entry)
{
    memset(bi, 0, sizeof(*bi));

    bi->magic = BOOT_MAGIC;
    bi->version = BOOT_ABI_VERSION;
    bi->flags = bp->flags;

    normalize_e820(bi, bp);

    bi->fb_addr = bp->fb_addr;
    bi->fb_pitch = bp->fb_pitch;
    bi->fb_width = bp->fb_width;
    bi->fb_height = bp->fb_height;
    bi->fb_bpp = bp->fb_bpp;
    bi->fb_red_pos = bp->fb_red_pos;
    bi->fb_red_size = bp->fb_red_size;
    bi->fb_green_pos = bp->fb_green_pos;
    bi->fb_green_size = bp->fb_green_size;
    bi->fb_blue_pos = bp->fb_blue_pos;
    bi->fb_blue_size = bp->fb_blue_size;
    bi->fb_rsvd_pos = bp->fb_rsvd_pos;
    bi->fb_rsvd_size = bp->fb_rsvd_size;

    bi->font_addr = bp->font_addr;
    bi->font_height = bp->font_height;
    bi->font_width = 8;

    bi->initrd_addr = bp->initrd_addr;
    bi->initrd_size = bp->initrd_size;

    bi->kernel_phys_base = phys_base;
    bi->kernel_size = size;
    bi->kernel_entry = entry;

    bi->rsdp_addr = 0;
    bi->boot_drive = bp->boot_drive;
}

static int load_kernel(struct ext2_fs *fs, struct boot_info *bi)
{
    struct ext2_inode inode;
    struct elf64_ehdr eh;
    uint32_t ino = 0;
    uint32_t ph_bytes;
    uint64_t phys_min = UINT64_MAX;
    uint64_t phys_max = 0;

    if (ext2_lookup(fs, "/kernel", &ino) != 0) {
        stage3_fail(EXIT_LOOKUP, "kernel not found");
    }
    if (ext2_read_inode(fs, ino, &inode) != 0 || !ext2_is_file(&inode)) {
        stage3_fail(EXIT_LOOKUP, "kernel is not a regular file");
    }

    uint64_t file_size = ext2_inode_size(fs, &inode);
    kprintf("stage3: kernel inode %u, %llu bytes\n", ino, file_size);

    if (file_size < sizeof(eh) ||
        ext2_read(fs, &inode, 0, &eh, sizeof(eh)) != (int64_t)sizeof(eh)) {
        stage3_fail(EXIT_ELF, "short ELF header");
    }

    if (memcmp(eh.ident, "\x7f" "ELF", 4) != 0 ||
        eh.ident[4] != 2 || eh.ident[5] != 1) {
        stage3_fail(EXIT_ELF, "not an ELF64 little-endian file");
    }
    if (eh.type != ET_EXEC || eh.machine != EM_X86_64) {
        stage3_fail(EXIT_ELF, "not an x86_64 executable");
    }
    if (eh.phentsize != sizeof(struct elf64_phdr) ||
        eh.phnum == 0 || eh.phnum > ARRAY_SIZE(g_phdrs)) {
        stage3_fail(EXIT_ELF, "unsupported program header table");
    }

    ph_bytes = (uint32_t)eh.phnum * sizeof(struct elf64_phdr);
    if (ext2_read(fs, &inode, eh.phoff, g_phdrs, ph_bytes) !=
        (int64_t)ph_bytes) {
        stage3_fail(EXIT_ELF, "short program headers");
    }

    for (uint16_t i = 0; i < eh.phnum; i++) {
        const struct elf64_phdr *ph = &g_phdrs[i];
        uint64_t dst;
        uint64_t end;

        if (ph->type != PT_LOAD) {
            continue;
        }
        if (ph->memsz == 0) {
            continue;
        }
        if (ph->filesz > ph->memsz) {
            stage3_fail(EXIT_ELF, "segment file size exceeds memory size");
        }

        dst = ph->paddr ? ph->paddr : ph->vaddr - KERNEL_VMA;
        end = dst + ph->memsz;
        if (dst < KERNEL_LOAD_MIN || end > KERNEL_LOAD_MAX ||
            end < dst) {
            stage3_fail(EXIT_LOAD, "segment outside the kernel window");
        }

        memset((void *)(uintptr_t)dst, 0, ph->memsz);
        if (ph->filesz &&
            ext2_read(fs, &inode, ph->offset, (void *)(uintptr_t)dst,
                      ph->filesz) != (int64_t)ph->filesz) {
            stage3_fail(EXIT_LOAD, "short segment read");
        }

        kprintf("stage3: segment phys 0x%llx..0x%llx (filesz 0x%llx)\n",
                dst, end, ph->filesz);

        if (dst < phys_min) {
            phys_min = dst;
        }
        if (end > phys_max) {
            phys_max = end;
        }
    }

    if (phys_min == UINT64_MAX) {
        stage3_fail(EXIT_ELF, "no loadable segments");
    }
    if (eh.entry < KERNEL_VMA || eh.entry >= KERNEL_VMA + KERNEL_MAP_SIZE) {
        stage3_fail(EXIT_ELF, "entry point outside the kernel mapping");
    }

    fill_boot_info(bi, (const struct boot_params *)(uintptr_t)0x6000, phys_min,
                   phys_max - phys_min, eh.entry);

    kprintf("stage3: kernel phys 0x%llx size 0x%llx entry 0x%llx\n",
            phys_min, phys_max - phys_min, eh.entry);
    return 0;
}

static void build_page_tables(void)
{
    uint64_t *pml4 = (uint64_t *)(uintptr_t)PAGE_TABLE_PHYS;

    /* Direct physical map of the first 4 GiB at PHYS_MAP_BASE. One PDPT page
     * with four entries, each pointing at its own page directory. */
    for (unsigned j = 0; j < 512; j++) {
        g_physmap_pdpt[j] = 0;
    }
    for (unsigned p = 0; p < 4; p++) {
        for (unsigned j = 0; j < 512; j++) {
            g_physmap_pd[p][j] = ((uint64_t)p * GB(1) + (uint64_t)j * MB(2)) |
                                 PTE_PRESENT | PTE_WRITE | PTE_HUGE;
        }
        g_physmap_pdpt[p] = (uint64_t)(uintptr_t)&g_physmap_pd[p][0] |
                            PTE_PRESENT | PTE_WRITE;
    }
    pml4[256] = (uint64_t)(uintptr_t)&g_physmap_pdpt[0] |
                PTE_PRESENT | PTE_WRITE;

    /* Kernel image window: 512 MiB at 0xFFFFFFFF80000000. */
    for (unsigned j = 0; j < 512; j++) {
        g_kernel_pd[j] = 0;
        g_kernel_pdpt[j] = 0;
    }
    for (unsigned j = 0; j < 256; j++) {
        g_kernel_pd[j] = ((uint64_t)j * MB(2)) | PTE_PRESENT | PTE_WRITE |
                         PTE_HUGE;
    }
    g_kernel_pdpt[510] = (uint64_t)(uintptr_t)&g_kernel_pd[0] |
                         PTE_PRESENT | PTE_WRITE;
    pml4[511] = (uint64_t)(uintptr_t)&g_kernel_pdpt[0] |
                PTE_PRESENT | PTE_WRITE;
}

void stage3_main(struct boot_params *bp)
{
    struct boot_info *bi = (struct boot_info *)(uintptr_t)BOOT_INFO_ADDR;

    serial_init();
    kprintf("ZENKU STAGE3 OK\n");

    if (!bp || bp->magic != BOOT_PARAMS_MAGIC) {
        stage3_fail(EXIT_MOUNT, "bad boot_params magic");
    }
    kprintf("stage3: boot_params v%u, initrd 0x%llx (%llu bytes)\n",
            bp->version, bp->initrd_addr, bp->initrd_size);

    if (bp->initrd_addr == 0 || bp->initrd_size == 0) {
        stage3_fail(EXIT_MOUNT, "missing ext2 root image");
    }

    g_mem.base = (const uint8_t *)(uintptr_t)bp->initrd_addr;
    g_mem.size = bp->initrd_size;
    g_mem.bdev.ctx = &g_mem;
    g_mem.bdev.block_size = 512;
    g_mem.bdev.block_count = g_mem.size / 512;
    g_mem.bdev.read = mem_bdev_read;

    if (ext2_mount(&g_fs, &g_mem.bdev) != 0) {
        stage3_fail(EXIT_MOUNT, "ext2 mount failed");
    }
    kprintf("stage3: ext2 %u-byte blocks, %u groups, %u inodes\n",
            g_fs.block_size, g_fs.group_count, g_fs.inodes_count);

    load_kernel(&g_fs, bi);

    build_page_tables();
    kprintf("stage3: paging ready (physmap 0x%llx, kernel 0x%llx)\n",
            PHYS_MAP_BASE, KERNEL_VMA);
    kprintf("stage3: jumping to kernel\n");

    stage3_jump_to_kernel(bi->kernel_entry, BOOT_INFO_ADDR);
}
