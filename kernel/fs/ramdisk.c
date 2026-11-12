#include "ramdisk.h"
#include <string.h>
#include <types.h>

#define PHYS_MAP_BASE 0xFFFF800000000000ull

static int ramdisk_read(struct bdev *dev, uint64_t block, void *buf,
                        uint32_t count)
{
    struct ramdisk *rd = (struct ramdisk *)dev->ctx;
    uint64_t offset = block * dev->block_size;
    uint64_t length = (uint64_t)count * dev->block_size;

    if (offset + length > rd->size || offset + length < offset) {
        return -1;
    }

    memcpy(buf, rd->base + offset, (size_t)length);
    return 0;
}

int ramdisk_init(struct ramdisk *rd, uint64_t phys, uint64_t size)
{
    if (!rd || !phys || size < 1024) {
        return -1;
    }

    rd->base = (const uint8_t *)(uintptr_t)(PHYS_MAP_BASE + phys);
    rd->size = size;
    rd->bdev.ctx = rd;
    rd->bdev.block_size = 512;
    rd->bdev.block_count = size / 512;
    rd->bdev.read = ramdisk_read;
    return 0;
}
