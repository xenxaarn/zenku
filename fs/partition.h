#ifndef ZENKU_PARTITION_H
#define ZENKU_PARTITION_H

#include <bdev.h>
#include <types.h>

struct part_entry {
    struct bdev bdev;
    struct bdev *parent;
    uint64_t start;
};

/* Parse MBR or GPT and fill `out`. A disk without a partition table is
 * exposed as a single whole-disk partition. Returns the entry count. */
int partition_scan(struct bdev *disk, struct part_entry *out, int max);

#endif /* ZENKU_PARTITION_H */
