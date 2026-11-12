#include <console.h>
#include <devfs.h>
#include <ext2.h>
#include <kio.h>
#include <string.h>
#include <types.h>

static int64_t null_read(void *ctx, void *buf, size_t len)
{
    (void)ctx;
    (void)buf;
    (void)len;
    return 0;
}

static int64_t null_write(void *ctx, const void *buf, size_t len)
{
    (void)ctx;
    (void)buf;
    return (int64_t)len;
}

static int64_t zero_read(void *ctx, void *buf, size_t len)
{
    (void)ctx;
    memset(buf, 0, len);
    return (int64_t)len;
}

static int64_t discard_write(void *ctx, const void *buf, size_t len)
{
    (void)ctx;
    (void)buf;
    return (int64_t)len;
}

static int64_t console_read(void *ctx, void *buf, size_t len)
{
    (void)ctx;
    (void)buf;
    (void)len;
    return 0;
}

static int64_t console_write(void *ctx, const void *buf, size_t len)
{
    const char *text = buf;

    (void)ctx;
    for (size_t i = 0; i < len; i++) {
        console_putc(text[i]);
    }
    return (int64_t)len;
}

static int64_t serial_read(void *ctx, void *buf, size_t len)
{
    (void)ctx;
    (void)buf;
    (void)len;
    return 0;
}

static int64_t serial_dev_write(void *ctx, const void *buf, size_t len)
{
    (void)ctx;
    serial_write((const char *)buf, len);
    return (int64_t)len;
}

static const struct devfs_ops g_devices[] = {
    { "console", 0x1000, console_read, console_write },
    { "tty0",    0x1001, console_read, console_write },
    { "ttyS0",   0x1002, serial_read,  serial_dev_write },
    { "null",    0x1003, null_read,    null_write    },
    { "zero",    0x1004, zero_read,    discard_write },
};

#define DEVICE_COUNT (sizeof(g_devices) / sizeof(g_devices[0]))

int devfs_lookup(const char *name, const struct devfs_ops **ops)
{
    if (!name || !ops) {
        return -1;
    }

    for (unsigned i = 0; i < DEVICE_COUNT; i++) {
        if (strcmp(name, g_devices[i].name) == 0) {
            *ops = &g_devices[i];
            return 0;
        }
    }
    return -1;
}

int devfs_list(devfs_dir_cb cb, void *ctx)
{
    if (!cb) {
        return -1;
    }

    for (unsigned i = 0; i < DEVICE_COUNT; i++) {
        int rc = cb(g_devices[i].name, (uint32_t)strlen(g_devices[i].name),
                    g_devices[i].ino, EXT2_FT_CHRDEV, ctx);

        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}
