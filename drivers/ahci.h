#ifndef ZENKU_AHCI_H
#define ZENKU_AHCI_H

#include <bdev.h>
#include <types.h>

/* Probe all AHCI controllers and fill up to `max` writable block devices.
 * Returns the number found, or a negative errno. */
int ahci_init(struct bdev *out, int max);

#endif /* ZENKU_AHCI_H */
