#include <devfs.h>
#include <errno.h>
#include <ext2.h>
#include <ext2_write.h>
#include <kio.h>
#include <pipe.h>
#include <ramdisk.h>
#include <string.h>
#include <types.h>
#include <vfs.h>

#define DEVFS_DIR_INO 0x0FFF

#define DEVFS_PREFIX     "/dev/"
#define DEVFS_PREFIX_LEN 5

#define VFS_MAX_MOUNTS 4

struct mount {
    bool used;
    bool writable;
    char path[VFS_NAME_MAX];
    struct ext2_fs *fs;
};

enum fd_kind {
    FD_FREE = 0,
    FD_EXT2,
    FD_DEV,
    FD_PIPE,
};

struct fd_slot {
    uint8_t kind;
    uint32_t flags;
    uint32_t ino;
    const struct devfs_ops *dev;
    struct pipe *pipe;
    bool pipe_write_end;
    bool writable;
    struct ext2_fs *fs;
    uint64_t offset;
};

struct fd_table {
    struct fd_slot slots[VFS_MAX_FDS];
    char cwd[VFS_NAME_MAX];
};

static struct ramdisk g_ramdisk;
static struct ext2_fs g_fs;
static struct mount g_mounts[VFS_MAX_MOUNTS];
static struct fd_table g_fd_tables[VFS_MAX_FD_TABLES];
static bool g_fd_table_used[VFS_MAX_FD_TABLES];
static struct fd_slot *g_fds;
static char *g_cwd;
static int g_active_table;
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

/*
 * Find the mount that owns `path` using longest-prefix matching. The
 * filesystem-relative path is written to `rel` (always starting with '/').
 */
static struct mount *mount_for_path(const char *path, char *rel, size_t cap)
{
    struct mount *best = NULL;
    size_t best_len = 0;

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        struct mount *m = &g_mounts[i];
        size_t len;

        if (!m->used) {
            continue;
        }
        len = strlen(m->path);
        if (len < best_len) {
            continue;
        }
        if (len == 1 && m->path[0] == '/') {
            if (path[0] == '/') {
                best = m;
                best_len = len;
            }
            continue;
        }
        if (strncmp(path, m->path, len) != 0) {
            continue;
        }
        if (path[len] != '\0' && path[len] != '/') {
            continue;
        }
        best = m;
        best_len = len;
    }

    if (!best) {
        return NULL;
    }
    {
        const char *sub = (best_len == 1) ? path : path + best_len;

        if (*sub == '\0') {
            sub = "/";
        }
        if (strlen(sub) >= cap) {
            return NULL;
        }
        strcpy(rel, sub);
    }
    return best;
}

static int stat_ext2(struct ext2_fs *fs, const char *path,
                     struct vfs_stat *out)
{
    struct ext2_inode inode;
    uint32_t ino;

    if (ext2_lookup(fs, path, &ino) != 0) {
        return -ENOENT;
    }
    if (ext2_read_inode(fs, ino, &inode) != 0) {
        return -EIO;
    }

    out->ino = ino;
    out->mode = inode.mode;
    out->size = ext2_inode_size(fs, &inode);
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

    memset(&g_fd_tables[0], 0, sizeof(g_fd_tables[0]));
    memset(g_fd_table_used, 0, sizeof(g_fd_table_used));
    memset(g_mounts, 0, sizeof(g_mounts));
    g_fd_table_used[0] = true;
    g_active_table = 0;
    g_fds = g_fd_tables[0].slots;
    g_fd_tables[0].cwd[0] = '/';
    g_fd_tables[0].cwd[1] = '\0';
    g_cwd = g_fd_tables[0].cwd;
    g_mounts[0].used = true;
    g_mounts[0].writable = false;
    g_mounts[0].fs = &g_fs;
    strcpy(g_mounts[0].path, "/");
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

int vfs_mount_data(struct ext2_fs *fs, bool writable)
{
    int slot = -1;

    if (!g_ready || !fs) {
        return -ENODEV;
    }
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return -ENFILE;
    }
    g_mounts[slot].used = true;
    g_mounts[slot].writable = writable;
    g_mounts[slot].fs = fs;
    strcpy(g_mounts[slot].path, "/data");
    return 0;
}

/* Normalize an absolute or relative path into an absolute path in `out`.
 * Components are collapsed, "." is dropped, and ".." never escapes root. */
