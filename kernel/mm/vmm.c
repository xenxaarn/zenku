#include <kio.h>
#include <kpanic.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <types.h>

#define PHYS_MAP_BASE 0xFFFF800000000000ull

#define PML4_INDEX(v) (((v) >> 39) & 0x1FF)
#define PDPT_INDEX(v) (((v) >> 30) & 0x1FF)
#define PD_INDEX(v)   (((v) >> 21) & 0x1FF)
#define PT_INDEX(v)   (((v) >> 12) & 0x1FF)

static uint64_t *g_pml4;

static inline uint64_t *phys_to_ptr(uint64_t phys)
{
    return (uint64_t *)(uintptr_t)(PHYS_MAP_BASE + phys);
}

static inline uint64_t entry_addr(uint64_t entry)
{
    return entry & PTE_ADDR_MASK;
}

static uint64_t *vmm_walk(uint64_t virt, bool create)
{
    uint64_t *table = g_pml4;
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
            *entry = frame | VMM_KERNEL_FLAGS;
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

int vmm_init(void)
{
    g_pml4 = phys_to_ptr(VMM_PML4_PHYS);
    if (!(g_pml4[PML4_INDEX(0xFFFFFFFF80000000ull)] & PTE_PRESENT)) {
        kprintf("vmm: kernel PML4 entry is missing\n");
        return -1;
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

    pte = vmm_walk(virt, true);
    if (!pte) {
        return -1;
    }
    *pte = (phys & PTE_ADDR_MASK) | (flags & ~PTE_ADDR_MASK);
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
    uint64_t *pte = vmm_walk(virt, false);

    if (!pte || !(*pte & PTE_PRESENT)) {
        return -1;
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
    uint64_t *pte = vmm_walk(virt, false);

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
