#include <kio.h>
#include <kpanic.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <string.h>
#include <types.h>

#define HEAP_ALIGN 16
#define HEAP_MAGIC 0x48454150424C4B31ull /* "HEAPBLK1" */

/* A 16-byte trailer keeps the following block 16-byte aligned and detects
 * overruns past the requested payload. */
#define HEAP_TRAILER_SIZE 16
#define HEAP_TRAILER_MAGIC 0x5452414C5A4E4B31ull /* "TRALZNK1" */

#define HEAP_POISON_ALLOC 0xCC
#define HEAP_POISON_FREE  0xDD

struct heap_block {
    uint64_t magic;
    uint64_t size;              /* payload bytes */
    struct heap_block *next;
    uint64_t free;
} ALIGNED(HEAP_ALIGN);

static struct heap_block *g_first;
static size_t g_initial_free;

static inline size_t align_up(size_t value)
{
    return (value + (HEAP_ALIGN - 1)) & ~(size_t)(HEAP_ALIGN - 1);
}

static inline uint64_t *block_trailer(const struct heap_block *block)
{
    return (uint64_t *)((uint8_t *)(block + 1) + block->size);
}

static inline void trailer_set(struct heap_block *block)
{
    uint64_t *trailer = block_trailer(block);

    trailer[0] = HEAP_TRAILER_MAGIC;
    trailer[1] = ~block->size;
}

static inline bool trailer_ok(const struct heap_block *block)
{
    const uint64_t *trailer = block_trailer(block);

    return trailer[0] == HEAP_TRAILER_MAGIC && trailer[1] == ~block->size;
}

