#include <console.h>
#include <devfs.h>
#include <errno.h>
#include <ext2.h>
#include <kio.h>
#include <process.h>
#include <string.h>
#include <types.h>

#define CONSOLE_INPUT_MAX 512

static char g_console_input[CONSOLE_INPUT_MAX];
static unsigned g_console_head;
static unsigned g_console_tail;
static bool g_console_eof;

void devfs_console_push(const char *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        unsigned next = (g_console_head + 1) % CONSOLE_INPUT_MAX;

        if (next == g_console_tail) {
            break; /* drop on overflow */
        }
        g_console_input[g_console_head] = data[i];
        g_console_head = next;
    }
    process_wake_io();
}

void devfs_console_push_eof(void)
{
    g_console_eof = true;
    process_wake_io();
}

bool devfs_console_pending(void)
{
    return g_console_head != g_console_tail || g_console_eof;
}

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
    size_t count = 0;

    (void)ctx;
    if (len == 0) {
        return 0;
    }

    while (count < len && g_console_tail != g_console_head) {
        ((char *)buf)[count++] = g_console_input[g_console_tail];
        g_console_tail = (g_console_tail + 1) % CONSOLE_INPUT_MAX;
    }
    if (count > 0) {
        return (int64_t)count;
    }
    if (g_console_eof) {
        g_console_eof = false;
        return 0;
    }
    return -EAGAIN;
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
