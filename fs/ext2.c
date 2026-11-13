#include <ext2.h>
#include <string.h>

#define EXT2_RO_COMPAT_LARGE_FILE 0x0002

struct ext2_superblock {
    uint32_t inodes_count;
    uint32_t blocks_count;
    uint32_t r_blocks_count;
    uint32_t free_blocks_count;
    uint32_t free_inodes_count;
    uint32_t first_data_block;
    uint32_t log_block_size;
    uint32_t log_frag_size;
    uint32_t blocks_per_group;
    uint32_t frags_per_group;
    uint32_t inodes_per_group;
    uint32_t mtime;
    uint32_t wtime;
    uint16_t mnt_count;
    uint16_t max_mnt_count;
    uint16_t magic;
    uint16_t state;
    uint16_t errors;
    uint16_t minor_rev_level;
    uint32_t lastcheck;
    uint32_t checkinterval;
    uint32_t creator_os;
    uint32_t rev_level;
    uint16_t def_resuid;
    uint16_t def_resgid;
    uint32_t first_ino;
    uint16_t inode_size;
    uint16_t block_group_nr;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
    uint8_t uuid[16];
    char volume_name[16];
} PACKED;

static uint16_t le16(const uint8_t *p)
{
    uint16_t v;

    memcpy(&v, p, sizeof(v));
    return v;
}

static uint32_t le32(const uint8_t *p)
{
    uint32_t v;

    memcpy(&v, p, sizeof(v));
    return v;
}

static int read_fs_block(struct ext2_fs *fs, uint64_t block, void *buf)
{
    uint32_t ratio = fs->block_size / fs->dev_block_size;

    if (block >= fs->blocks_count) {
        return -1;
    }
    return bdev_read(fs->dev, block * ratio, buf, ratio);
}

int ext2_mount(struct ext2_fs *fs, struct bdev *dev)
{
    uint8_t sb_buf[1024];
    const struct ext2_superblock *sb = (const void *)sb_buf;
    uint32_t sb_blocks;
    uint32_t per_block;

    if (!dev || dev->block_size == 0 || dev->block_size > 4096) {
        return -1;
    }
    if (!(1024 % dev->block_size)) {
        sb_blocks = 1024 / dev->block_size;
    } else {
        return -1;
    }

    memset(fs, 0, sizeof(*fs));
    fs->dev = dev;
    fs->dev_block_size = dev->block_size;

    /* The superblock always starts at byte offset 1024. */
    if (bdev_read(dev, 1024 / dev->block_size, sb_buf, sb_blocks) != 0) {
        return -1;
    }

    if (sb->magic != EXT2_MAGIC) {
        return -1;
    }
    if (sb->log_block_size > 2) {
        return -1;
    }
    if (!sb->blocks_per_group || !sb->inodes_per_group) {
        return -1;
    }
    if (sb->feature_incompat & ~EXT2_SUPPORTED_INCOMPAT) {
        return -1;
    }

    fs->block_size = 1024u << sb->log_block_size;
    fs->blocks_count = sb->blocks_count;
    fs->inodes_count = sb->inodes_count;
    fs->first_data_block = sb->first_data_block;
    fs->blocks_per_group = sb->blocks_per_group;
    fs->inodes_per_group = sb->inodes_per_group;
    fs->inode_size = sb->inode_size ? sb->inode_size : 128;
    fs->feature_compat = sb->feature_compat;
    fs->feature_incompat = sb->feature_incompat;
    fs->feature_ro_compat = sb->feature_ro_compat;

    if (fs->inode_size < 128 || fs->inode_size > fs->block_size) {
        return -1;
    }
    if (fs->blocks_count <= fs->first_data_block) {
        return -1;
    }

    fs->group_count = (fs->blocks_count - fs->first_data_block +
                       fs->blocks_per_group - 1) / fs->blocks_per_group;
    if (fs->group_count == 0 || fs->group_count > EXT2_MAX_GROUPS) {
        return -1;
    }

    /* Group descriptors start in the block after the superblock block. */
    per_block = fs->block_size / sizeof(struct ext2_group_desc);
    for (uint32_t i = 0; i < fs->group_count; i++) {
        uint64_t gd_block = (uint64_t)fs->first_data_block + 1 +
                            i / per_block;

        if (read_fs_block(fs, gd_block, fs->ind_buf) != 0) {
            return -1;
        }
        memcpy(&fs->groups[i],
               fs->ind_buf + (i % per_block) * sizeof(struct ext2_group_desc),
               sizeof(struct ext2_group_desc));

        if (fs->groups[i].inode_table >= fs->blocks_count) {
            return -1;
        }
    }

    return 0;
}

