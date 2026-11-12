#ifndef ZENKU_PMM_H
#define ZENKU_PMM_H

#include <boot_info.h>
#include <types.h>

/* The direct map covers the first 4 GiB, so that is the PMM's horizon. */
#define PMM_MAX_PHYS   GB(4)
#define PMM_MAX_FRAMES (PMM_MAX_PHYS / PAGE_SIZE)

void pmm_init(const struct boot_info *bi);
void pmm_reserve_range(uint64_t base, uint64_t length);
void pmm_reserve_frame(uint64_t phys);

uint64_t pmm_total_frames(void);
uint64_t pmm_free_frames(void);
uint64_t pmm_used_frames(void);
uint64_t pmm_alloc_frame(void);
void pmm_free_frame(uint64_t phys);
bool pmm_frame_is_used(uint64_t phys);

void pmm_selftest(void);

#endif /* ZENKU_PMM_H */
