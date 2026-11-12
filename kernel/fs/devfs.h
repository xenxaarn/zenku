#ifndef ZENKU_DEVFS_H
#define ZENKU_DEVFS_H

#include <types.h>

struct devfs_ops {
    const char *name;
    uint32_t ino;
    int64_t (*read)(void *ctx, void *buf, size_t len);
    int64_t (*write)(void *ctx, const void *buf, size_t len);
};

typedef int (*devfs_dir_cb)(const char *name, uint32_t name_len, uint32_t ino,
                            uint8_t type, void *ctx);

/* Find a device by name ("null", "zero", ...). Returns 0 on success. */
int devfs_lookup(const char *name, const struct devfs_ops **ops);

/* Visit every node under /dev. */
int devfs_list(devfs_dir_cb cb, void *ctx);

#endif /* ZENKU_DEVFS_H */
