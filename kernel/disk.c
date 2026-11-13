#include <ahci.h>
#include <bdev.h>
#include <disk.h>
#include <errno.h>
#include <ext2.h>
#include <ext2_write.h>
#include <kio.h>
#include <partition.h>
#include <pata.h>
#include <rtc.h>
#include <string.h>
#include <types.h>
#include <vfs.h>

#define MAX_DISKS 8
#define MAX_PARTS 8

static struct bdev g_disks[MAX_DISKS];
static struct part_entry g_parts[MAX_DISKS][MAX_PARTS];
static struct ext2_fs g_data_fs;
static struct ext2_fs g_probe_fs;

static uint32_t rtc_epoch(void)
{
    struct rtc_time t;
    int y;
    int m;
    int d;
    int era;
    unsigned yoe;
    unsigned doy;
    unsigned doe;
    long days;

    if (rtc_read(&t) != 0) {
        return 0;
    }
    y = t.year;
    m = t.month;
    d = t.day;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    days = (long)era * 146097 + (long)doe - 719468;
    return (uint32_t)(days * 86400 + t.hour * 3600 + t.minute * 60 +
                      t.second);
}

void disk_init(void)
{
    struct bdev *chosen = NULL;
    bool chosen_writable = false;
    bool chosen_data = false;
    int disk_count = 0;
    int rc;

    ext2_set_time_source(rtc_epoch);

    rc = ahci_init(g_disks, MAX_DISKS);
    if (rc > 0) {
        disk_count += rc;
    }
    rc = pata_init(&g_disks[disk_count], MAX_DISKS - disk_count);
    if (rc > 0) {
        disk_count += rc;
    }
    if (disk_count == 0) {
        kprintf("disk: no writable storage found; /data is unavailable\n");
        return;
    }

    for (int d = 0; d < disk_count; d++) {
        int count;

        kprintf("disk: probing %s disk %d (%llu sectors)\n",
                g_disks[d].name ? g_disks[d].name : "block",
                d, (unsigned long long)g_disks[d].block_count);
        count = partition_scan(&g_disks[d], g_parts[d], MAX_PARTS);
        if (count < 0) {
            kprintf("disk: partition scan failed (%d)\n", count);
            continue;
        }
        for (int p = 0; p < count; p++) {
            struct ext2_fs *probe = &g_probe_fs;
            struct bdev *part = &g_parts[d][p].bdev;

            if (ext2_mount(probe, part) != 0) {
                continue;
            }
            kprintf("disk: partition %d.%d ext2, %u KiB blocks, label \"%s\"\n",
                    d, p, probe->block_size / 1024, probe->label);
            if (strcmp(probe->label, "ZENKU_DATA") == 0) {
                bool usable = ext2_can_write(probe) && bdev_writable(part);

                if (!chosen || !chosen_writable) {
                    chosen = part;
                    chosen_writable = usable;
                    chosen_data = true;
                }
            } else if (!chosen) {
                chosen = part;
                chosen_writable = false;
                chosen_data = false;
            }
        }
    }

    if (!chosen) {
        kprintf("disk: no ext2 data volume; /data is unavailable\n");
        return;
    }
    if (ext2_mount(&g_data_fs, chosen) != 0) {
        kprintf("disk: data volume disappeared during mount\n");
        return;
    }

    if (!chosen_writable && chosen_data && !ext2_can_write(&g_data_fs)) {
        kprintf("disk: ZENKU_DATA needs recovery or has unsupported "
                "features\n");
    }
    if (vfs_mount_data(&g_data_fs, chosen_writable) != 0) {
        kprintf("disk: cannot register /data mount\n");
        return;
    }
    kprintf("disk: /data mounted %s (%u free blocks)\n",
            chosen_writable ? "read-write" : "read-only",
            g_data_fs.free_blocks_count);
    if (!chosen_writable) {
        kprintf("disk: only a volume labelled ZENKU_DATA is mounted "
                "read-write\n");
    }
}