static uint64_t irq_save(void)
{
    uint64_t flags;

    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void irq_restore(uint64_t flags)
{
    if (flags & (1ull << 9)) {
        __asm__ volatile("sti" : : : "memory");
    }
}

static bool block_valid(const struct heap_block *block)
{
    return block && block->magic == HEAP_MAGIC;
}

static void heap_verify(void)
{
    for (struct heap_block *block = g_first; block; block = block->next) {
        if (!block_valid(block)) {
            kpanic("heap: corrupt block header at %p", block);
        }
        if (!trailer_ok(block)) {
            kpanic("heap: redzone overwritten for block at %p", block);
        }
        if (block->next && block->next <= block) {
            kpanic("heap: non-monotonic block list at %p", block);
        }
    }
}

void heap_init(void)
{
    uint64_t mapped = 0;

    for (uint64_t offset = 0; offset < KERNEL_HEAP_SIZE;
         offset += PAGE_SIZE) {
        uint64_t frame = pmm_alloc_frame();

        if (frame == 0) {
            kpanic("heap: out of frames after %llu bytes", mapped);
        }
        if (vmm_map(KERNEL_HEAP_BASE + offset, frame,
                    VMM_KERNEL_FLAGS) != 0) {
            kpanic("heap: cannot map 0x%llx", KERNEL_HEAP_BASE + offset);
        }
        mapped += PAGE_SIZE;
    }

    g_first = (struct heap_block *)(uintptr_t)KERNEL_HEAP_BASE;
    g_first->magic = HEAP_MAGIC;
    g_first->size = KERNEL_HEAP_SIZE - sizeof(struct heap_block) -
                    HEAP_TRAILER_SIZE;
    g_first->next = NULL;
    g_first->free = 1;
    trailer_set(g_first);
    g_initial_free = g_first->size;

    kprintf("heap: %llu MiB mapped at 0x%llx (%llu KiB usable, redzones on)\n",
            mapped / MB(1), KERNEL_HEAP_BASE, g_first->size / KB(1));
}

void *kmalloc(size_t size)
{
    uint64_t flags;
    struct heap_block *block;
    size_t needed;

    if (size == 0) {
        return NULL;
    }

    needed = align_up(size);
    flags = irq_save();

    for (block = g_first; block; block = block->next) {
        if (!block_valid(block)) {
            irq_restore(flags);
            kpanic("heap: corrupt block header at %p", block);
        }
        if (block->free && !trailer_ok(block)) {
            irq_restore(flags);
            kpanic("heap: redzone overwritten for free block at %p", block);
        }
        if (!block->free || block->size < needed) {
            continue;
        }

        /* Split if the remainder can hold a header, trailer, and a minimal
         * aligned payload. */
        if (block->size >= needed + HEAP_TRAILER_SIZE +
                               sizeof(struct heap_block) + HEAP_ALIGN) {
            struct heap_block *rest = (struct heap_block *)
                ((uint8_t *)(block + 1) + needed + HEAP_TRAILER_SIZE);

            rest->magic = HEAP_MAGIC;
            rest->size = block->size - needed - HEAP_TRAILER_SIZE -
                         sizeof(struct heap_block);
            rest->next = block->next;
            rest->free = 1;
            trailer_set(rest);

            block->next = rest;
            block->size = needed;
            trailer_set(block);
        }

        block->free = 0;
        memset(block + 1, HEAP_POISON_ALLOC, block->size);
        trailer_set(block);
        irq_restore(flags);
        return (void *)(block + 1);
    }

    irq_restore(flags);
    return NULL;
}

void *kzalloc(size_t size)
{
    void *ptr = kmalloc(size);

    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void *kcalloc(size_t count, size_t size)
{
    if (count && size > SIZE_MAX / count) {
        return NULL;
    }
    return kzalloc(count * size);
}

void kfree(void *ptr)
{
    uint64_t flags;
    struct heap_block *block;

    if (!ptr) {
        return;
    }

    flags = irq_save();
    block = (struct heap_block *)ptr - 1;
    if (!block_valid(block)) {
        irq_restore(flags);
        kpanic("heap: kfree of invalid pointer %p", ptr);
    }
    if (block->free) {
        irq_restore(flags);
        kpanic("heap: double free of %p", ptr);
    }
    if (!trailer_ok(block)) {
        irq_restore(flags);
        kpanic("heap: redzone overwritten before free of %p", ptr);
    }

    memset(block + 1, HEAP_POISON_FREE, block->size);
    block->free = 1;

    /* Coalesce every adjacent free pair in one pass. */
    for (block = g_first; block; block = block->next) {
        while (block->free && block->next && block->next->free) {
            struct heap_block *next = block->next;

            if (!block_valid(next) || !trailer_ok(next)) {
                irq_restore(flags);
                kpanic("heap: corrupt neighbour after %p", block);
            }
            block->size += sizeof(struct heap_block) + next->size +
                           HEAP_TRAILER_SIZE;
            block->next = next->next;
            trailer_set(block);
        }
    }

    irq_restore(flags);
}

void *krealloc(void *ptr, size_t size)
{
    struct heap_block *block;
    void *fresh;
    size_t copy;

    if (!ptr) {
        return kmalloc(size);
    }
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    block = (struct heap_block *)ptr - 1;
    if (!block_valid(block) || !trailer_ok(block)) {
        kpanic("heap: krealloc of invalid pointer %p", ptr);
    }
    if (block->size >= size) {
        return ptr;
    }

    fresh = kmalloc(size);
    if (!fresh) {
        return NULL;
    }

    copy = block->size < size ? block->size : size;
    memcpy(fresh, ptr, copy);
    kfree(ptr);
    return fresh;
}

size_t heap_free_bytes(void)
{
    struct heap_block *block;
    size_t total = 0;

    for (block = g_first; block; block = block->next) {
        if (block->free) {
            total += block->size;
        }
    }
    return total;
}

size_t heap_used_bytes(void)
{
    return g_initial_free - heap_free_bytes();
}

void heap_selftest(void)
{
    enum { BLOCKS = 512 };
    static void *ptrs[BLOCKS];
    size_t before = heap_free_bytes();
    unsigned allocated = 0;

    for (unsigned i = 0; i < BLOCKS; i++) {
        size_t size = 16 + (i * 29) % 512;

        ptrs[i] = kmalloc(size);
        if (!ptrs[i]) {
            break;
        }
        memset(ptrs[i], 0xA5, size);
        allocated++;
    }
    heap_verify();

    for (unsigned i = 0; i < allocated; i++) {
        kfree(ptrs[i]);
    }
    heap_verify();

    if (allocated != BLOCKS) {
        kpanic("heap selftest: allocated only %u/%u blocks",
               allocated, BLOCKS);
    }
    if (heap_free_bytes() != before) {
        kpanic("heap selftest: leak, free %llu -> %llu",
               (uint64_t)before, (uint64_t)heap_free_bytes());
    }

    void *big = kzalloc(64 * 1024);
    if (!big) {
        kpanic("heap selftest: 64 KiB allocation failed");
    }
    memset(big, 0x5A, 64 * 1024);
    kfree(big);
    heap_verify();

    kprintf("heap: selftest %u alloc/free blocks, redzones intact, no leaks\n",
            BLOCKS);
}