int vfs_resolve(const char *path, char *out, size_t cap)
{
    const char *p;
    size_t used = 0;

    if (!path || !*path || !out || cap < 2) {
        return -ENOENT;
    }

    if (path[0] != '/') {
        const char *base = g_cwd ? g_cwd : "/";

        while (*base && used + 1 < cap) {
            out[used++] = *base++;
        }
        if (used == 0) {
            out[used++] = '/';
        }
    }

    p = path;
    while (*p) {
        const char *start;
        size_t len;

        while (*p == '/') {
            p++;
        }
        if (!*p) {
            break;
        }
        start = p;
        while (*p && *p != '/') {
            p++;
        }
        len = (size_t)(p - start);

        if (len == 1 && start[0] == '.') {
            continue;
        }
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            while (used > 1 && out[used - 1] != '/') {
                used--;
            }
            if (used > 1) {
                used--;
            }
            continue;
        }
        if (used == 0 || out[used - 1] != '/') {
            if (used + 1 >= cap) {
                return -ENAMETOOLONG;
            }
            out[used++] = '/';
        }
        if (used + len >= cap) {
            return -ENAMETOOLONG;
        }
        memcpy(out + used, start, len);
        used += len;
    }

    if (used == 0) {
        out[used++] = '/';
    }
    out[used] = '\0';
    return 0;
}

int vfs_stat(const char *path, struct vfs_stat *out)
{
    char resolved[VFS_NAME_MAX];
    int rc;
    const char *name;
    const struct devfs_ops *ops;

    if (!g_ready || !path || !out) {
        return -ENOENT;
    }
    rc = vfs_resolve(path, resolved, sizeof(resolved));
    if (rc < 0) {
        return rc;
    }
    path = resolved;

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

    {
        char rel[VFS_NAME_MAX];
        struct mount *m = mount_for_path(path, rel, sizeof(rel));

        if (!m) {
            return -ENOENT;
        }
        return stat_ext2(m->fs, rel, out);
    }
}

static struct fd_slot *fd_alloc(uint32_t flags, const struct vfs_stat *st,
                                const struct devfs_ops *dev,
                                struct ext2_fs *fs, bool writable)
{
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        struct fd_slot *slot = &g_fds[i];

        if (slot->kind != FD_FREE) {
            continue;
        }

        slot->flags = flags;
        slot->offset = 0;
        slot->ino = st->ino;
        slot->dev = dev;
        slot->pipe = NULL;
        slot->pipe_write_end = false;
        slot->writable = writable;
        slot->fs = fs;
        slot->kind = dev ? FD_DEV : FD_EXT2;
        return slot;
    }
    return NULL;
}

