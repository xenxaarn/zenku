#include <errno.h>
#include <io.h>
#include <kio.h>
#include <pata.h>
#include <string.h>
#include <types.h>

#define PATA_MAX_DRIVES 4
#define PATA_SECTOR 512

struct pata_channel {
    uint16_t io;
    uint16_t ctrl;
};

struct pata_drive {
    struct pata_channel ch;
    bool slave;
    bool lba48;
    uint64_t sectors;
    struct bdev bdev;
};

static struct pata_drive g_drives[PATA_MAX_DRIVES];
static int g_drive_count;

static const struct pata_channel g_channels[2] = {
    { 0x1F0, 0x3F6 },
    { 0x170, 0x376 },
};

static uint8_t status(struct pata_drive *d)
{
    return inb((uint16_t)(d->ch.io + 7));
}

static void delay_400ns(struct pata_drive *d)
{
    for (int i = 0; i < 4; i++) {
        (void)status(d);
    }
}

static int wait_not_busy(struct pata_drive *d)
{
    for (int i = 0; i < 100000; i++) {
        uint8_t s = status(d);

        if (s == 0xFF) {
            /* Floating bus: no device is attached to this channel. */
            return -ENODEV;
        }
        if (!(s & 0x80)) {
            return 0;
        }
    }
    return -EIO;
}

static int wait_drq(struct pata_drive *d)
{
    for (int i = 0; i < 1000000; i++) {
        uint8_t s = status(d);

        if (s & 0x01) {
            return -EIO;
        }
        if (!(s & 0x80) && (s & 0x08)) {
            return 0;
        }
    }
    return -EIO;
}

static void select_drive(struct pata_drive *d, uint8_t extra)
{
    outb((uint16_t)(d->ch.io + 6),
         (uint8_t)(0xA0 | (d->slave ? 0x10 : 0x00) | extra));
    delay_400ns(d);
}

static int identify(struct pata_drive *d, uint16_t *words)
{
    select_drive(d, 0);
    outb((uint16_t)(d->ch.io + 2), 0);
    outb((uint16_t)(d->ch.io + 3), 0);
    outb((uint16_t)(d->ch.io + 4), 0);
    outb((uint16_t)(d->ch.io + 5), 0);
    outb((uint16_t)(d->ch.io + 7), 0xEC);

    delay_400ns(d);
    if (wait_not_busy(d) != 0) {
        return -ENODEV;
    }
    {
        uint8_t s = status(d);

        if (!s || (s & 0x01) || !(s & 0x08)) {
            return -ENODEV;
        }
    }
    for (int i = 0; i < 256; i++) {
        words[i] = inw(d->ch.io);
    }
    return 0;
}

static int command_read(struct pata_drive *d, uint64_t lba)
{
    if (d->lba48) {
        outb((uint16_t)(d->ch.io + 2), 0);
        outb((uint16_t)(d->ch.io + 3), (uint8_t)(lba >> 24));
        outb((uint16_t)(d->ch.io + 4), (uint8_t)(lba >> 32));
        outb((uint16_t)(d->ch.io + 5), (uint8_t)(lba >> 40));
        outb((uint16_t)(d->ch.io + 2), 1);
        outb((uint16_t)(d->ch.io + 3), (uint8_t)lba);
        outb((uint16_t)(d->ch.io + 4), (uint8_t)(lba >> 8));
        outb((uint16_t)(d->ch.io + 5), (uint8_t)(lba >> 16));
        select_drive(d, 0x40);
        outb((uint16_t)(d->ch.io + 7), 0x24);
    } else {
        select_drive(d, (uint8_t)((lba >> 24) & 0x0F));
        outb((uint16_t)(d->ch.io + 2), 1);
        outb((uint16_t)(d->ch.io + 3), (uint8_t)lba);
        outb((uint16_t)(d->ch.io + 4), (uint8_t)(lba >> 8));
        outb((uint16_t)(d->ch.io + 5), (uint8_t)(lba >> 16));
        outb((uint16_t)(d->ch.io + 7), 0x20);
    }
    return 0;
}

static int drive_read(struct bdev *dev, uint64_t block, void *buf,
                      uint32_t count)
{
    struct pata_drive *d = dev->ctx;
    uint16_t *dst = buf;

    for (uint32_t n = 0; n < count; n++) {
        command_read(d, block + n);
        if (wait_drq(d) != 0) {
            return -EIO;
        }
        for (int i = 0; i < 256; i++) {
            dst[n * 256 + i] = inw(d->ch.io);
        }
        if (wait_not_busy(d) != 0) {
            return -EIO;
        }
    }
    return 0;
}

