#include <kio.h>
#include <kpanic.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <string.h>
#include <types.h>

#define PHYS_MAP_BASE 0xFFFF800000000000ull

#define PML4_INDEX(v) (((v) >> 39) & 0x1FF)
#define PDPT_INDEX(v) (((v) >> 30) & 0x1FF)
#define PD_INDEX(v)   (((v) >> 21) & 0x1FF)
#define PT_INDEX(v)   (((v) >> 12) & 0x1FF)

static uint64_t *g_pml4;

/* One reference count per physical frame, used only for user pages that can
 * be shared by fork. Kernel frames and page tables are not tracked here. */
static uint8_t g_page_refs[PMM_MAX_FRAMES];

static void page_ref_set(uint64_t phys, uint8_t value)
{
    uint64_t frame = phys / PAGE_SIZE;

    if (frame < PMM_MAX_FRAMES) {
        g_page_refs[frame] = value;
    }
}

static void page_ref_get(uint64_t phys)
{
    uint64_t frame = phys / PAGE_SIZE;

    if (frame < PMM_MAX_FRAMES && g_page_refs[frame] != UINT8_MAX) {
        g_page_refs[frame]++;
    }
}

static void page_ref_put(uint64_t phys)
{
    uint64_t frame = phys / PAGE_SIZE;

    if (frame >= PMM_MAX_FRAMES) {
        return;
    }
    if (g_page_refs[frame] > 1) {
        g_page_refs[frame]--;
        return;
    }
    g_page_refs[frame] = 0;
    pmm_free_frame(phys);
}

static inline uint64_t *phys_to_ptr(uint64_t phys)
{
    return (uint64_t *)(uintptr_t)(PHYS_MAP_BASE + phys);
}

static inline uint64_t entry_addr(uint64_t entry)
{
    return entry & PTE_ADDR_MASK;
}

static uint64_t *space_walk(uint64_t *pml4, uint64_t virt, bool create,
                            uint64_t flags)
{
    uint64_t *table = pml4;
    unsigned indexes[3];

    indexes[0] = PML4_INDEX(virt);
    indexes[1] = PDPT_INDEX(virt);
    indexes[2] = PD_INDEX(virt);

    for (unsigned level = 0; level < 3; level++) {
        uint64_t *entry = &table[indexes[level]];

        if (!(*entry & PTE_PRESENT)) {
            uint64_t frame;

            if (!create) {
                return NULL;
            }

            frame = pmm_alloc_frame();
            if (frame == 0) {
                return NULL;
            }
            memset(phys_to_ptr(frame), 0, PAGE_SIZE);
            *entry = frame | VMM_KERNEL_FLAGS;
        }

        /* Every level must allow user access for a user page to be usable. */
        if ((flags & PTE_USER) && !(*entry & PTE_USER)) {
            *entry |= PTE_USER;
        }

        if (*entry & PTE_HUGE) {
            /* A huge mapping cannot be split with the current design. */
            return NULL;
        }

        table = phys_to_ptr(entry_addr(*entry));
        if (level == 2) {
            break;
        }
    }

    return &table[PT_INDEX(virt)];
}

static uint64_t *vmm_walk(uint64_t virt, bool create, uint64_t flags)
{
    return space_walk(g_pml4, virt, create, flags);
}

static int clone_level(uint64_t src_phys, uint64_t dst_phys, unsigned level)
{
    uint64_t *src = phys_to_ptr(src_phys);
    uint64_t *dst = phys_to_ptr(dst_phys);

    for (unsigned i = 0; i < 512; i++) {
        uint64_t se = src[i];
        uint64_t frame;

        if (!(se & PTE_PRESENT)) {
            continue;
        }
        if (se & PTE_HUGE) {
            return -1;
        }

        if (level == 1) {
            /* Share the leaf page. Both parent and child lose write access;
             * the first write allocates a private copy. */
            uint64_t page = entry_addr(se);

            page_ref_get(page);
            src[i] = (se & ~PTE_WRITE) | PTE_COW;
            dst[i] = (se & ~PTE_WRITE) | PTE_COW;
            continue;
        }

        frame = pmm_alloc_frame();
        if (frame == 0) {
            return -1;
        }
        memset(phys_to_ptr(frame), 0, PAGE_SIZE);
        dst[i] = frame | (se & ~PTE_ADDR_MASK);
        if (clone_level(entry_addr(se), frame, level - 1) != 0) {
            return -1;
        }
    }
    return 0;
}