int ext2_read_inode(struct ext2_fs *fs, uint32_t ino, struct ext2_inode *out)
{
    uint32_t group;
    uint32_t index;
    uint64_t offset;
    uint32_t block;
    uint32_t block_off;
    uint32_t copy;

    if (ino == 0 || ino > fs->inodes_count) {
        return -1;
    }

    group = (ino - 1) / fs->inodes_per_group;
    index = (ino - 1) % fs->inodes_per_group;
    if (group >= fs->group_count) {
        return -1;
    }

    offset = (uint64_t)fs->groups[group].inode_table * fs->block_size +
             (uint64_t)index * fs->inode_size;
    block = (uint32_t)(offset / fs->block_size);
    block_off = (uint32_t)(offset % fs->block_size);

    if (block_off + fs->inode_size > fs->block_size) {
        return -1;
    }
    if (read_fs_block(fs, block, fs->ind_buf) != 0) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    copy = fs->inode_size < sizeof(*out) ? fs->inode_size : sizeof(*out);
    memcpy(out, fs->ind_buf + block_off, copy);
    return 0;
}

uint64_t ext2_inode_size(const struct ext2_fs *fs,
                         const struct ext2_inode *inode)
{
    uint64_t size = inode->size;

    if ((fs->feature_ro_compat & EXT2_RO_COMPAT_LARGE_FILE) &&
        !ext2_is_dir(inode)) {
        size |= (uint64_t)inode->size_high << 32;
    }
    return size;
}

static int bmap_indirect(struct ext2_fs *fs, uint32_t block, uint32_t index,
                         uint32_t *out)
{
    if (block == 0) {
        *out = 0;
        return 0;
    }
    if (read_fs_block(fs, block, fs->ind_buf) != 0) {
        return -1;
    }
    *out = le32(fs->ind_buf + (uint64_t)index * 4);
    return 0;
}

static int bmap_double(struct ext2_fs *fs, uint32_t block, uint32_t index,
                       uint32_t per_block, uint32_t *out)
{
    uint32_t mid;

    if (bmap_indirect(fs, block, index / per_block, &mid) != 0) {
        return -1;
    }
    return bmap_indirect(fs, mid, index % per_block, out);
}

static int bmap_triple(struct ext2_fs *fs, uint32_t block, uint32_t index,
                       uint32_t per_block, uint32_t *out)
{
    uint32_t level1;

    if (bmap_indirect(fs, block, index / per_block, &level1) != 0) {
        return -1;
    }
    return bmap_double(fs, level1, index % per_block, per_block, out);
}

static int ext2_bmap(struct ext2_fs *fs, const struct ext2_inode *inode,
                     uint64_t logical, uint32_t *out)
{
    uint32_t per_block = fs->block_size / 4;

    if (logical < 12) {
        *out = inode->block[logical];
        return 0;
    }
    logical -= 12;

    if (logical < per_block) {
        return bmap_indirect(fs, inode->block[12], (uint32_t)logical, out);
    }
    logical -= per_block;

    if (logical < (uint64_t)per_block * per_block) {
        return bmap_double(fs, inode->block[13], (uint32_t)logical,
                           per_block, out);
    }
    logical -= (uint64_t)per_block * per_block;

    if (logical < (uint64_t)per_block * per_block * per_block) {
        return bmap_triple(fs, inode->block[14], (uint32_t)logical,
                           per_block, out);
    }
    return -1;
}

int64_t ext2_read(struct ext2_fs *fs, const struct ext2_inode *inode,
                  uint64_t offset, void *buf, uint64_t len)
{
    uint8_t *dst = buf;
    uint64_t size = ext2_inode_size(fs, inode);
    uint64_t done = 0;

    if (offset >= size) {
        return 0;
    }
    if (len > size - offset) {
        len = size - offset;
    }

    while (done < len) {
        uint64_t position = offset + done;
        uint32_t block_off = (uint32_t)(position % fs->block_size);
        uint32_t chunk = fs->block_size - block_off;
        uint32_t physical;

        if (chunk > len - done) {
            chunk = (uint32_t)(len - done);
        }
        if (ext2_bmap(fs, inode, position / fs->block_size, &physical) != 0) {
            return -1;
        }
        if (physical == 0) {
            memset(dst + done, 0, chunk);
        } else {
            if (read_fs_block(fs, physical, fs->data_buf) != 0) {
                return -1;
            }
            memcpy(dst + done, fs->data_buf + block_off, chunk);
        }
        done += chunk;
    }

    return (int64_t)done;
}

