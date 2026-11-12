#ifndef ZENKU_VMM_H
#define ZENKU_VMM_H

#include <types.h>

#define PTE_PRESENT  (1ull << 0)
#define PTE_WRITE    (1ull << 1)
#define PTE_USER     (1ull << 2)
#define PTE_PWT      (1ull << 3)
#define PTE_PCD      (1ull << 4)
#define PTE_ACCESSED (1ull << 5)
#define PTE_DIRTY    (1ull << 6)
#define PTE_HUGE     (1ull << 7)
#define PTE_GLOBAL   (1ull << 8)
#define PTE_NX       (1ull << 63)

#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ull

#define VMM_KERNEL_FLAGS (PTE_PRESENT | PTE_WRITE)
#define VMM_USER_FLAGS   (PTE_PRESENT | PTE_WRITE | PTE_USER)

/* Physical address of the PML4 installed by stage3. */
#define VMM_PML4_PHYS 0x1000ull

int vmm_init(void);
int vmm_map(uint64_t virt, uint64_t phys, uint64_t flags);
int vmm_map_range(uint64_t virt, uint64_t phys, uint64_t length,
                  uint64_t flags);
int vmm_unmap(uint64_t virt);
int vmm_unmap_range(uint64_t virt, uint64_t length);
int vmm_protect(uint64_t virt, uint64_t flags);
uint64_t vmm_translate(uint64_t virt);
bool vmm_is_mapped(uint64_t virt);
void vmm_flush_tlb(uint64_t virt);

void vmm_selftest(void);

#endif /* ZENKU_VMM_H */