static int drive_write(struct bdev *dev, uint64_t block, const void *buf,
                       uint32_t count)
{
    struct pata_drive *d = dev->ctx;
    const uint16_t *src = buf;

    for (uint32_t n = 0; n < count; n++) {
        if (d->lba48) {
            outb((uint16_t)(d->ch.io + 2), 0);
            outb((uint16_t)(d->ch.io + 3), (uint8_t)((block + n) >> 24));
            outb((uint16_t)(d->ch.io + 4), (uint8_t)((block + n) >> 32));
            outb((uint16_t)(d->ch.io + 5), (uint8_t)((block + n) >> 40));
            outb((uint16_t)(d->ch.io + 2), 1);
            outb((uint16_t)(d->ch.io + 3), (uint8_t)(block + n));
            outb((uint16_t)(d->ch.io + 4), (uint8_t)((block + n) >> 8));
            outb((uint16_t)(d->ch.io + 5), (uint8_t)((block + n) >> 16));
            select_drive(d, 0x40);
            outb((uint16_t)(d->ch.io + 7), 0x34);
        } else {
            select_drive(d, (uint8_t)(((block + n) >> 24) & 0x0F));
            outb((uint16_t)(d->ch.io + 2), 1);
            outb((uint16_t)(d->ch.io + 3), (uint8_t)(block + n));
            outb((uint16_t)(d->ch.io + 4), (uint8_t)((block + n) >> 8));
            outb((uint16_t)(d->ch.io + 5), (uint8_t)((block + n) >> 16));
            outb((uint16_t)(d->ch.io + 7), 0x30);
        }
        if (wait_drq(d) != 0) {
            return -EIO;
        }
        for (int i = 0; i < 256; i++) {
            outw(d->ch.io, src[n * 256 + i]);
        }
        delay_400ns(d);
        if (wait_not_busy(d) != 0) {
            return -EIO;
        }
    }
    return 0;
}

static int drive_flush(struct bdev *dev)
{
    struct pata_drive *d = dev->ctx;

    select_drive(d, 0);
    outb((uint16_t)(d->ch.io + 7), 0xE7);
    return wait_not_busy(d);
}

static int probe_drive(const struct pata_channel *ch, bool slave,
                       struct pata_drive *d)
{
    uint16_t words[256];
    uint64_t sectors;

    memset(d, 0, sizeof(*d));
    d->ch = *ch;
    d->slave = slave;

    if (identify(d, words) != 0) {
        return -ENODEV;
    }
    if (words[0] == 0 || words[0] == 0xFFFF) {
        return -ENODEV;
    }

    sectors = (uint64_t)words[60] | ((uint64_t)words[61] << 16);
    d->lba48 = (words[83] & (1u << 10)) != 0;
    if (d->lba48) {
        uint64_t ext = (uint64_t)words[100] | ((uint64_t)words[101] << 16) |
                       ((uint64_t)words[102] << 32) |
                       ((uint64_t)words[103] << 48);

        if (ext) {
            sectors = ext;
        }
    }
    if (!sectors) {
        return -ENODEV;
    }

    d->sectors = sectors;
    d->bdev.ctx = d;
    d->bdev.block_size = PATA_SECTOR;
    d->bdev.block_count = sectors;
    d->bdev.name = "pata";
    d->bdev.read_only = false;
    d->bdev.read = drive_read;
    d->bdev.write = drive_write;
    d->bdev.flush = drive_flush;
    return 0;
}

int pata_init(struct bdev *out, int max)
{
    int found = 0;

    if (!out || max <= 0) {
        return -EINVAL;
    }
    for (int ch = 0; ch < 2 && found < max; ch++) {
        for (int unit = 0; unit < 2 && found < max; unit++) {
            if (g_drive_count >= PATA_MAX_DRIVES) {
                break;
            }
            if (probe_drive(&g_channels[ch], unit == 1,
                            &g_drives[g_drive_count]) == 0) {
                struct pata_drive *d = &g_drives[g_drive_count];

                kprintf("pata: %s %s disk %llu sectors%s\n",
                        ch == 0 ? "primary" : "secondary",
                        unit ? "slave" : "master",
                        (unsigned long long)d->sectors,
                        d->lba48 ? " (LBA48)" : "");
                out[found++] = d->bdev;
                g_drive_count++;
            }
        }
    }
    return found;
}