int vfs_open(const char *path, int flags)
{
    char resolved[VFS_NAME_MAX];
    char rel[VFS_NAME_MAX];
    struct vfs_stat st;
    const struct devfs_ops *dev = NULL;
    struct mount *m;
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
    rc = vfs_resolve(path, resolved, sizeof(resolved));
    if (rc < 0) {
        return rc;
    }
    path = resolved;

    rc = vfs_stat(path, &st);
    if (rc < 0) {
        if (rc != -ENOENT || !(flags & O_CREAT)) {
            return rc;
        }
        m = mount_for_path(path, rel, sizeof(rel));
        if (!m) {
            return -ENOENT;
        }
        if (!m->writable) {
            return -EROFS;
        }
        rc = ext2_wcreate(m->fs, rel, 0644, NULL);
        if (rc < 0) {
            return rc;
        }
        rc = vfs_stat(path, &st);
        if (rc < 0) {
            return rc;
        }
    } else if ((flags & O_CREAT) && (flags & O_EXCL)) {
        return -EEXIST;
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
        m = NULL;
    } else if (S_ISREG(st.mode)) {
        m = mount_for_path(path, rel, sizeof(rel));
        if (!m) {
            return -ENOENT;
        }
        if (access != O_RDONLY && !m->writable) {
            return -EROFS;
        }
        if ((flags & O_TRUNC) && access != O_RDONLY) {
            rc = ext2_wtruncate(m->fs, st.ino, 0);
            if (rc < 0) {
                return rc;
            }
        }
    } else {
        return -EINVAL;
    }

    slot = fd_alloc((uint32_t)flags, &st, dev, m ? m->fs : NULL,
                    m ? m->writable : false);
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

    if (slot->kind == FD_PIPE) {
        if (slot->pipe_write_end) {
            return -EBADF;
        }
        n = pipe_read(slot->pipe, buf, len);
    } else if (slot->kind == FD_DEV) {
        if (!slot->dev || !slot->dev->read) {
            return -EINVAL;
        }
        n = slot->dev->read(NULL, buf, len);
    } else {
        struct ext2_inode inode;

        if (!slot->fs || ext2_read_inode(slot->fs, slot->ino, &inode) != 0) {
            return -EIO;
        }
        n = ext2_read(slot->fs, &inode, slot->offset, buf, len);
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

    if (slot->kind == FD_PIPE) {
        if (!slot->pipe_write_end) {
            return -EBADF;
        }
        n = pipe_write(slot->pipe, buf, len);
    } else if (slot->kind != FD_DEV) {
        struct ext2_inode inode;
        uint64_t offset;

        if (!slot->fs || !slot->writable) {
            return -EROFS;
        }
        if (ext2_read_inode(slot->fs, slot->ino, &inode) != 0) {
            return -EIO;
        }
        offset = (slot->flags & O_APPEND) ?
                 ext2_inode_size(slot->fs, &inode) : slot->offset;
        n = ext2_wwrite(slot->fs, slot->ino, offset, buf, len);
        if (n > 0) {
            slot->offset = offset + (uint64_t)n;
            return n;
        }
        return n;
    } else {
        if (!slot->dev || !slot->dev->write) {
            return -EINVAL;
        }
        n = slot->dev->write(NULL, buf, len);
    }
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
    if (!slot->fs || ext2_read_inode(slot->fs, slot->ino, &inode) != 0) {
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
        base = (int64_t)ext2_inode_size(slot->fs, &inode);
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
    if (slot->kind == FD_PIPE) {
        pipe_drop_end(slot->pipe, slot->pipe_write_end);
    }
    memset(slot, 0, sizeof(*slot));
    return 0;
}

int vfs_dup2(int oldfd, int newfd)
{
    struct fd_slot *old = fd_get(oldfd);
    struct fd_slot *slot;

    if (!old) {
        return -EBADF;
    }
    if (newfd < 0 || newfd >= VFS_MAX_FDS) {
        return -EBADF;
    }
    if (oldfd == newfd) {
        return newfd;
    }

    slot = &g_fds[newfd];
    if (slot->kind == FD_PIPE) {
        pipe_drop_end(slot->pipe, slot->pipe_write_end);
    }
    if (old->kind == FD_PIPE) {
        pipe_add_end(old->pipe, old->pipe_write_end);
    }
    *slot = *old;
    return newfd;
}

static struct fd_slot *table_slots(int id)
{
    if (id < 0 || id >= VFS_MAX_FD_TABLES || !g_fd_table_used[id]) {
        return NULL;
    }
    return g_fd_tables[id].slots;
}

int vfs_fdtable_create(void)
{
    for (int id = 1; id < VFS_MAX_FD_TABLES; id++) {
        if (g_fd_table_used[id]) {
            continue;
        }
        memset(&g_fd_tables[id], 0, sizeof(g_fd_tables[id]));
        g_fd_tables[id].cwd[0] = '/';
        g_fd_tables[id].cwd[1] = '\0';
        g_fd_table_used[id] = true;
        return id;
    }
    return -EMFILE;
}

int vfs_fdtable_clone(int id)
{
    struct fd_slot *src = table_slots(id);
    int dst_id;
    struct fd_slot *dst;

    if (!src) {
        return -EBADF;
    }
    dst_id = vfs_fdtable_create();
    if (dst_id < 0) {
        return dst_id;
    }
    dst = g_fd_tables[dst_id].slots;
    memcpy(dst, src, sizeof(g_fd_tables[dst_id].slots));
    memcpy(g_fd_tables[dst_id].cwd, g_fd_tables[id].cwd,
           sizeof(g_fd_tables[dst_id].cwd));

    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (dst[i].kind == FD_PIPE) {
            pipe_add_end(dst[i].pipe, dst[i].pipe_write_end);
        }
    }
    return dst_id;
}

void vfs_fdtable_destroy(int id)
{
    struct fd_slot *slots = table_slots(id);

    if (!slots || id == 0) {
        return;
    }
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (slots[i].kind == FD_PIPE) {
            pipe_drop_end(slots[i].pipe, slots[i].pipe_write_end);
        }
        memset(&slots[i], 0, sizeof(slots[i]));
    }
    g_fd_table_used[id] = false;
    if (g_active_table == id) {
        vfs_fdtable_select(0);
    }
}