static int dir_lookup(struct ext2_fs *fs, uint32_t dir_ino, const char *name,
                      uint32_t name_len, uint32_t *out)
{
    struct ext2_inode dir;
    uint64_t size;

    if (ext2_read_inode(fs, dir_ino, &dir) != 0 || !ext2_is_dir(&dir)) {
        return -1;
    }

    size = ext2_inode_size(fs, &dir);
    for (uint64_t offset = 0; offset < size; offset += fs->block_size) {
        uint32_t physical;
        uint32_t pos = 0;

        if (ext2_bmap(fs, &dir, offset / fs->block_size, &physical) != 0) {
            return -1;
        }
        if (physical == 0) {
            continue;
        }
        if (read_fs_block(fs, physical, fs->data_buf) != 0) {
            return -1;
        }

        while (pos + 8 <= fs->block_size) {
            const uint8_t *rec = fs->data_buf + pos;
            uint32_t ino = le32(rec);
            uint16_t rec_len = le16(rec + 4);
            uint8_t rec_name_len = rec[6];

            if (rec_len < 8 || pos + rec_len > fs->block_size) {
                break;
            }
            if (ino != 0 && rec_name_len == name_len &&
                memcmp(rec + 8, name, name_len) == 0) {
                *out = ino;
                return 0;
            }
            pos += rec_len;
        }
    }

    return -1;
}

int ext2_iterate(struct ext2_fs *fs, uint32_t dir_ino, ext2_dir_cb cb,
                 void *ctx)
{
    struct ext2_inode dir;
    uint64_t size;

    if (!cb || ext2_read_inode(fs, dir_ino, &dir) != 0 || !ext2_is_dir(&dir)) {
        return -1;
    }

    size = ext2_inode_size(fs, &dir);
    for (uint64_t offset = 0; offset < size; offset += fs->block_size) {
        uint32_t physical;
        uint32_t pos = 0;

        if (ext2_bmap(fs, &dir, offset / fs->block_size, &physical) != 0) {
            return -1;
        }
        if (physical == 0) {
            continue;
        }
        if (read_fs_block(fs, physical, fs->data_buf) != 0) {
            return -1;
        }

        while (pos + 8 <= fs->block_size) {
            const uint8_t *rec = fs->data_buf + pos;
            uint32_t ino = le32(rec);
            uint16_t rec_len = le16(rec + 4);
            uint8_t name_len = rec[6];
            uint8_t type = rec[7];

            if (rec_len < 8 || pos + rec_len > fs->block_size ||
                8 + name_len > rec_len) {
                /* A directory block must be fully covered by a chain of
                 * valid records. Treat anything else as corruption rather
                 * than silently returning a partial listing. */
                return -1;
            }
            if (ino != 0 && name_len > 0) {
                char name[256];
                int rc;

                memcpy(name, rec + 8, name_len);
                name[name_len] = '\0';

                rc = cb(name, name_len, ino, type, ctx);
                if (rc != 0) {
                    return rc;
                }
            }
            pos += rec_len;
        }
    }

    return 0;
}

int ext2_lookup(struct ext2_fs *fs, const char *path, uint32_t *ino_out)
{
    uint32_t ino = EXT2_ROOT_INO;
    const char *p = path;

    while (*p == '/') {
        p++;
    }

    while (*p) {
        const char *start = p;
        uint32_t len;

        while (*p && *p != '/') {
            p++;
        }
        len = (uint32_t)(p - start);

        if (len == 0) {
            break;
        }
        if (len == 1 && start[0] == '.') {
            /* current directory */
        } else if (len == 2 && start[0] == '.' && start[1] == '.') {
            /* Resolve ".." through the directory's own entry so it works at
             * any depth, not just at the root. */
            uint32_t parent;

            if (dir_lookup(fs, ino, "..", 2, &parent) != 0) {
                return -1;
            }
            ino = parent;
        } else {
            uint32_t next;

            if (dir_lookup(fs, ino, start, len, &next) != 0) {
                return -1;
            }
            ino = next;
        }

        while (*p == '/') {
            p++;
        }
    }

    *ino_out = ino;
    return 0;
}