static void free_level(uint64_t table_phys, unsigned level)
{
    uint64_t *table = phys_to_ptr(table_phys);

    for (unsigned i = 0; i < 512; i++) {
        uint64_t entry = table[i];

        if (!(entry & PTE_PRESENT)) {
            continue;
        }
        if (level == 1) {
            page_ref_put(entry_addr(entry));
            continue;
        }
        if (entry & PTE_HUGE) {
            continue;
        }
        free_level(entry_addr(entry), level - 1);
        pmm_free_frame(entry_addr(entry));
    }
}

uint64_t vmm_space_create(void)
{
    uint64_t phys = pmm_alloc_frame();
    uint64_t *pml4;

    if (phys == 0) {
        return 0;
    }
    pml4 = phys_to_ptr(phys);
    memset(pml4, 0, PAGE_SIZE);

    /* Kernel half is shared with every address space. */
    for (unsigned i = 256; i < 512; i++) {
        pml4[i] = g_pml4[i];
    }
    return phys;
}

uint64_t vmm_space_clone(uint64_t src_phys)
{
    uint64_t dst_phys = vmm_space_create();
    uint64_t *src;
    uint64_t *dst;

    if (dst_phys == 0) {
        return 0;
    }

    src = phys_to_ptr(src_phys);
    dst = phys_to_ptr(dst_phys);
    for (unsigned i = 0; i < 256; i++) {
        uint64_t frame;

        if (!(src[i] & PTE_PRESENT)) {
            continue;
        }
        frame = pmm_alloc_frame();
        if (frame == 0) {
            vmm_space_destroy(dst_phys);
            return 0;
        }
        memset(phys_to_ptr(frame), 0, PAGE_SIZE);
        dst[i] = frame | (src[i] & ~PTE_ADDR_MASK);
        if (clone_level(entry_addr(src[i]), frame, 3) != 0) {
            vmm_space_destroy(dst_phys);
            return 0;
        }
    }

    /* The parent's writable mappings were downgraded to COW above. Flush the
     * stale writable TLB entries before it resumes. */
    __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3"
                     : : : "rax", "memory");
    return dst_phys;
}

void vmm_space_destroy(uint64_t pml4_phys)
{
    uint64_t *pml4;

    if (!pml4_phys) {
        return;
    }

    pml4 = phys_to_ptr(pml4_phys);
    for (unsigned i = 0; i < 256; i++) {
        if (!(pml4[i] & PTE_PRESENT)) {
            continue;
        }
        free_level(entry_addr(pml4[i]), 3);
        pmm_free_frame(entry_addr(pml4[i]));
        pml4[i] = 0;
    }
    pmm_free_frame(pml4_phys);
}

void vmm_space_switch(uint64_t pml4_phys)
{
    if (pml4_phys == 0) {
        return;
    }
    g_pml4 = phys_to_ptr(pml4_phys);
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

uint64_t vmm_kernel_space(void)
{
    return VMM_PML4_PHYS;
}

bool vmm_cow_fault(uint64_t virt, uint64_t error)
{
    uint64_t *pte;
    uint64_t old;
    uint64_t frame;
    uint64_t flags;

    /* A COW fault is a write fault. It may come from user mode or from a
     * kernel syscall writing into a user buffer; kernel addresses are never
     * resolved here. */
    if (!(error & 0x2) || (virt >= 0x0000800000000000ull && !(error & 0x4))) {
        return false;
    }

    pte = vmm_walk(virt, false, 0);
    if (!pte || !(*pte & PTE_PRESENT) || !(*pte & PTE_COW)) {
        return false;
    }

    old = entry_addr(*pte);
    frame = pmm_alloc_frame();
    if (frame == 0) {
        return false;
    }

    memcpy(phys_to_ptr(frame), phys_to_ptr(old), PAGE_SIZE);
    flags = (*pte & ~PTE_ADDR_MASK) & ~PTE_COW;
    flags |= PTE_PRESENT | PTE_WRITE | PTE_USER;
    *pte = (frame & PTE_ADDR_MASK) | flags;
    page_ref_set(frame, 1);
    page_ref_put(old);
    vmm_flush_tlb(virt & ~(PAGE_SIZE - 1));
    return true;
}

int vmm_init(void)
{
    uint64_t cr0;

    g_pml4 = phys_to_ptr(VMM_PML4_PHYS);
    if (!(g_pml4[PML4_INDEX(0xFFFFFFFF80000000ull)] & PTE_PRESENT)) {
        kprintf("vmm: kernel PML4 entry is missing\n");
        return -1;
    }

    /* Supervisor writes must honor read-only PTEs, otherwise a syscall
     * writing into a COW user page would modify the shared frame instead of
     * taking a private copy. */
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1ull << 16);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");

    /* Drop the bootloader's low identity map; the kernel only needs the
     * higher-half window and the direct map from here on. */
    if (g_pml4[0] & PTE_PRESENT) {
        g_pml4[0] = 0;
        __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3"
                         : : : "rax", "memory");
    }

    kprintf("vmm: page tables at 0x%llx, 4 KiB mappings ready\n",
            (uint64_t)VMM_PML4_PHYS);
    return 0;
}

