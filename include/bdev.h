#ifndef ZENKU_BDEV_H
#define ZENKU_BDEV_H

#include <errno.h>
#include <types.h>

/*
 * Block device interface. Block addresses are device blocks of
 * `block_size` bytes; filesystems translate their own block size on top.
 *
 * Read-only devices leave `write` and `flush` NULL. Callers must use the
 * bdev_* wrappers so bounds and capability checks stay in one place.
 */
struct bdev {
    void *ctx;
    uint32_t block_size;
    uint64_t block_count;
    const char *name;
    bool read_only;
    int (*read)(struct bdev *dev, uint64_t block, void *buf, uint32_t count);
    int (*write)(struct bdev *dev, uint64_t block, const void *buf,
                 uint32_t count);
    int (*flush)(struct bdev *dev);
};

static inline int bdev_read(struct bdev *dev, uint64_t block, void *buf,
                            uint32_t count)
{
    if (!dev || !dev->read) {
        return -ENODEV;
    }
    if (block + count < block || block + count > dev->block_count) {
        return -EIO;
    }
    return dev->read(dev, block, buf, count);
}

static inline int bdev_write(struct bdev *dev, uint64_t block,
                             const void *buf, uint32_t count)
{
    if (!dev || !dev->write || dev->read_only) {
        return -EROFS;
    }
    if (block + count < block || block + count > dev->block_count) {
        return -EIO;
    }
    return dev->write(dev, block, buf, count);
}

static inline int bdev_flush(struct bdev *dev)
{
    if (!dev) {
        return -ENODEV;
    }
    if (!dev->flush) {
        return 0;
    }
    return dev->flush(dev);
}

static inline bool bdev_writable(const struct bdev *dev)
{
    return dev && dev->write && !dev->read_only;
}

#endif /* ZENKU_BDEV_H */
