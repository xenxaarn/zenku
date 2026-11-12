#ifndef ZENKU_VFS_H
#define ZENKU_VFS_H

#include <ext2.h>
#include <types.h>

/* File types stored in st_mode. */
#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000

#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)

/* open(2) flags. Only the access mode is implemented for now. */
#define O_RDONLY  0x0000
#define O_WRONLY  0x0001
#define O_RDWR    0x0002
#define O_ACCMODE 0x0003

/* lseek(2) whence values. */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define VFS_MAX_FDS 32
#define VFS_MAX_FD_TABLES 32

struct vfs_stat {
    uint32_t ino;
    uint32_t mode;
    uint64_t size;
    uint32_t links;
};

/* Directory visitor: return 0 to continue, non-zero to stop. */
typedef int (*vfs_dir_cb)(const char *name, uint32_t name_len, uint32_t ino,
                          uint8_t type, void *ctx);

int vfs_init(uint64_t initrd_phys, uint64_t initrd_size);
bool vfs_ready(void);

int vfs_open(const char *path, int flags);
int64_t fd_read(int fd, void *buf, size_t len);
int64_t fd_write(int fd, const void *buf, size_t len);
int64_t fd_lseek(int fd, int64_t offset, int whence);
int fd_close(int fd);
int fd_fstat(int fd, struct vfs_stat *out);

/* True when fd refers to the interactive terminal device. */
bool fd_is_console(int fd);

/* Create a non-blocking pipe; fds[0] is the read end, fds[1] the write end. */
int vfs_pipe(int fds[2]);

/* File descriptor tables. Table 0 belongs to the kernel console; each
 * process owns (and inherits on fork) its own table. */
int vfs_fdtable_create(void);
int vfs_fdtable_clone(int id);
void vfs_fdtable_destroy(int id);
void vfs_fdtable_select(int id);

int vfs_stat(const char *path, struct vfs_stat *out);
int64_t vfs_read_file(const char *path, void *buf, uint64_t cap);
int vfs_list(const char *path, vfs_dir_cb cb, void *ctx);

#endif /* ZENKU_VFS_H */
