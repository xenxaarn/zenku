#include <devfs.h>
#include <errno.h>
#include <ext2.h>
#include <kio.h>
#include <ramdisk.h>
#include <string.h>
#include <types.h>
#include <vfs.h>

#define DEVFS_DIR_INO 0x0FFF

#define DEVFS_PREFIX     "/dev/"
#define DEVFS_PREFIX_LEN 5

enum fd_kind {
    FD_FREE = 0,
    FD_EXT2,
    FD_DEV,
};

struct fd_slot {
    uint8_t kind;
    uint8_t flags;
    uint32_t ino;
    const struct devfs_ops *dev;
    uint64_t offset;
};

static struct ramdisk g_ramdisk;
static struct ext2_fs g_fs;
static struct fd_slot g_fds[VFS_MAX_FDS];
static bool g_ready;

static bool is_dev_root(const char *path)
{
    return strcmp(path, "/dev") == 0 || strcmp(path, "/dev/") == 0;
}

/* Extract the device name from "/dev/<name>"; reject nested paths. */
static const char *dev_name(const char *path)
{
    const char *name;

    if (strncmp(path, DEVFS_PREFIX, DEVFS_PREFIX_LEN) != 0) {
        return NULL;
    }
    name = path + DEVFS_PREFIX_LEN;
    if (*name == '\0') {
        return NULL;
    }
    for (const char *p = name; *p; p++) {
        if (*p == '/') {
            return NULL;
        }
    }
    return name;
}

static bool path_touches_dev(const char *path)
{
    return is_dev_root(path) ||
           strncmp(path, DEVFS_PREFIX, DEVFS_PREFIX_LEN) == 0;
}

static int stat_ext2(const char *path, struct vfs_stat *out)
{
    struct ext2_inode inode;
    uint32_t ino;

    if (ext2_lookup(&g_fs, path, &ino) != 0) {
        return -ENOENT;
    }
    if (ext2_read_inode(&g_fs, ino, &inode) != 0) {
        return -EIO;
    }

    out->ino = ino;
    out->mode = inode.mode;
    out->size = ext2_inode_size(&g_fs, &inode);
    out->links = inode.links_count;
    return 0;
}

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

    memset(g_fds, 0, sizeof(g_fds));
    g_ready = true;
    kprintf("vfs: ext2 mounted, %u KiB blocks, %u inodes, %u groups\n",
            g_fs.block_size / 1024, g_fs.inodes_count, g_fs.group_count);
    kprintf("vfs: devfs ready (console, tty0, ttyS0, null, zero)\n");
    return 0;
}

bool vfs_ready(void)
{
    return g_ready;
}

int vfs_stat(const char *path, struct vfs_stat *out)
{
    const char *name;
    const struct devfs_ops *ops;

    if (!g_ready || !path || !out) {
        return -ENOENT;
    }

    if (is_dev_root(path)) {
        out->ino = DEVFS_DIR_INO;
        out->mode = S_IFDIR | 0555;
        out->size = 0;
        out->links = 2;
        return 0;
    }

    if (path_touches_dev(path)) {
        name = dev_name(path);
        if (!name || devfs_lookup(name, &ops) != 0) {
            return -ENOENT;
        }
        out->ino = ops->ino;
        out->mode = S_IFCHR | 0666;
        out->size = 0;
        out->links = 1;
        return 0;
    }

    return stat_ext2(path, out);
}

static struct fd_slot *fd_alloc(int flags, const struct vfs_stat *st,
                                const struct devfs_ops *dev)
{
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        struct fd_slot *slot = &g_fds[i];

        if (slot->kind != FD_FREE) {
            continue;
        }

        slot->flags = (uint8_t)flags;
        slot->offset = 0;
        slot->ino = st->ino;
        slot->dev = dev;
        slot->kind = dev ? FD_DEV : FD_EXT2;
        return slot;
    }
    return NULL;
}

int vfs_open(const char *path, int flags)
{
    struct vfs_stat st;
    const struct devfs_ops *dev = NULL;
    const char *name;
    struct fd_slot *slot;
    int access = flags & O_ACCMODE;
    int rc;

    if (!g_ready) {
        return -ENODEV;
    }
    if (access != O_RDONLY && access != O_WRONLY && access != O_RDWR) {
        return -EINVAL;
    }

    rc = vfs_stat(path, &st);
    if (rc < 0) {
        return rc;
    }
    if (S_ISDIR(st.mode)) {
        return -EISDIR;
    }

    if (S_ISCHR(st.mode)) {
        name = dev_name(path);
        if (!name || devfs_lookup(name, &dev) != 0) {
            return -ENOENT;
        }
        if (access != O_RDONLY && !dev->write) {
            return -EINVAL;
        }
        if (access != O_WRONLY && !dev->read) {
            return -EINVAL;
        }
    } else if (S_ISREG(st.mode)) {
        /* The root filesystem is read-only for now. */
        if (access != O_RDONLY) {
            return -EROFS;
        }
    } else {
        return -EINVAL;
    }

    slot = fd_alloc(flags, &st, dev);
    if (!slot) {
        return -EMFILE;
    }
    return (int)(slot - g_fds);
}

