#include <errno.h>
#include <partition.h>
#include <string.h>
#include <types.h>

#define SECTOR 512

#define MBR_PART_OFFSET 446
#define MBR_PART_COUNT  4
#define MBR_TYPE_GPT    0xEE
#define MBR_TYPE_EXTENDED 0x05
#define MBR_TYPE_EXTENDED_LBA 0x0F

static uint32_t crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;

    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);

            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static uint32_t le32(const uint8_t *p)
{
    uint32_t v;

    memcpy(&v, p, sizeof(v));
    return v;
}

static uint64_t le64(const uint8_t *p)
{
    uint64_t v;

    memcpy(&v, p, sizeof(v));
    return v;
}

static int part_read(struct bdev *dev, uint64_t block, void *buf,
                     uint32_t count)
{
    struct part_entry *p = dev->ctx;

    if (block + count > dev->block_count) {
        return -EIO;
    }
    return bdev_read(p->parent, p->start + block, buf, count);
}

static int part_write(struct bdev *dev, uint64_t block, const void *buf,
                      uint32_t count)
{
    struct part_entry *p = dev->ctx;

    if (block + count > dev->block_count) {
        return -EIO;
    }
    return bdev_write(p->parent, p->start + block, buf, count);
}

static int part_flush(struct bdev *dev)
{
    struct part_entry *p = dev->ctx;

    return bdev_flush(p->parent);
}

static void add_partition(struct bdev *disk, struct part_entry *out,
                          uint64_t start, uint64_t sectors)
{
    struct part_entry *p = out;

    p->parent = disk;
    p->start = start;
    p->bdev.ctx = p;
    p->bdev.block_size = disk->block_size;
    p->bdev.block_count = sectors;
    p->bdev.name = disk->name;
    p->bdev.read_only = disk->read_only;
    p->bdev.read = part_read;
    p->bdev.write = part_write;
    p->bdev.flush = part_flush;
}

static void add_whole_disk(struct bdev *disk, struct part_entry *out)
{
    add_partition(disk, out, 0, disk->block_count);
}

static int parse_gpt(struct bdev *disk, struct part_entry *out, int max)
{
    static uint8_t header[SECTOR];
    static uint8_t entries[128 * 128];
    uint32_t header_size;
    uint32_t header_crc;
    uint32_t entry_lba;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t array_crc;
    uint32_t array_bytes;
    uint8_t saved[4];
    int found = 0;

    if (bdev_read(disk, 1, header, 1) != 0) {
        return -EIO;
    }
    if (memcmp(header, "EFI PART", 8) != 0) {
        return -ENOENT;
    }
    header_size = le32(header + 12);
    if (header_size < 92 || header_size > SECTOR) {
        return -EINVAL;
    }
    header_crc = le32(header + 16);
    memcpy(saved, header + 16, 4);
    memset(header + 16, 0, 4);
    if (crc32(header, header_size) != header_crc) {
        memcpy(header + 16, saved, 4);
        return -EINVAL;
    }
    memcpy(header + 16, saved, 4);

    entry_lba = le32(header + 72);
    entry_count = le32(header + 80);
    entry_size = le32(header + 84);
    array_crc = le32(header + 88);
    if (entry_count == 0 || entry_count > 128 || entry_size < 128 ||
        entry_size > 128) {
        return -EINVAL;
    }
    array_bytes = entry_count * entry_size;
    if (array_bytes > sizeof(entries) ||
        entry_lba + array_bytes / SECTOR > disk->block_count) {
        return -EINVAL;
    }
    if (bdev_read(disk, entry_lba, entries,
                  array_bytes / SECTOR) != 0) {
        return -EIO;
    }
    if (crc32(entries, array_bytes) != array_crc) {
        return -EINVAL;
    }

    for (uint32_t i = 0; i < entry_count && found < max; i++) {
        const uint8_t *e = entries + i * entry_size;
        bool zero = true;

        for (int b = 0; b < 16; b++) {
            if (e[b]) {
                zero = false;
                break;
            }
        }
        if (zero) {
            continue;
        }
        {
            uint64_t first = le64(e + 32);
            uint64_t last = le64(e + 40);

            if (last < first || last >= disk->block_count) {
                continue;
            }
            add_partition(disk, &out[found], first, last - first + 1);
            found++;
        }
    }
    return found;
}

int partition_scan(struct bdev *disk, struct part_entry *out, int max)
{
    uint8_t mbr[SECTOR];
    bool protective = false;
    int found = 0;

    if (!disk || !out || max <= 0) {
        return -EINVAL;
    }
    if (bdev_read(disk, 0, mbr, 1) != 0) {
        return -EIO;
    }
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        add_whole_disk(disk, &out[0]);
        return 1;
    }

    for (int i = 0; i < MBR_PART_COUNT; i++) {
        if (mbr[MBR_PART_OFFSET + i * 16 + 4] == MBR_TYPE_GPT) {
            protective = true;
            break;
        }
    }
    if (protective) {
        int rc = parse_gpt(disk, out, max);

        if (rc >= 0) {
            return rc;
        }
    }

    for (int i = 0; i < MBR_PART_COUNT && found < max; i++) {
        const uint8_t *e = mbr + MBR_PART_OFFSET + i * 16;
        uint8_t type = e[4];
        uint64_t start = le32(e + 8);
        uint64_t count = le32(e + 12);

        if (type == 0 || type == MBR_TYPE_GPT ||
            type == MBR_TYPE_EXTENDED || type == MBR_TYPE_EXTENDED_LBA) {
            continue;
        }
        if (!count || start + count > disk->block_count) {
            continue;
        }
        add_partition(disk, &out[found], start, count);
        found++;
    }

    if (!found) {
        add_whole_disk(disk, &out[0]);
        found = 1;
    }
    return found;
}
