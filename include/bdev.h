#ifndef ZENKU_BDEV_H
#define ZENKU_BDEV_H

#include <types.h>

/*
 * Simple read-only block device. Block addresses are device blocks of
 * `block_size` bytes; filesystems translate their own block size on top.
 */
struct bdev {
    void *ctx;
    uint32_t block_size;
    uint64_t block_count;
    int (*read)(struct bdev *dev, uint64_t block, void *buf, uint32_t count);
};

static inline int bdev_read(struct bdev *dev, uint64_t block, void *buf,
                            uint32_t count)
{
    if (!dev || !dev->read) {
        return -1;
    }
    if (block + count > dev->block_count) {
        return -1;
    }
    return dev->read(dev, block, buf, count);
}

#endif /* ZENKU_BDEV_H */
