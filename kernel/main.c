#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/irq.h>
#include <boot_info.h>
#include <compiler.h>
#include <console.h>
#include <desktop.h>
#include <fb.h>
#include <io.h>
#include <kio.h>
#include <kpanic.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mouse.h>
#include <pic.h>
#include <pit.h>
#include <ps2.h>
#include <serial.h>
#include <types.h>
#include <version.h>

#define PHYS_MAP_BASE 0xFFFF800000000000ull

/* Kernel boot stack, defined by arch/x86_64/entry.asm. */
extern uint8_t boot_stack_top[];

static inline void *phys_to_virt(uint64_t phys)
{
    return (void *)(uintptr_t)(PHYS_MAP_BASE + phys);
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

#ifdef FAULT_TEST
static void fault_selftest(void)
{
#if FAULT_TEST == 0
    volatile int zero = 0;
    volatile int one = 1;

    kprintf("kernel: triggering #DE (divide by zero)\n");
    kprintf("kernel: result %d\n", one / zero);
#elif FAULT_TEST == 1
    kprintf("kernel: triggering #UD (ud2)\n");
    __asm__ volatile("ud2");
#elif FAULT_TEST == 2
    kprintf("kernel: triggering #GP (invalid SS load)\n");
    __asm__ volatile("movw $0x28, %%ax; movw %%ax, %%ss" : : : "rax");
#elif FAULT_TEST == 3
    kprintf("kernel: triggering #PF (unmapped kernel write)\n");
    *(volatile uint64_t *)(uintptr_t)0xFFFFFFFFB0000000ull = 0xDEADBEEFull;
#endif

    kprintf("kernel: fault selftest returned unexpectedly\n");
    kpanic("fault selftest (vector %d) did not fault", FAULT_TEST);
}
#endif

static void cpu_init(void)
{
    gdt_init((uint64_t)(uintptr_t)boot_stack_top);
    kprintf("kernel: gdt loaded, tss rsp0 0x%llx, double-fault IST ready\n",
            tss_get_rsp0());

    idt_init();
    irq_init();

    pic_remap(IDT_IRQ_BASE, IDT_IRQ_BASE + 8);
    pic_disable();
    pit_init(100);
    pic_unmask(0);
    pic_unmask(2);

    kprintf("kernel: idt %u vectors, pic remapped to 0x%x/0x%x\n",
            IDT_VECTORS, IDT_IRQ_BASE, IDT_IRQ_BASE + 8);
    kprintf("kernel: pit configured at %u Hz\n", pit_frequency());

    ps2_init();
    kprintf("kernel: ps2 keyboard irq1 enabled\n");

    serial_enable_rx_irq();
    kprintf("kernel: serial irq4 receive enabled\n");
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

    pmm_init(bi);
    if (vmm_init() != 0) {
        kpanic("vmm: initialization failed");
    }
    heap_init();
    kprintf("kernel: memory managers ready\n");

    pmm_selftest();
    vmm_selftest();
    heap_selftest();

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

    cpu_init();
#ifdef FAULT_TEST
    fault_selftest();
#endif
    kprintf("kernel: enabling interrupts\n");
    irq_enable();

    /* Prove the timer actually fires before the kernel idles. */
    pit_wait_ticks(10);
    kprintf("kernel: timer running (%llu ticks)\n", pit_ticks());

    desktop_init(bi);

#ifdef TEST_EXIT
    kprintf("kernel: ALL TESTS PASSED\n");
    kprintf("kernel: entering idle loop\n");
    outb(0x501, 0x10);
    cpu_halt();
#endif

    kprintf("kernel: entering idle loop\n");
    bool mouse_reported = false;
    for (;;) {
        int key;

        __asm__ volatile("sti; hlt" : : : "memory");

        mouse_poll();
        desktop_tick();
        if (!mouse_reported && mouse_packets() > 0) {
            kprintf("mouse: pointer active (%llu packets)\n",
                    mouse_packets());
            mouse_reported = true;
        }

        while ((key = keyboard_pop()) >= 0) {
            console_input(key);
        }
        key = serial_poll_char();
        if (key >= 0) {
            console_input(key);
        }

        fb_present();
    }
}
