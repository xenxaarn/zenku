#ifndef ZENKU_HEAP_H
#define ZENKU_HEAP_H

#include <types.h>

/*
 * Kernel heap window: the upper half of the unused 1 GiB kernel PDPT
 * (0xFFFFFFFFA0000000..0xFFFFFFFFBFFFFFFF). Mapped with 4 KiB pages on
 * demand from the PMM.
 */
#define KERNEL_HEAP_BASE 0xFFFFFFFFA0000000ull
#define KERNEL_HEAP_SIZE MB(16)

void heap_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *kcalloc(size_t count, size_t size);
void *krealloc(void *ptr, size_t size);
void kfree(void *ptr);

size_t heap_free_bytes(void);
size_t heap_used_bytes(void);
void heap_selftest(void);

#endif /* ZENKU_HEAP_H */
