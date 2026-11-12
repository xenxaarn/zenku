#include <kio.h>
#include <kpanic.h>
#include <mm/pmm.h>
#include <string.h>
#include <types.h>

#define PHYS_MAP_BASE 0xFFFF800000000000ull

/* One bit per 4 KiB frame: 128 KiB covers the full 4 GiB direct map. */
static uint64_t g_bitmap[PMM_MAX_FRAMES / 64];
static uint64_t g_free_frames;
static uint64_t g_total_frames;
static uint64_t g_next_frame;

static inline bool frame_used(uint64_t frame)
{
    return (g_bitmap[frame / 64] & (1ull << (frame % 64))) != 0;
}

static inline void frame_set(uint64_t frame)
{
    g_bitmap[frame / 64] |= 1ull << (frame % 64);
}

static inline void frame_clear(uint64_t frame)
{
    g_bitmap[frame / 64] &= ~(1ull << (frame % 64));
}

void pmm_reserve_range(uint64_t base, uint64_t length)
{
    uint64_t first;
    uint64_t last;

    if (length == 0) {
        return;
    }

    first = base / PAGE_SIZE;
    last = ALIGN_UP(base + length, PAGE_SIZE) / PAGE_SIZE;
    if (last > PMM_MAX_FRAMES) {
        last = PMM_MAX_FRAMES;
    }

    for (uint64_t frame = first; frame < last; frame++) {
        if (!frame_used(frame)) {
            frame_set(frame);
            g_free_frames--;
        }
    }
}

void pmm_reserve_frame(uint64_t phys)
{
    pmm_reserve_range(phys, PAGE_SIZE);
}

void pmm_init(const struct boot_info *bi)
{
    const struct e820_entry *entries;
    uint64_t usable = 0;

    memset(g_bitmap, 0xFF, sizeof(g_bitmap));
    g_free_frames = 0;
    g_total_frames = 0;
    g_next_frame = 0;

    if (!bi->e820_addr || !bi->e820_count) {
        kpanic("pmm: no E820 map");
    }

    entries = (const struct e820_entry *)(uintptr_t)
              (PHYS_MAP_BASE + bi->e820_addr);

    for (uint32_t i = 0; i < bi->e820_count; i++) {
        uint64_t base;
        uint64_t end;

        if (entries[i].type != E820_TYPE_USABLE) {
            continue;
        }

        base = ALIGN_UP(entries[i].base, PAGE_SIZE);
        end = ALIGN_DOWN(entries[i].base + entries[i].length, PAGE_SIZE);
        if (base >= PMM_MAX_PHYS) {
            continue;
        }
        if (end > PMM_MAX_PHYS) {
            end = PMM_MAX_PHYS;
        }

        for (uint64_t frame = base / PAGE_SIZE;
             frame < end / PAGE_SIZE; frame++) {
            if (frame_used(frame)) {
                frame_clear(frame);
                g_free_frames++;
            }
        }
        usable += entries[i].length;
    }

    if (g_free_frames == 0) {
        kpanic("pmm: no usable frames");
    }

    /*
     * Low memory holds the IVT/BDA, stage1/stage2/stage3, page tables,
     * boot_params, boot_info, the normalized E820 map, and the font.
     * Reserve all of it in one go.
     */
    pmm_reserve_range(0, MB(1));

    /* The kernel image, including BSS and the boot stack. */
    pmm_reserve_range(bi->kernel_phys_base, bi->kernel_size);

    /* The embedded ext2 image is owned by the boot chain until Phase 4. */
    if (bi->initrd_addr && bi->initrd_size) {
        pmm_reserve_range(bi->initrd_addr, bi->initrd_size);
    }

    /* Never hand out MMIO. */
    if ((bi->flags & BOOT_FLAG_FB) && bi->fb_addr && bi->fb_pitch &&
        bi->fb_height) {
        pmm_reserve_range(bi->fb_addr, (uint64_t)bi->fb_pitch * bi->fb_height);
    }

    g_total_frames = g_free_frames;

    kprintf("pmm: %llu usable MiB, %llu frames tracked, %llu free (%llu MiB)\n",
            usable / MB(1), PMM_MAX_FRAMES, g_free_frames,
            g_free_frames * PAGE_SIZE / MB(1));
}

uint64_t pmm_total_frames(void)
{
    return g_total_frames;
}

uint64_t pmm_free_frames(void)
{
    return g_free_frames;
}

uint64_t pmm_used_frames(void)
{
    return g_total_frames - g_free_frames;
}

bool pmm_frame_is_used(uint64_t phys)
{
    uint64_t frame = phys / PAGE_SIZE;

    if (frame >= PMM_MAX_FRAMES) {
        return true;
    }
    return frame_used(frame);
}

uint64_t pmm_alloc_frame(void)
{
    uint64_t start = g_next_frame;

    for (uint64_t i = 0; i < PMM_MAX_FRAMES; i++) {
        uint64_t frame = (start + i) % PMM_MAX_FRAMES;

        if (!frame_used(frame)) {
            frame_set(frame);
            g_free_frames--;
            g_next_frame = frame + 1;
            if (g_next_frame >= PMM_MAX_FRAMES) {
                g_next_frame = 0;
            }

            memset((void *)(uintptr_t)(PHYS_MAP_BASE + frame * PAGE_SIZE),
                   0, PAGE_SIZE);
            return frame * PAGE_SIZE;
        }
    }

    return 0;
}

void pmm_free_frame(uint64_t phys)
{
    uint64_t frame;

    if (phys & (PAGE_SIZE - 1)) {
        kprintf("pmm: free of unaligned address 0x%llx\n", phys);
        return;
    }

    frame = phys / PAGE_SIZE;
    if (frame >= PMM_MAX_FRAMES) {
        kprintf("pmm: free beyond the direct map: 0x%llx\n", phys);
        return;
    }
    if (!frame_used(frame)) {
        kprintf("pmm: double free of frame 0x%llx\n", phys);
        return;
    }

    frame_clear(frame);
    g_free_frames++;
}

void pmm_selftest(void)
{
    enum { COUNT = 10000 };
    static uint64_t frames[COUNT];
    uint64_t before = g_free_frames;
    uint64_t allocated = 0;

    for (uint64_t i = 0; i < COUNT; i++) {
        frames[i] = pmm_alloc_frame();
        if (frames[i] == 0) {
            break;
        }
        allocated++;

        volatile uint64_t *cell =
            (volatile uint64_t *)(uintptr_t)(PHYS_MAP_BASE + frames[i]);
        cell[0] = 0x5A5A00000000ull | i;
        if (cell[0] != (0x5A5A00000000ull | i)) {
            kpanic("pmm selftest: frame 0x%llx is not writable", frames[i]);
        }
    }

    for (uint64_t i = 0; i < allocated; i++) {
        pmm_free_frame(frames[i]);
    }

    if (allocated != COUNT) {
        kpanic("pmm selftest: allocated only %llu/%u frames",
               allocated, COUNT);
    }
    if (g_free_frames != before) {
        kpanic("pmm selftest: leak, free %llu -> %llu",
               before, g_free_frames);
    }

    kprintf("pmm: selftest %u alloc/free frames, no leaks\n", COUNT);
}