static struct fd_slot *fd_get(int fd)
{
    struct fd_slot *slot;

    if (fd < 0 || fd >= VFS_MAX_FDS) {
        return NULL;
    }
    slot = &g_fds[fd];
    return slot->kind == FD_FREE ? NULL : slot;
}

int64_t fd_read(int fd, void *buf, size_t len)
{
    struct fd_slot *slot = fd_get(fd);
    int64_t n;

    if (!slot) {
        return -EBADF;
    }
    if ((slot->flags & O_ACCMODE) == O_WRONLY) {
        return -EBADF;
    }
    if (!buf && len) {
        return -EFAULT;
    }

    if (slot->kind == FD_DEV) {
        if (!slot->dev || !slot->dev->read) {
            return -EINVAL;
        }
        n = slot->dev->read(NULL, buf, len);
    } else {
        struct ext2_inode inode;

        if (ext2_read_inode(&g_fs, slot->ino, &inode) != 0) {
            return -EIO;
        }
        n = ext2_read(&g_fs, &inode, slot->offset, buf, len);
    }

    if (n > 0) {
        slot->offset += (uint64_t)n;
    }
    return n;
}

int64_t fd_write(int fd, const void *buf, size_t len)
{
    struct fd_slot *slot = fd_get(fd);
    int64_t n;

    if (!slot) {
        return -EBADF;
    }
    if ((slot->flags & O_ACCMODE) == O_RDONLY) {
        return -EBADF;
    }
    if (!buf && len) {
        return -EFAULT;
    }

    if (slot->kind != FD_DEV) {
        return -EROFS;
    }
    if (!slot->dev || !slot->dev->write) {
        return -EINVAL;
    }

    n = slot->dev->write(NULL, buf, len);
    if (n > 0) {
        slot->offset += (uint64_t)n;
    }
    return n;
}

int64_t fd_lseek(int fd, int64_t offset, int whence)
{
    struct fd_slot *slot = fd_get(fd);
    struct ext2_inode inode;
    int64_t base;
    int64_t target;

    if (!slot) {
        return -EBADF;
    }
    if (slot->kind != FD_EXT2) {
        return -EINVAL;
    }
    if (ext2_read_inode(&g_fs, slot->ino, &inode) != 0) {
        return -EIO;
    }

    switch (whence) {
    case SEEK_SET:
        base = 0;
        break;
    case SEEK_CUR:
        base = (int64_t)slot->offset;
        break;
    case SEEK_END:
        base = (int64_t)ext2_inode_size(&g_fs, &inode);
        break;
    default:
        return -EINVAL;
    }

    target = base + offset;
    if (target < 0) {
        return -EINVAL;
    }

    slot->offset = (uint64_t)target;
    return target;
}

int fd_close(int fd)
{
    struct fd_slot *slot = fd_get(fd);

    if (!slot) {
        return -EBADF;
    }
    memset(slot, 0, sizeof(*slot));
    return 0;
}

int fd_fstat(int fd, struct vfs_stat *out)
{
    struct fd_slot *slot = fd_get(fd);
    struct ext2_inode inode;

    if (!slot || !out) {
        return -EBADF;
    }

    memset(out, 0, sizeof(*out));
    out->ino = slot->ino;
    if (slot->kind == FD_DEV) {
        out->mode = S_IFCHR | 0666;
        out->links = 1;
        return 0;
    }

    if (ext2_read_inode(&g_fs, slot->ino, &inode) != 0) {
        return -EIO;
    }
    out->mode = inode.mode;
    out->size = ext2_inode_size(&g_fs, &inode);
    out->links = inode.links_count;
    return 0;
}

int64_t vfs_read_file(const char *path, void *buf, uint64_t cap)
{
    int fd;
    int64_t n;

    fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        return fd;
    }

    n = fd_read(fd, buf, (size_t)cap);
    fd_close(fd);
    return n;
}

int vfs_list(const char *path, vfs_dir_cb cb, void *ctx)
{
    struct vfs_stat st;
    uint32_t ino;
    int rc;

    if (!cb) {
        return -EINVAL;
    }
    if (is_dev_root(path)) {
        return devfs_list((devfs_dir_cb)cb, ctx);
    }
    if (path_touches_dev(path)) {
        return -ENOTDIR;
    }

    rc = vfs_stat(path, &st);
    if (rc < 0) {
        return rc;
    }
    if (!S_ISDIR(st.mode)) {
        return -ENOTDIR;
    }

    ino = st.ino;
    return ext2_iterate(&g_fs, ino, (ext2_dir_cb)cb, ctx);
}
