#ifndef ZENKU_PATA_H
#define ZENKU_PATA_H

#include <bdev.h>
#include <types.h>

/* Probe the legacy ATA channels and fill up to `max` block devices. */
int pata_init(struct bdev *out, int max);

#endif /* ZENKU_PATA_H */
