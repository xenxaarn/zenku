#include <boot_info.h>
#include <compiler.h>
#include <fb.h>
#include <io.h>
#include <kio.h>
#include <types.h>
#include <version.h>

#define PHYS_MAP_BASE 0xFFFF800000000000ull

static inline void *phys_to_virt(uint64_t phys)
{
    return (void *)(uintptr_t)(PHYS_MAP_BASE + phys);
}

static void kpanic(const char *fmt, ...) NORETURN;

static void kpanic(const char *fmt, ...)
{
    va_list ap;

    serial_puts("\nPANIC: ");
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    serial_puts("\n");

    outb(0x501, 0x20);
    cpu_halt();
}

static uint64_t total_usable_memory(const struct boot_info *bi)
{
    const struct e820_entry *entries;
    uint64_t total = 0;

    if (!bi->e820_addr || !bi->e820_count) {
        return 0;
    }

    entries = phys_to_virt(bi->e820_addr);
    for (uint32_t i = 0; i < bi->e820_count; i++) {
        if (entries[i].type == E820_TYPE_USABLE) {
            total += entries[i].length;
        }
    }
    return total;
}

void kmain(struct boot_info *bi)
{
    uint64_t usable;

    serial_init();

    kprintf("\nZENKU KERNEL OK\n");
    kprintf("kernel: %s, 64-bit higher half\n", OS_VERSION_STRING);
    kprintf("kernel: kmain at %p\n", (void *)(uintptr_t)&kmain);
    kprintf("kernel: boot_info at %p (v%u, flags 0x%x)\n",
            (void *)bi, bi->version, bi->flags);

    if (bi->magic != BOOT_MAGIC) {
        kpanic("boot_info magic mismatch: 0x%llx", bi->magic);
    }
    if (bi->version != BOOT_ABI_VERSION) {
        kpanic("boot_info version %u, expected %u", bi->version,
               BOOT_ABI_VERSION);
    }

    usable = total_usable_memory(bi);
    kprintf("kernel: e820 %u entries, usable %llu MiB\n",
            bi->e820_count, usable / MB(1));

    kprintf("kernel: kernel phys 0x%llx size 0x%llx entry 0x%llx\n",
            bi->kernel_phys_base, bi->kernel_size, bi->kernel_entry);
    kprintf("kernel: initrd 0x%llx (%llu bytes), boot drive 0x%llx\n",
            bi->initrd_addr, bi->initrd_size, bi->boot_drive);

    if (bi->flags & BOOT_FLAG_FB) {
        kprintf("kernel: framebuffer %ux%u %ubpp pitch %u at 0x%llx\n",
                bi->fb_width, bi->fb_height, bi->fb_bpp, bi->fb_pitch,
                bi->fb_addr);
        if (fb_init(bi) == 0) {
            fb_splash(bi);
            kprintf("kernel: framebuffer banner drawn\n");
        } else {
            kprintf("kernel: framebuffer unsupported by fb driver\n");
        }
    } else {
        kprintf("kernel: no framebuffer\n");
    }

    kprintf("kernel: entering idle loop\n");
#ifdef TEST_EXIT
    outb(0x501, 0x10);
#endif
    cpu_halt();
}
