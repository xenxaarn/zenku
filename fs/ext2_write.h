#ifndef ZENKU_EXT2_WRITE_H
#define ZENKU_EXT2_WRITE_H

#include <ext2.h>

/*
 * Kernel-only ext2 writer. The reader in fs/ext2.c stays free of allocation
 * logic so the BIOS stage3 and the UEFI loader can keep sharing it.
 *
 * All operations are write-through: data blocks, then inode metadata, then
 * bitmaps, then the superblock, followed by a device flush. There is no
 * journal, so a power loss can leave a partially updated filesystem.
 */

/* Timestamp source. The kernel installs an RTC-backed implementation; host
 * tests can leave it unset, in which case zero is used. */
typedef uint32_t (*ext2_time_fn)(void);
void ext2_set_time_source(ext2_time_fn fn);

/* Create an empty regular file. Returns 0 and optionally the inode. */
int ext2_wcreate(struct ext2_fs *fs, const char *path, uint16_t mode,
                 uint32_t *ino_out);

/* Write at `offset`, extending the file and zero-filling new blocks. */
int64_t ext2_wwrite(struct ext2_fs *fs, uint32_t ino, uint64_t offset,
                    const void *buf, uint64_t len);

/* Change the file size. Extending creates a sparse tail. */
int ext2_wtruncate(struct ext2_fs *fs, uint32_t ino, uint64_t size);

/* Remove a regular file (or a directory with ext2_wrmdir). */
int ext2_wunlink(struct ext2_fs *fs, const char *path);

/* Create a directory, including "." and ".." entries. */
int ext2_wmkdir(struct ext2_fs *fs, const char *path, uint16_t mode);

/* Remove an empty directory. */
int ext2_wrmdir(struct ext2_fs *fs, const char *path);

/* Flush the underlying device. */
int ext2_wsync(struct ext2_fs *fs);

#endif /* ZENKU_EXT2_WRITE_H */
