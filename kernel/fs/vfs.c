#include <ext2.h>
#include <kio.h>
#include <ramdisk.h>
#include <types.h>
#include <vfs.h>

static struct ramdisk g_ramdisk;
static struct ext2_fs g_fs;
static bool g_ready;

int vfs_init(uint64_t initrd_phys, uint64_t initrd_size)
{
    uint32_t root;

    if (ramdisk_init(&g_ramdisk, initrd_phys, initrd_size) != 0) {
        kprintf("vfs: initrd is unavailable\n");
        return -1;
    }
    if (ext2_mount(&g_fs, &g_ramdisk.bdev) != 0) {
        kprintf("vfs: no supported ext2 filesystem on the initrd\n");
        return -1;
    }
    if (ext2_lookup(&g_fs, "/", &root) != 0 || root != EXT2_ROOT_INO) {
        kprintf("vfs: root inode is missing\n");
        return -1;
    }

    g_ready = true;
    kprintf("vfs: ext2 mounted, %u KiB blocks, %u inodes, %u groups\n",
            g_fs.block_size / 1024, g_fs.inodes_count, g_fs.group_count);
    return 0;
}

bool vfs_ready(void)
{
    return g_ready;
}

int vfs_resolve(const char *path, uint32_t *ino_out)
{
    if (!g_ready || !path || !ino_out) {
        return -1;
    }
    return ext2_lookup(&g_fs, path, ino_out);
}

int vfs_stat(const char *path, uint32_t *ino_out, struct ext2_inode *inode)
{
    uint32_t ino;

    if (!inode || vfs_resolve(path, &ino) != 0) {
        return -1;
    }
    if (ext2_read_inode(&g_fs, ino, inode) != 0) {
        return -1;
    }
    if (ino_out) {
        *ino_out = ino;
    }
    return 0;
}

int64_t vfs_read_file(const char *path, void *buf, uint64_t cap)
{
    struct ext2_inode inode;
    uint64_t size;

    if (!buf || vfs_stat(path, NULL, &inode) != 0) {
        return -1;
    }
    if (ext2_is_dir(&inode) || !ext2_is_file(&inode)) {
        return -1;
    }

    size = ext2_inode_size(&g_fs, &inode);
    if (size > cap) {
        size = cap;
    }
    return ext2_read(&g_fs, &inode, 0, buf, size);
}

int vfs_list(const char *path, vfs_dir_cb cb, void *ctx)
{
    uint32_t ino;

    if (!cb || vfs_resolve(path, &ino) != 0) {
        return -1;
    }
    return ext2_iterate(&g_fs, ino, (ext2_dir_cb)cb, ctx);
}
