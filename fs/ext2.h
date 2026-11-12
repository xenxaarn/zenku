#ifndef ZENKU_EXT2_H
#define ZENKU_EXT2_H

#include <bdev.h>
#include <compiler.h>
#include <types.h>

#define EXT2_MAGIC 0xEF53

/* Incompatible features we cannot read. */
#define EXT2_INCOMPAT_COMPRESSION 0x0001
#define EXT2_INCOMPAT_FILETYPE    0x0002
#define EXT2_INCOMPAT_RECOVER     0x0004
#define EXT2_INCOMPAT_JOURNAL_DEV 0x0008
#define EXT2_INCOMPAT_META_BG     0x0010
#define EXT2_INCOMPAT_EXTENTS     0x0040
#define EXT2_INCOMPAT_64BIT       0x0080
#define EXT2_INCOMPAT_MMP         0x0100
#define EXT2_INCOMPAT_FLEX_BG     0x0200
#define EXT2_INCOMPAT_EA_INODE    0x0400
#define EXT2_INCOMPAT_DIRDATA     0x1000
#define EXT2_INCOMPAT_CSUM_SEED   0x2000
#define EXT2_INCOMPAT_LARGEDIR    0x4000
#define EXT2_INCOMPAT_INLINE_DATA 0x8000

#define EXT2_SUPPORTED_INCOMPAT \
    (EXT2_INCOMPAT_FILETYPE | EXT2_INCOMPAT_FLEX_BG)

#define EXT2_ROOT_INO 2

/* Directory entry file types (when the FILETYPE feature is present). */
#define EXT2_FT_UNKNOWN  0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2
#define EXT2_FT_CHRDEV   3
#define EXT2_FT_BLKDEV   4
#define EXT2_FT_FIFO     5
#define EXT2_FT_SOCK     6
#define EXT2_FT_SYMLINK  7

struct ext2_inode {
    uint16_t mode;
    uint16_t uid;
    uint32_t size;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint16_t gid;
    uint16_t links_count;
    uint32_t blocks;
    uint32_t flags;
    uint32_t osd1;
    uint32_t block[15];
    uint32_t generation;
    uint32_t file_acl;
    uint32_t size_high;
    uint32_t faddr;
    uint8_t osd2[12];
} PACKED;

struct ext2_group_desc {
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;
    uint16_t free_blocks_count;
    uint16_t free_inodes_count;
    uint16_t used_dirs_count;
    uint16_t pad;
    uint32_t reserved[3];
} PACKED;

#define EXT2_MAX_GROUPS 64

struct ext2_fs {
    struct bdev *dev;
    uint32_t block_size;        /* filesystem block size in bytes */
    uint32_t dev_block_size;    /* device block size in bytes */
    uint32_t first_data_block;
    uint32_t blocks_count;
    uint32_t inodes_count;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t inode_size;
    uint32_t group_count;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
    struct ext2_group_desc groups[EXT2_MAX_GROUPS];
    uint8_t ind_buf[4096];
    uint8_t data_buf[4096];
};

/* Mount the filesystem found on `dev`. Returns 0 on success. */
int ext2_mount(struct ext2_fs *fs, struct bdev *dev);

/* Read inode `ino` into `out`. Returns 0 on success. */
int ext2_read_inode(struct ext2_fs *fs, uint32_t ino, struct ext2_inode *out);

/* Resolve an absolute path ("/kernel" or "/boot/kernel"). Returns 0 on
 * success and stores the inode number in `ino_out`. */
int ext2_lookup(struct ext2_fs *fs, const char *path, uint32_t *ino_out);

/*
 * Visit every entry in a directory. The callback returns 0 to continue or
 * non-zero to stop; that value is returned from ext2_iterate.
 */
typedef int (*ext2_dir_cb)(const char *name, uint32_t name_len,
                           uint32_t ino, uint8_t type, void *ctx);
int ext2_iterate(struct ext2_fs *fs, uint32_t dir_ino, ext2_dir_cb cb,
                 void *ctx);

/* Read bytes from an inode. Returns the number of bytes copied (which may be
 * less than `len` at end of file), or a negative value on error. */
int64_t ext2_read(struct ext2_fs *fs, const struct ext2_inode *inode,
                  uint64_t offset, void *buf, uint64_t len);

/* Full size of an inode, including the high 32 bits when present. */
uint64_t ext2_inode_size(const struct ext2_fs *fs,
                         const struct ext2_inode *inode);

static inline bool ext2_is_dir(const struct ext2_inode *inode)
{
    return (inode->mode & 0xF000) == 0x4000;
}

static inline bool ext2_is_file(const struct ext2_inode *inode)
{
    return (inode->mode & 0xF000) == 0x8000;
}

#endif /* ZENKU_EXT2_H */