int vmm_map(uint64_t virt, uint64_t phys, uint64_t flags)
{
    uint64_t *pte;

    if ((virt & (PAGE_SIZE - 1)) || (phys & (PAGE_SIZE - 1))) {
        return -1;
    }

    pte = vmm_walk(virt, true, flags);
    if (!pte) {
        return -1;
    }
    *pte = (phys & PTE_ADDR_MASK) | (flags & ~PTE_ADDR_MASK);
    if (flags & PTE_USER) {
        page_ref_set(phys, 1);
    }
    vmm_flush_tlb(virt);
    return 0;
}

int vmm_map_range(uint64_t virt, uint64_t phys, uint64_t length,
                  uint64_t flags)
{
    if (length == 0) {
        return 0;
    }
    if ((virt & (PAGE_SIZE - 1)) || (phys & (PAGE_SIZE - 1))) {
        return -1;
    }

    for (uint64_t offset = 0; offset < length; offset += PAGE_SIZE) {
        if (vmm_map(virt + offset, phys + offset, flags) != 0) {
            for (uint64_t undo = 0; undo < offset; undo += PAGE_SIZE) {
                vmm_unmap(virt + undo);
            }
            return -1;
        }
    }
    return 0;
}

int vmm_unmap(uint64_t virt)
{
    uint64_t *pte = vmm_walk(virt, false, 0);

    if (!pte || !(*pte & PTE_PRESENT)) {
        return -1;
    }
    if (*pte & PTE_USER) {
        page_ref_put(entry_addr(*pte));
    }
    *pte = 0;
    vmm_flush_tlb(virt);
    return 0;
}

int vmm_unmap_range(uint64_t virt, uint64_t length)
{
    for (uint64_t offset = 0; offset < length; offset += PAGE_SIZE) {
        vmm_unmap(virt + offset);
    }
    return 0;
}

int vmm_protect(uint64_t virt, uint64_t flags)
{
    uint64_t *pte = vmm_walk(virt, false, 0);

    if (!pte || !(*pte & PTE_PRESENT)) {
        return -1;
    }
    *pte = (*pte & PTE_ADDR_MASK) | (flags & ~PTE_ADDR_MASK);
    vmm_flush_tlb(virt);
    return 0;
}

uint64_t vmm_translate(uint64_t virt)
{
    uint64_t *table = g_pml4;
    uint64_t entry;
    unsigned indexes[4];

    indexes[0] = PML4_INDEX(virt);
    indexes[1] = PDPT_INDEX(virt);
    indexes[2] = PD_INDEX(virt);
    indexes[3] = PT_INDEX(virt);

    for (unsigned level = 0; level < 3; level++) {
        entry = table[indexes[level]];
        if (!(entry & PTE_PRESENT)) {
            return 0;
        }
        if (entry & PTE_HUGE) {
            uint64_t size = level == 2 ? MB(2) : GB(1);
            uint64_t mask = size - 1;

            return entry_addr(entry) + (virt & mask);
        }
        table = phys_to_ptr(entry_addr(entry));
    }

    entry = table[indexes[3]];
    if (!(entry & PTE_PRESENT)) {
        return 0;
    }
    return entry_addr(entry) + (virt & (PAGE_SIZE - 1));
}

bool vmm_is_mapped(uint64_t virt)
{
    return vmm_translate(virt) != 0;
}

void vmm_flush_tlb(uint64_t virt)
{
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void vmm_selftest(void)
{
    const uint64_t va = 0xFFFFFFFFBFF00000ull;
    uint64_t frame = pmm_alloc_frame();
    volatile uint64_t *cell;

    if (frame == 0) {
        kpanic("vmm selftest: out of frames");
    }

    if (vmm_map(va, frame, VMM_KERNEL_FLAGS) != 0) {
        kpanic("vmm selftest: map failed");
    }
    if (vmm_translate(va) != frame) {
        kpanic("vmm selftest: translate mismatch");
    }

    cell = (volatile uint64_t *)(uintptr_t)va;
    cell[0] = 0x1122334455667788ull;
    if (cell[0] != 0x1122334455667788ull) {
        kpanic("vmm selftest: mapping is not writable");
    }

    if (vmm_unmap(va) != 0) {
        kpanic("vmm selftest: unmap failed");
    }
    if (vmm_is_mapped(va)) {
        kpanic("vmm selftest: mapping survived unmap");
    }

    pmm_free_frame(frame);
    kprintf("vmm: selftest map/translate/unmap ok\n");
}