void vfs_fdtable_select(int id)
{
    struct fd_slot *slots = table_slots(id);

    if (!slots) {
        slots = g_fd_tables[0].slots;
        id = 0;
    }
    g_active_table = id;
    g_fds = slots;
    g_cwd = g_fd_tables[id].cwd;
    if (!g_cwd[0]) {
        g_cwd[0] = '/';
        g_cwd[1] = '\0';
    }
}

bool fd_is_console(int fd)
{
    struct fd_slot *slot = fd_get(fd);

    if (!slot || slot->kind != FD_DEV || !slot->dev || !slot->dev->name) {
        return false;
    }
    return strcmp(slot->dev->name, "console") == 0 ||
           strcmp(slot->dev->name, "tty0") == 0;
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
    if (slot->kind == FD_PIPE) {
        out->mode = S_IFIFO | 0666;
        out->links = 1;
        return 0;
    }
    if (slot->kind == FD_DEV) {
        out->mode = S_IFCHR | 0666;
        out->links = 1;
        return 0;
    }

    if (ext2_read_inode(slot->fs, slot->ino, &inode) != 0) {
        return -EIO;
    }
    out->mode = inode.mode;
    out->size = ext2_inode_size(slot->fs, &inode);
    out->links = inode.links_count;
    return 0;
}

int vfs_pipe(int fds[2])
{
    struct pipe *pipe;
    struct fd_slot *read_slot = NULL;
    struct fd_slot *write_slot = NULL;
    int read_fd = -1;
    int write_fd = -1;

    if (!fds) {
        return -EINVAL;
    }

    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (g_fds[i].kind != FD_FREE) {
            continue;
        }
        if (!read_slot) {
            read_slot = &g_fds[i];
            read_fd = i;
        } else {
            write_slot = &g_fds[i];
            write_fd = i;
            break;
        }
    }
    if (!read_slot || !write_slot) {
        return -EMFILE;
    }

    pipe = pipe_create();
    if (!pipe) {
        return -ENFILE;
    }

    pipe_add_end(pipe, false);
    pipe_add_end(pipe, true);

    memset(read_slot, 0, sizeof(*read_slot));
    read_slot->kind = FD_PIPE;
    read_slot->flags = O_RDONLY;
    read_slot->pipe = pipe;

    memset(write_slot, 0, sizeof(*write_slot));
    write_slot->kind = FD_PIPE;
    write_slot->flags = O_WRONLY;
    write_slot->pipe = pipe;
    write_slot->pipe_write_end = true;

    fds[0] = read_fd;
    fds[1] = write_fd;
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
    char resolved[VFS_NAME_MAX];
    struct vfs_stat st;
    uint32_t ino;
    int rc;

    if (!cb) {
        return -EINVAL;
    }
    if (vfs_resolve(path, resolved, sizeof(resolved)) < 0) {
        return -ENOENT;
    }
    path = resolved;
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
    {
        char rel[VFS_NAME_MAX];
        struct mount *m = mount_for_path(path, rel, sizeof(rel));

        if (!m) {
            return -ENOENT;
        }
        return ext2_iterate(m->fs, ino, (ext2_dir_cb)cb, ctx);
    }
}

struct readdir_ctx {
    uint32_t index;
    uint32_t current;
    struct vfs_dirent *out;
};

static int readdir_cb(const char *name, uint32_t name_len, uint32_t ino,
                      uint8_t type, void *opaque)
{
    struct readdir_ctx *ctx = opaque;
    uint32_t n;

    if (ctx->current++ != ctx->index) {
        return 0;
    }

    memset(ctx->out, 0, sizeof(*ctx->out));
    ctx->out->ino = ino;
    ctx->out->type = type;
    n = name_len;
    if (n >= VFS_NAME_MAX) {
        n = VFS_NAME_MAX - 1;
    }
    if (name && n) {
        memcpy(ctx->out->name, name, n);
    }
    ctx->out->name_len = (uint8_t)n;
    return 1;
}

