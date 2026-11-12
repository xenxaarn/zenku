#ifndef ZENKU_RAMDISK_H
#define ZENKU_RAMDISK_H

#include <bdev.h>
#include <types.h>

/* The ext2 image is loaded by the boot chain and lives at a physical address
 * inside the direct map; the kernel treats it as a read-only 512-byte-block
 * device. */
struct ramdisk {
    struct bdev bdev;
    const uint8_t *base;
    uint64_t size;
};

int ramdisk_init(struct ramdisk *rd, uint64_t phys, uint64_t size);

#endif /* ZENKU_RAMDISK_H */
