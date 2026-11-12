#ifndef ZENKU_VFS_H
#define ZENKU_VFS_H

#include <ext2.h>
#include <types.h>

/* Directory visitor: return 0 to continue, non-zero to stop. */
typedef int (*vfs_dir_cb)(const char *name, uint32_t name_len, uint32_t ino,
                          uint8_t type, void *ctx);

int vfs_init(uint64_t initrd_phys, uint64_t initrd_size);
bool vfs_ready(void);

int vfs_resolve(const char *path, uint32_t *ino_out);
int vfs_stat(const char *path, uint32_t *ino_out, struct ext2_inode *inode);
int64_t vfs_read_file(const char *path, void *buf, uint64_t cap);
int vfs_list(const char *path, vfs_dir_cb cb, void *ctx);

#endif /* ZENKU_VFS_H */