int vfs_readdir(const char *path, uint32_t index, struct vfs_dirent *out)
{
    struct readdir_ctx ctx;
    int rc;

    if (!out) {
        return -EFAULT;
    }

    ctx.index = index;
    ctx.current = 0;
    ctx.out = out;
    rc = vfs_list(path, readdir_cb, &ctx);
    if (rc == 1) {
        return 0;
    }
    if (rc < 0) {
        return rc;
    }
    return -ENOENT;
}

int vfs_chdir(const char *path)
{
    char resolved[VFS_NAME_MAX];
    struct vfs_stat st;
    int rc;

    rc = vfs_resolve(path, resolved, sizeof(resolved));
    if (rc < 0) {
        return rc;
    }
    rc = vfs_stat(resolved, &st);
    if (rc < 0) {
        return rc;
    }
    if (!S_ISDIR(st.mode)) {
        return -ENOTDIR;
    }
    if (!g_cwd) {
        return -ENODEV;
    }
    memcpy(g_cwd, resolved, strlen(resolved) + 1);
    return 0;
}

int vfs_getcwd(char *out, size_t cap)
{
    size_t len;

    if (!out || cap < 2 || !g_cwd) {
        return -EINVAL;
    }
    len = strlen(g_cwd) + 1;
    if (len > cap) {
        return -ERANGE;
    }
    memcpy(out, g_cwd, len);
    return (int)len;
}

static int writable_mount_for(const char *path, char *rel, size_t cap,
                              struct mount **out)
{
    char resolved[VFS_NAME_MAX];
    struct mount *m;
    int rc;

    rc = vfs_resolve(path, resolved, sizeof(resolved));
    if (rc < 0) {
        return rc;
    }
    m = mount_for_path(resolved, rel, cap);
    if (!m) {
        return -ENOENT;
    }
    if (!m->writable) {
        return -EROFS;
    }
    *out = m;
    return 0;
}

int vfs_mkdir(const char *path, uint16_t mode)
{
    char rel[VFS_NAME_MAX];
    struct mount *m;
    int rc;

    if (!g_ready || !path) {
        return -ENODEV;
    }
    rc = writable_mount_for(path, rel, sizeof(rel), &m);
    if (rc < 0) {
        return rc;
    }
    if (mode == 0) {
        mode = 0755;
    }
    return ext2_wmkdir(m->fs, rel, mode);
}

int vfs_rmdir(const char *path)
{
    char rel[VFS_NAME_MAX];
    struct mount *m;
    int rc;

    if (!g_ready || !path) {
        return -ENODEV;
    }
    rc = writable_mount_for(path, rel, sizeof(rel), &m);
    if (rc < 0) {
        return rc;
    }
    if (strcmp(rel, "/") == 0) {
        return -EBUSY;
    }
    return ext2_wrmdir(m->fs, rel);
}

int vfs_unlink(const char *path)
{
    char rel[VFS_NAME_MAX];
    struct mount *m;
    int rc;

    if (!g_ready || !path) {
        return -ENODEV;
    }
    rc = writable_mount_for(path, rel, sizeof(rel), &m);
    if (rc < 0) {
        return rc;
    }
    return ext2_wunlink(m->fs, rel);
}

int vfs_truncate(const char *path, uint64_t size)
{
    char rel[VFS_NAME_MAX];
    struct mount *m;
    struct vfs_stat st;
    int rc;

    if (!g_ready || !path) {
        return -ENODEV;
    }
    rc = writable_mount_for(path, rel, sizeof(rel), &m);
    if (rc < 0) {
        return rc;
    }
    rc = stat_ext2(m->fs, rel, &st);
    if (rc < 0) {
        return rc;
    }
    if (S_ISDIR(st.mode)) {
        return -EISDIR;
    }
    return ext2_wtruncate(m->fs, st.ino, size);
}

int fd_fsync(int fd)
{
    struct fd_slot *slot = fd_get(fd);

    if (!slot) {
        return -EBADF;
    }
    if (slot->kind != FD_EXT2) {
        return 0;
    }
    if (!slot->fs || !slot->writable) {
        return -EROFS;
    }
    return ext2_wsync(slot->fs);
}

int vfs_sync(void)
{
    int rc = 0;

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        int one;

        if (!g_mounts[i].used || !g_mounts[i].writable) {
            continue;
        }
        one = ext2_wsync(g_mounts[i].fs);
        if (one < 0) {
            rc = one;
        }
    }
    return rc;
}
