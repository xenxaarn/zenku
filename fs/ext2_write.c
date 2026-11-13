#include <errno.h>
#include <ext2.h>
#include <ext2_write.h>
#include <string.h>

#define S_IFREG_ 0100000
#define S_IFDIR_ 0040000
#define EXT2_PATH_MAX 256

struct dir_record {
    uint32_t ino;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t type;
};

static ext2_time_fn g_time_fn;

/* Single-threaded kernel scratch buffers for nested indirect traversal. */
static uint8_t g_tmp[2][4096];

void ext2_set_time_source(ext2_time_fn fn)
{
    g_time_fn = fn;
}

static uint32_t now_seconds(void)
{
    return g_time_fn ? g_time_fn() : 0;
}

static uint16_t get16(const uint8_t *p)
{
    uint16_t v;

    memcpy(&v, p, sizeof(v));
    return v;
}

static void put16(uint8_t *p, uint16_t v)
{
    memcpy(p, &v, sizeof(v));
}

static uint32_t get32(const uint8_t *p)
{
    uint32_t v;

    memcpy(&v, p, sizeof(v));
    return v;
}

static void put32(uint8_t *p, uint32_t v)
{
    memcpy(p, &v, sizeof(v));
}

static int read_fs_block(struct ext2_fs *fs, uint64_t block, void *buf)
{
    uint32_t ratio = fs->block_size / fs->dev_block_size;

    if (block >= fs->blocks_count) {
        return -EIO;
    }
    return bdev_read(fs->dev, block * ratio, buf, ratio);
}

static int write_fs_block(struct ext2_fs *fs, uint64_t block, const void *buf)
{
    uint32_t ratio = fs->block_size / fs->dev_block_size;

    if (block >= fs->blocks_count) {
        return -EIO;
    }
    return bdev_write(fs->dev, block * ratio, buf, ratio);
}

static int write_inode(struct ext2_fs *fs, uint32_t ino,
                       const struct ext2_inode *inode)
{
    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;
    uint64_t offset = (uint64_t)fs->groups[group].inode_table *
                      fs->block_size + (uint64_t)index * fs->inode_size;
    uint32_t block = (uint32_t)(offset / fs->block_size);
    uint32_t block_off = (uint32_t)(offset % fs->block_size);
    uint32_t copy = fs->inode_size < sizeof(*inode) ?
                    fs->inode_size : sizeof(*inode);

    if (group >= fs->group_count ||
        block_off + fs->inode_size > fs->block_size) {
        return -EIO;
    }
    if (read_fs_block(fs, block, fs->ind_buf) != 0) {
        return -EIO;
    }
    memset(fs->ind_buf + block_off, 0, fs->inode_size);
    memcpy(fs->ind_buf + block_off, inode, copy);
    if (write_fs_block(fs, block, fs->ind_buf) != 0) {
        return -EIO;
    }
    return 0;
}

static int write_group_desc(struct ext2_fs *fs, uint32_t group)
{
    uint32_t per_block = fs->block_size / sizeof(struct ext2_group_desc);
    uint64_t block = (uint64_t)fs->first_data_block + 1 + group / per_block;
    uint32_t off = (group % per_block) * sizeof(struct ext2_group_desc);

    if (read_fs_block(fs, block, g_tmp[0]) != 0) {
        return -EIO;
    }
    memcpy(g_tmp[0] + off, &fs->groups[group],
           sizeof(struct ext2_group_desc));
    if (write_fs_block(fs, block, g_tmp[0]) != 0) {
        return -EIO;
    }
    return 0;
}

static int write_super(struct ext2_fs *fs)
{
    uint32_t block = 1024 / fs->block_size;
    uint32_t off = 1024 % fs->block_size;
    struct ext2_superblock *sb;

    if (read_fs_block(fs, block, g_tmp[0]) != 0) {
        return -EIO;
    }
    sb = (struct ext2_superblock *)(void *)(g_tmp[0] + off);
    sb->free_blocks_count = fs->free_blocks_count;
    sb->free_inodes_count = fs->free_inodes_count;
    sb->wtime = now_seconds();
    if (write_fs_block(fs, block, g_tmp[0]) != 0) {
        return -EIO;
    }
    return 0;
}

static int alloc_block(struct ext2_fs *fs, uint32_t *out)
{
    for (uint32_t g = 0; g < fs->group_count; g++) {
        uint32_t first = fs->first_data_block + g * fs->blocks_per_group;
        uint32_t count = fs->blocks_per_group;
        uint32_t bits;

        if (first >= fs->blocks_count) {
            continue;
        }
        if (count > fs->blocks_count - first) {
            count = fs->blocks_count - first;
        }
        if (fs->groups[g].free_blocks_count == 0) {
            continue;
        }
        if (read_fs_block(fs, fs->groups[g].block_bitmap, g_tmp[0]) != 0) {
            return -EIO;
        }
        for (bits = 0; bits < count; bits++) {
            uint8_t mask = (uint8_t)(1u << (bits % 8));

            if (g_tmp[0][bits / 8] & mask) {
                continue;
            }
            g_tmp[0][bits / 8] |= mask;
            if (write_fs_block(fs, fs->groups[g].block_bitmap,
                               g_tmp[0]) != 0) {
                return -EIO;
            }
            fs->groups[g].free_blocks_count--;
            fs->free_blocks_count--;
            if (write_group_desc(fs, g) != 0) {
                return -EIO;
            }
            *out = first + bits;
            return 0;
        }
    }
    return -ENOSPC;
}

static int alloc_inode(struct ext2_fs *fs, bool dir, uint32_t *out)
{
    for (uint32_t g = 0; g < fs->group_count; g++) {
        uint32_t first = g * fs->inodes_per_group + 1;
        uint32_t count = fs->inodes_per_group;

        if (first > fs->inodes_count) {
            continue;
        }
        if (count > fs->inodes_count - first + 1) {
            count = fs->inodes_count - first + 1;
        }
        if (fs->groups[g].free_inodes_count == 0) {
            continue;
        }
        if (read_fs_block(fs, fs->groups[g].inode_bitmap, g_tmp[0]) != 0) {
            return -EIO;
        }
        for (uint32_t bits = 0; bits < count; bits++) {
            uint8_t mask = (uint8_t)(1u << (bits % 8));

            if (g_tmp[0][bits / 8] & mask) {
                continue;
            }
            g_tmp[0][bits / 8] |= mask;
            if (write_fs_block(fs, fs->groups[g].inode_bitmap,
                               g_tmp[0]) != 0) {
                return -EIO;
            }
            fs->groups[g].free_inodes_count--;
            fs->free_inodes_count--;
            if (dir) {
                fs->groups[g].used_dirs_count++;
            }
            if (write_group_desc(fs, g) != 0) {
                return -EIO;
            }
            *out = first + bits;
            return 0;
        }
    }
    return -ENOSPC;
}

static int free_block(struct ext2_fs *fs, uint32_t block)
{
    uint32_t g;
    uint32_t first;
    uint32_t bit;

    if (block < fs->first_data_block || block >= fs->blocks_count) {
        return -EINVAL;
    }
    g = (block - fs->first_data_block) / fs->blocks_per_group;
    first = fs->first_data_block + g * fs->blocks_per_group;
    bit = block - first;
    if (g >= fs->group_count) {
        return -EINVAL;
    }
    if (read_fs_block(fs, fs->groups[g].block_bitmap, g_tmp[0]) != 0) {
        return -EIO;
    }
    g_tmp[0][bit / 8] &= (uint8_t)~(1u << (bit % 8));
    if (write_fs_block(fs, fs->groups[g].block_bitmap, g_tmp[0]) != 0) {
        return -EIO;
    }
    fs->groups[g].free_blocks_count++;
    fs->free_blocks_count++;
    return write_group_desc(fs, g);
}

static int free_inode(struct ext2_fs *fs, uint32_t ino, bool dir)
{
    uint32_t g = (ino - 1) / fs->inodes_per_group;
    uint32_t bit = (ino - 1) % fs->inodes_per_group;

    if (g >= fs->group_count) {
        return -EINVAL;
    }
    if (read_fs_block(fs, fs->groups[g].inode_bitmap, g_tmp[0]) != 0) {
        return -EIO;
    }
    g_tmp[0][bit / 8] &= (uint8_t)~(1u << (bit % 8));
    if (write_fs_block(fs, fs->groups[g].inode_bitmap, g_tmp[0]) != 0) {
        return -EIO;
    }
    fs->groups[g].free_inodes_count++;
    if (dir && fs->groups[g].used_dirs_count) {
        fs->groups[g].used_dirs_count--;
    }
    fs->free_inodes_count++;
    return write_group_desc(fs, g);
}

static int split_parent(const char *path, char *parent, size_t parent_cap,
                        char *name, size_t name_cap)
{
    size_t len;
    size_t cut;
    size_t n;

    if (!path || path[0] != '/') {
        return -EINVAL;
    }
    len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        len--;
    }
    cut = len;
    while (cut > 0 && path[cut - 1] != '/') {
        cut--;
    }
    if (cut == len) {
        return -EINVAL;
    }
    n = len - cut;
    if (n == 0 || n >= name_cap || n > 255) {
        return -ENAMETOOLONG;
    }
    if ((n == 1 && path[cut] == '.') ||
        (n == 2 && path[cut] == '.' && path[cut + 1] == '.')) {
        return -EINVAL;
    }
    if (cut == 0) {
        if (parent_cap < 2) {
            return -ENAMETOOLONG;
        }
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        if (cut >= parent_cap) {
            return -ENAMETOOLONG;
        }
        memcpy(parent, path, cut);
        parent[cut] = '\0';
    }
    memcpy(name, path + cut, n);
    name[n] = '\0';
    return 0;
}

/*
 * Find `name` in a directory. On success fills the physical block, record
 * offset, previous record offset (or UINT32_MAX) and inode number.
 */
static int dir_find(struct ext2_fs *fs, uint32_t dir_ino, const char *name,
                    uint32_t name_len, uint32_t *ino_out, uint32_t *block_out,
                    uint32_t *off_out, uint32_t *prev_out)
{
    struct ext2_inode dir;
    uint64_t size;

    if (ext2_read_inode(fs, dir_ino, &dir) != 0 || !ext2_is_dir(&dir)) {
        return -ENOTDIR;
    }
    size = ext2_inode_size(fs, &dir);
    for (uint64_t pos = 0; pos < size; pos += fs->block_size) {
        uint32_t physical;
        uint32_t off = 0;
        uint32_t prev = UINT32_MAX;

        if (ext2_bmap(fs, &dir, pos / fs->block_size, &physical) != 0) {
            return -EIO;
        }
        if (physical == 0) {
            continue;
        }
        if (read_fs_block(fs, physical, fs->data_buf) != 0) {
            return -EIO;
        }
        while (off + 8 <= fs->block_size) {
            uint32_t ino = get32(fs->data_buf + off);
            uint16_t rec = get16(fs->data_buf + off + 4);
            uint8_t nlen = fs->data_buf[off + 6];

            if (rec < 8 || off + rec > fs->block_size) {
                return -EIO;
            }
            if (ino != 0 && nlen == name_len &&
                memcmp(fs->data_buf + off + 8, name, name_len) == 0) {
                *ino_out = ino;
                *block_out = physical;
                *off_out = off;
                *prev_out = prev;
                return 0;
            }
            prev = off;
            off += rec;
        }
    }
    return -ENOENT;
}

static bool dir_is_empty(struct ext2_fs *fs, uint32_t dir_ino)
{
    struct ext2_inode dir;
    uint64_t size;

    if (ext2_read_inode(fs, dir_ino, &dir) != 0) {
        return false;
    }
    size = ext2_inode_size(fs, &dir);
    for (uint64_t pos = 0; pos < size; pos += fs->block_size) {
        uint32_t physical;
        uint32_t off = 0;

        if (ext2_bmap(fs, &dir, pos / fs->block_size, &physical) != 0) {
            return false;
        }
        if (physical == 0) {
            continue;
        }
        if (read_fs_block(fs, physical, fs->data_buf) != 0) {
            return false;
        }
        while (off + 8 <= fs->block_size) {
            uint32_t ino = get32(fs->data_buf + off);
            uint16_t rec = get16(fs->data_buf + off + 4);
            uint8_t nlen = fs->data_buf[off + 6];

            if (rec < 8 || off + rec > fs->block_size) {
                return false;
            }
            if (ino != 0 &&
                !(nlen == 1 && fs->data_buf[off + 8] == '.') &&
                !(nlen == 2 && fs->data_buf[off + 8] == '.' &&
                  fs->data_buf[off + 9] == '.')) {
                return false;
            }
            off += rec;
        }
    }
    return true;
}

static int alloc_and_zero(struct ext2_fs *fs, uint32_t *out)
{
    uint32_t block;
    int rc = alloc_block(fs, &block);

    if (rc < 0) {
        return rc;
    }
    memset(fs->data_buf, 0, fs->block_size);
    if (write_fs_block(fs, block, fs->data_buf) != 0) {
        free_block(fs, block);
        return -EIO;
    }
    *out = block;
    return 0;
}

/* Grow the inode's size by one block and return its physical block. */
static int dir_append_block(struct ext2_fs *fs, struct ext2_inode *dir,
                            uint32_t *out)
{
    uint64_t logical = ext2_inode_size(fs, dir) / fs->block_size;
    uint32_t per = fs->block_size / 4;
    uint32_t block;
    int rc;

    if (logical < 12) {
        if (dir->block[logical] == 0) {
            rc = alloc_and_zero(fs, &block);
            if (rc < 0) {
                return rc;
            }
            dir->block[logical] = block;
            dir->blocks += fs->block_size / 512;
        }
        *out = dir->block[logical];
        goto grow;
    }
    logical -= 12;
    if (logical < per) {
        if (dir->block[12] == 0) {
            rc = alloc_and_zero(fs, &block);
            if (rc < 0) {
                return rc;
            }
            dir->block[12] = block;
            dir->blocks += fs->block_size / 512;
        }
        if (read_fs_block(fs, dir->block[12], fs->ind_buf) != 0) {
            return -EIO;
        }
        if (get32(fs->ind_buf + logical * 4) == 0) {
            rc = alloc_and_zero(fs, &block);
            if (rc < 0) {
                return rc;
            }
            put32(fs->ind_buf + logical * 4, block);
            if (write_fs_block(fs, dir->block[12], fs->ind_buf) != 0) {
                return -EIO;
            }
            dir->blocks += fs->block_size / 512;
        }
        *out = get32(fs->ind_buf + logical * 4);
        goto grow;
    }
    return -ENOSPC;

grow:
    dir->size += fs->block_size;
    return 0;
}

static int dir_add(struct ext2_fs *fs, uint32_t dir_ino, const char *name,
                   uint32_t name_len, uint32_t ino, uint8_t type)
{
    struct ext2_inode dir;
    uint64_t size;
    uint32_t need = (8 + name_len + 3) & ~3u;

    if (ext2_read_inode(fs, dir_ino, &dir) != 0 || !ext2_is_dir(&dir)) {
        return -ENOTDIR;
    }
    size = ext2_inode_size(fs, &dir);

    for (uint64_t pos = 0; pos < size; pos += fs->block_size) {
        uint32_t physical;
        uint32_t off = 0;

        if (ext2_bmap(fs, &dir, pos / fs->block_size, &physical) != 0) {
            return -EIO;
        }
        if (physical == 0) {
            continue;
        }
        if (read_fs_block(fs, physical, fs->data_buf) != 0) {
            return -EIO;
        }
        while (off + 8 <= fs->block_size) {
            uint32_t existing = get32(fs->data_buf + off);
            uint16_t rec = get16(fs->data_buf + off + 4);
            uint8_t nlen = fs->data_buf[off + 6];
            uint32_t used = (8 + nlen + 3) & ~3u;

            if (rec < 8 || off + rec > fs->block_size ||
                used > rec) {
                return -EIO;
            }
            if (rec >= used + need) {
                put16(fs->data_buf + off + 4, (uint16_t)used);
                put32(fs->data_buf + off + used, ino);
                put16(fs->data_buf + off + used + 4,
                      (uint16_t)(rec - used));
                fs->data_buf[off + used + 6] = (uint8_t)name_len;
                fs->data_buf[off + used + 7] = type;
                memcpy(fs->data_buf + off + used + 8, name, name_len);
                if (write_fs_block(fs, physical, fs->data_buf) != 0) {
                    return -EIO;
                }
                (void)existing;
                return 0;
            }
            off += rec;
        }
    }

    {
        uint32_t physical;
        int rc = dir_append_block(fs, &dir, &physical);

        if (rc < 0) {
            return rc;
        }
        memset(fs->data_buf, 0, fs->block_size);
        put32(fs->data_buf, ino);
        put16(fs->data_buf + 4, (uint16_t)fs->block_size);
        fs->data_buf[6] = (uint8_t)name_len;
        fs->data_buf[7] = type;
        memcpy(fs->data_buf + 8, name, name_len);
        if (write_fs_block(fs, physical, fs->data_buf) != 0) {
            return -EIO;
        }
        dir.mtime = now_seconds();
        dir.ctime = dir.mtime;
        return write_inode(fs, dir_ino, &dir);
    }
}

/* Remove `name` from a directory, merging the record into its predecessor. */
static int dir_remove(struct ext2_fs *fs, uint32_t dir_ino, const char *name,
                      uint32_t name_len, uint32_t *ino_out)
{
    uint32_t ino;
    uint32_t physical;
    uint32_t off;
    uint32_t prev;

    if (dir_find(fs, dir_ino, name, name_len, &ino, &physical, &off,
                 &prev) != 0) {
        return -ENOENT;
    }
    if (read_fs_block(fs, physical, fs->data_buf) != 0) {
        return -EIO;
    }
    if (prev != UINT32_MAX) {
        uint16_t prev_rec = get16(fs->data_buf + prev + 4);
        uint16_t rec = get16(fs->data_buf + off + 4);

        put16(fs->data_buf + prev + 4, (uint16_t)(prev_rec + rec));
    } else {
        put32(fs->data_buf + off, 0);
    }
    if (write_fs_block(fs, physical, fs->data_buf) != 0) {
        return -EIO;
    }
    *ino_out = ino;
    return 0;
}

static int clear_indirect_entry(struct ext2_fs *fs, struct ext2_inode *inode,
                                uint64_t logical, uint32_t per)
{
    if (logical < 12) {
        inode->block[logical] = 0;
        return 0;
    }
    logical -= 12;
    if (logical < per) {
        uint32_t block = inode->block[12];

        if (!block) {
            return -EIO;
        }
        if (read_fs_block(fs, block, fs->ind_buf) != 0) {
            return -EIO;
        }
        put32(fs->ind_buf + logical * 4, 0);
        return write_fs_block(fs, block, fs->ind_buf);
    }
    logical -= per;
    if (logical < (uint64_t)per * per) {
        uint32_t l1 = inode->block[13];
        uint32_t l2;

        if (!l1 || read_fs_block(fs, l1, fs->ind_buf) != 0) {
            return -EIO;
        }
        l2 = get32(fs->ind_buf + (logical / per) * 4);
        if (!l2 || read_fs_block(fs, l2, fs->ind_buf) != 0) {
            return -EIO;
        }
        put32(fs->ind_buf + (logical % per) * 4, 0);
        return write_fs_block(fs, l2, fs->ind_buf);
    }
    return -ENOSPC;
}

/* Free data blocks and clear their mappings for [from, to). */
static int free_range(struct ext2_fs *fs, struct ext2_inode *inode,
                      uint64_t from, uint64_t to)
{
    uint32_t per = fs->block_size / 4;
    uint32_t sectors = fs->block_size / 512;

    for (uint64_t logical = from; logical < to; logical++) {
        uint32_t physical;

        if (ext2_bmap(fs, inode, logical, &physical) != 0) {
            return -EIO;
        }
        if (!physical) {
            continue;
        }
        if (free_block(fs, physical) != 0) {
            return -EIO;
        }
        if (inode->blocks >= sectors) {
            inode->blocks -= sectors;
        }
        if (clear_indirect_entry(fs, inode, logical, per) != 0) {
            return -EIO;
        }
    }
    return 0;
}

/* Free an indirect-container block and keep i_blocks in step. */
static void free_container(struct ext2_fs *fs, struct ext2_inode *inode,
                           uint32_t block)
{
    uint32_t sectors = fs->block_size / 512;

    if (free_block(fs, block) == 0 && inode->blocks >= sectors) {
        inode->blocks -= sectors;
    }
}

/* Free indirect container blocks whose data range is entirely removed. */
static void free_containers(struct ext2_fs *fs, struct ext2_inode *inode,
                            uint64_t kept, uint32_t per)
{
    uint64_t direct_end = 12;
    uint64_t single_end = direct_end + per;
    uint64_t double_end = single_end + (uint64_t)per * per;

    if (kept <= direct_end && inode->block[12]) {
        free_container(fs, inode, inode->block[12]);
        inode->block[12] = 0;
    }
    if (kept <= single_end && inode->block[13]) {
        uint32_t l1 = inode->block[13];

        if (read_fs_block(fs, l1, fs->ind_buf) == 0) {
            for (uint32_t i = 0; i < per; i++) {
                uint32_t l2 = get32(fs->ind_buf + i * 4);

                if (l2) {
                    free_container(fs, inode, l2);
                }
            }
        }
        free_container(fs, inode, l1);
        inode->block[13] = 0;
    }
    if (kept <= double_end && inode->block[14]) {
        uint32_t l1 = inode->block[14];

        if (read_fs_block(fs, l1, fs->ind_buf) == 0) {
            for (uint32_t i = 0; i < per; i++) {
                uint32_t l2 = get32(fs->ind_buf + i * 4);

                if (!l2) {
                    continue;
                }
                if (read_fs_block(fs, l2, g_tmp[1]) == 0) {
                    for (uint32_t j = 0; j < per; j++) {
                        uint32_t l3 = get32(g_tmp[1] + j * 4);

                        if (l3) {
                            free_container(fs, inode, l3);
                        }
                    }
                }
                free_container(fs, inode, l2);
            }
        }
        free_container(fs, inode, l1);
        inode->block[14] = 0;
    }
}

static int bmap_alloc(struct ext2_fs *fs, struct ext2_inode *inode,
                      uint64_t logical, uint32_t *out, bool *allocated)
{
    uint32_t per = fs->block_size / 4;
    uint32_t block;
    uint8_t *ind;
    int rc;

    *allocated = false;
    if (logical < 12) {
        if (inode->block[logical] == 0) {
            rc = alloc_and_zero(fs, &block);
            if (rc < 0) {
                return rc;
            }
            inode->block[logical] = block;
            inode->blocks += fs->block_size / 512;
            *allocated = true;
        }
        *out = inode->block[logical];
        return 0;
    }
    logical -= 12;
    if (logical < per) {
        if (inode->block[12] == 0) {
            rc = alloc_and_zero(fs, &block);
            if (rc < 0) {
                return rc;
            }
            inode->block[12] = block;
            inode->blocks += fs->block_size / 512;
        }
        if (read_fs_block(fs, inode->block[12], fs->ind_buf) != 0) {
            return -EIO;
        }
        ind = fs->ind_buf;
        if (get32(ind + logical * 4) == 0) {
            rc = alloc_and_zero(fs, &block);
            if (rc < 0) {
                return rc;
            }
            put32(ind + logical * 4, block);
            if (write_fs_block(fs, inode->block[12], ind) != 0) {
                return -EIO;
            }
            inode->blocks += fs->block_size / 512;
            *allocated = true;
        }
        *out = get32(ind + logical * 4);
        return 0;
    }
    logical -= per;
    if (logical < (uint64_t)per * per) {
        uint32_t l1;
        uint32_t l2;

        if (inode->block[13] == 0) {
            rc = alloc_and_zero(fs, &block);
            if (rc < 0) {
                return rc;
            }
            inode->block[13] = block;
            inode->blocks += fs->block_size / 512;
        }
        l1 = inode->block[13];
        if (read_fs_block(fs, l1, fs->ind_buf) != 0) {
            return -EIO;
        }
        l2 = get32(fs->ind_buf + (logical / per) * 4);
        if (l2 == 0) {
            rc = alloc_and_zero(fs, &block);
            if (rc < 0) {
                return rc;
            }
            l2 = block;
            put32(fs->ind_buf + (logical / per) * 4, l2);
            if (write_fs_block(fs, l1, fs->ind_buf) != 0) {
                return -EIO;
            }
            inode->blocks += fs->block_size / 512;
        }
        if (read_fs_block(fs, l2, fs->ind_buf) != 0) {
            return -EIO;
        }
        if (get32(fs->ind_buf + (logical % per) * 4) == 0) {
            rc = alloc_and_zero(fs, &block);
            if (rc < 0) {
                return rc;
            }
            put32(fs->ind_buf + (logical % per) * 4, block);
            if (write_fs_block(fs, l2, fs->ind_buf) != 0) {
                return -EIO;
            }
            inode->blocks += fs->block_size / 512;
            *allocated = true;
        }
        *out = get32(fs->ind_buf + (logical % per) * 4);
        return 0;
    }
    return -ENOSPC;
}

static int ensure_writable(struct ext2_fs *fs)
{
    if (!fs || !ext2_can_write(fs)) {
        return -EROFS;
    }
    if (!bdev_writable(fs->dev)) {
        return -EROFS;
    }
    return 0;
}

int ext2_wsync(struct ext2_fs *fs)
{
    if (!fs || !fs->dev) {
        return -ENODEV;
    }
    return bdev_flush(fs->dev);
}

int64_t ext2_wwrite(struct ext2_fs *fs, uint32_t ino, uint64_t offset,
                    const void *buf, uint64_t len)
{
    const uint8_t *src = buf;
    struct ext2_inode inode;
    uint64_t size;
    uint64_t done = 0;
    int rc;

    rc = ensure_writable(fs);
    if (rc < 0) {
        return rc;
    }
    if (!buf && len) {
        return -EFAULT;
    }
    if (ext2_read_inode(fs, ino, &inode) != 0) {
        return -EIO;
    }
    if (!ext2_is_file(&inode)) {
        return -EISDIR;
    }
    if (offset + len < offset) {
        return -EINVAL;
    }
    size = ext2_inode_size(fs, &inode);

    while (done < len) {
        uint64_t pos = offset + done;
        uint32_t block_off = (uint32_t)(pos % fs->block_size);
        uint32_t chunk = fs->block_size - block_off;
        uint32_t physical;
        bool allocated;

        if (chunk > len - done) {
            chunk = (uint32_t)(len - done);
        }
        rc = bmap_alloc(fs, &inode, pos / fs->block_size, &physical,
                        &allocated);
        if (rc < 0) {
            break;
        }
        if (read_fs_block(fs, physical, fs->data_buf) != 0) {
            rc = -EIO;
            break;
        }
        memcpy(fs->data_buf + block_off, src + done, chunk);
        if (write_fs_block(fs, physical, fs->data_buf) != 0) {
            rc = -EIO;
            break;
        }
        done += chunk;
        if (pos + chunk > size) {
            size = pos + chunk;
        }
        (void)allocated;
    }

    inode.size = (uint32_t)size;
    inode.size_high = 0;
    inode.mtime = now_seconds();
    inode.ctime = inode.mtime;
    if (write_inode(fs, ino, &inode) != 0) {
        return -EIO;
    }
    if (write_super(fs) != 0) {
        return -EIO;
    }
    ext2_wsync(fs);
    return rc < 0 ? rc : (int64_t)done;
}

int ext2_wtruncate(struct ext2_fs *fs, uint32_t ino, uint64_t size)
{
    struct ext2_inode inode;
    uint64_t old_size;
    uint64_t old_blocks;
    uint64_t new_blocks;
    uint32_t per;
    int rc;

    rc = ensure_writable(fs);
    if (rc < 0) {
        return rc;
    }
    if (ext2_read_inode(fs, ino, &inode) != 0) {
        return -EIO;
    }
    if (ext2_is_dir(&inode)) {
        return -EISDIR;
    }
    old_size = ext2_inode_size(fs, &inode);
    per = fs->block_size / 4;

    if (size == old_size) {
        return 0;
    }
    old_blocks = (old_size + fs->block_size - 1) / fs->block_size;
    new_blocks = (size + fs->block_size - 1) / fs->block_size;

    if (new_blocks < old_blocks) {
        rc = free_range(fs, &inode, new_blocks, old_blocks);
        if (rc < 0) {
            return rc;
        }
        free_containers(fs, &inode, new_blocks, per);
    }

    if (size && size % fs->block_size) {
        uint32_t physical;

        if (ext2_bmap(fs, &inode, size / fs->block_size, &physical) == 0 &&
            physical) {
            if (read_fs_block(fs, physical, fs->data_buf) != 0) {
                return -EIO;
            }
            memset(fs->data_buf + (size % fs->block_size), 0,
                   fs->block_size - (size % fs->block_size));
            if (write_fs_block(fs, physical, fs->data_buf) != 0) {
                return -EIO;
            }
        }
    }

    inode.size = (uint32_t)size;
    inode.size_high = 0;
    inode.mtime = now_seconds();
    inode.ctime = inode.mtime;
    if (write_inode(fs, ino, &inode) != 0) {
        return -EIO;
    }
    if (write_super(fs) != 0) {
        return -EIO;
    }
    ext2_wsync(fs);
    return 0;
}

static void init_inode(struct ext2_inode *inode, uint16_t mode, bool dir)
{
    uint32_t now = now_seconds();

    memset(inode, 0, sizeof(*inode));
    inode->mode = (uint16_t)((dir ? S_IFDIR_ : S_IFREG_) | (mode & 0777));
    inode->size = 0;
    inode->atime = now;
    inode->ctime = now;
    inode->mtime = now;
    inode->links_count = dir ? 2 : 1;
}

int ext2_wcreate(struct ext2_fs *fs, const char *path, uint16_t mode,
                 uint32_t *ino_out)
{
    char parent[EXT2_PATH_MAX];
    char name[256];
    struct ext2_inode pinode;
    struct ext2_inode inode;
    uint32_t parent_ino;
    uint32_t ino;
    int rc;

    rc = ensure_writable(fs);
    if (rc < 0) {
        return rc;
    }
    rc = split_parent(path, parent, sizeof(parent), name, sizeof(name));
    if (rc < 0) {
        return rc;
    }
    if (ext2_lookup(fs, parent, &parent_ino) != 0) {
        return -ENOENT;
    }
    if (ext2_read_inode(fs, parent_ino, &pinode) != 0) {
        return -EIO;
    }
    if (!ext2_is_dir(&pinode)) {
        return -ENOTDIR;
    }
    {
        uint32_t existing;
        uint32_t tmp;

        if (dir_find(fs, parent_ino, name, (uint32_t)strlen(name), &existing,
                     &tmp, &tmp, &tmp) == 0) {
            return -EEXIST;
        }
    }

    rc = alloc_inode(fs, false, &ino);
    if (rc < 0) {
        return rc;
    }
    init_inode(&inode, mode, false);
    if (write_inode(fs, ino, &inode) != 0) {
        free_inode(fs, ino, false);
        return -EIO;
    }
    rc = dir_add(fs, parent_ino, name, (uint32_t)strlen(name), ino,
                 EXT2_FT_REG_FILE);
    if (rc < 0) {
        free_inode(fs, ino, false);
        return rc;
    }
    if (ext2_read_inode(fs, parent_ino, &pinode) != 0) {
        return -EIO;
    }
    pinode.mtime = now_seconds();
    write_inode(fs, parent_ino, &pinode);
    write_super(fs);
    ext2_wsync(fs);
    if (ino_out) {
        *ino_out = ino;
    }
    return 0;
}

int ext2_wmkdir(struct ext2_fs *fs, const char *path, uint16_t mode)
{
    char parent[EXT2_PATH_MAX];
    char name[256];
    struct ext2_inode pinode;
    struct ext2_inode inode;
    uint32_t parent_ino;
    uint32_t ino;
    uint32_t block;
    int rc;

    rc = ensure_writable(fs);
    if (rc < 0) {
        return rc;
    }
    rc = split_parent(path, parent, sizeof(parent), name, sizeof(name));
    if (rc < 0) {
        return rc;
    }
    if (ext2_lookup(fs, parent, &parent_ino) != 0) {
        return -ENOENT;
    }
    if (ext2_read_inode(fs, parent_ino, &pinode) != 0) {
        return -EIO;
    }
    if (!ext2_is_dir(&pinode)) {
        return -ENOTDIR;
    }
    {
        uint32_t existing;
        uint32_t tmp;

        if (dir_find(fs, parent_ino, name, (uint32_t)strlen(name), &existing,
                     &tmp, &tmp, &tmp) == 0) {
            return -EEXIST;
        }
    }

    rc = alloc_inode(fs, true, &ino);
    if (rc < 0) {
        return rc;
    }
    rc = alloc_and_zero(fs, &block);
    if (rc < 0) {
        free_inode(fs, ino, true);
        return rc;
    }
    init_inode(&inode, mode, true);
    inode.size = fs->block_size;
    inode.blocks = fs->block_size / 512;

    if (read_fs_block(fs, block, fs->data_buf) != 0) {
        free_block(fs, block);
        free_inode(fs, ino, true);
        return -EIO;
    }
    memset(fs->data_buf, 0, fs->block_size);
    put32(fs->data_buf, ino);
    put16(fs->data_buf + 4, 12);
    fs->data_buf[6] = 1;
    fs->data_buf[7] = EXT2_FT_DIR;
    fs->data_buf[8] = '.';
    put32(fs->data_buf + 12, parent_ino);
    put16(fs->data_buf + 16, (uint16_t)(fs->block_size - 12));
    fs->data_buf[18] = 2;
    fs->data_buf[19] = EXT2_FT_DIR;
    fs->data_buf[20] = '.';
    fs->data_buf[21] = '.';
    if (write_fs_block(fs, block, fs->data_buf) != 0) {
        free_block(fs, block);
        free_inode(fs, ino, true);
        return -EIO;
    }
    inode.block[0] = block;
    if (write_inode(fs, ino, &inode) != 0) {
        free_block(fs, block);
        free_inode(fs, ino, true);
        return -EIO;
    }
    rc = dir_add(fs, parent_ino, name, (uint32_t)strlen(name), ino,
                 EXT2_FT_DIR);
    if (rc < 0) {
        free_range(fs, &inode, 0, 1);
        free_inode(fs, ino, true);
        return rc;
    }
    if (ext2_read_inode(fs, parent_ino, &pinode) != 0) {
        return -EIO;
    }
    pinode.links_count++;
    pinode.mtime = now_seconds();
    write_inode(fs, parent_ino, &pinode);
    write_super(fs);
    ext2_wsync(fs);
    return 0;
}

int ext2_wunlink(struct ext2_fs *fs, const char *path)
{
    char parent[EXT2_PATH_MAX];
    char name[256];
    struct ext2_inode pinode;
    struct ext2_inode inode;
    uint32_t parent_ino;
    uint32_t ino;
    int rc;

    rc = ensure_writable(fs);
    if (rc < 0) {
        return rc;
    }
    rc = split_parent(path, parent, sizeof(parent), name, sizeof(name));
    if (rc < 0) {
        return rc;
    }
    if (ext2_lookup(fs, parent, &parent_ino) != 0) {
        return -ENOENT;
    }
    if (ext2_read_inode(fs, parent_ino, &pinode) != 0) {
        return -EIO;
    }
    {
        uint32_t block;
        uint32_t off;
        uint32_t prev;

        rc = dir_find(fs, parent_ino, name, (uint32_t)strlen(name), &ino,
                      &block, &off, &prev);
    }
    if (rc < 0) {
        return rc == -ENOENT ? -ENOENT : rc;
    }
    if (ext2_read_inode(fs, ino, &inode) != 0) {
        return -EIO;
    }
    if (ext2_is_dir(&inode)) {
        return -EISDIR;
    }
    rc = dir_remove(fs, parent_ino, name, (uint32_t)strlen(name), &ino);
    if (rc < 0) {
        return rc;
    }
    if (inode.links_count > 1) {
        inode.links_count--;
        write_inode(fs, ino, &inode);
    } else {
        uint64_t blocks = (ext2_inode_size(fs, &inode) + fs->block_size - 1) /
                          fs->block_size;

        free_range(fs, &inode, 0, blocks);
        free_containers(fs, &inode, 0, fs->block_size / 4);
        inode.links_count = 0;
        inode.dtime = now_seconds();
        inode.blocks = 0;
        inode.size = 0;
        write_inode(fs, ino, &inode);
        free_inode(fs, ino, false);
    }
    pinode.mtime = now_seconds();
    write_inode(fs, parent_ino, &pinode);
    write_super(fs);
    ext2_wsync(fs);
    return 0;
}

int ext2_wrmdir(struct ext2_fs *fs, const char *path)
{
    char parent[EXT2_PATH_MAX];
    char name[256];
    struct ext2_inode pinode;
    struct ext2_inode inode;
    uint32_t parent_ino;
    uint32_t ino;
    int rc;

    rc = ensure_writable(fs);
    if (rc < 0) {
        return rc;
    }
    rc = split_parent(path, parent, sizeof(parent), name, sizeof(name));
    if (rc < 0) {
        return rc;
    }
    if (ext2_lookup(fs, parent, &parent_ino) != 0) {
        return -ENOENT;
    }
    if (ext2_read_inode(fs, parent_ino, &pinode) != 0) {
        return -EIO;
    }
    {
        uint32_t tmp;
        uint32_t off;
        uint32_t prev;

        rc = dir_find(fs, parent_ino, name, (uint32_t)strlen(name), &ino,
                      &tmp, &off, &prev);
        if (rc < 0) {
            return rc;
        }
    }
    if (ext2_read_inode(fs, ino, &inode) != 0) {
        return -EIO;
    }
    if (!ext2_is_dir(&inode)) {
        return -ENOTDIR;
    }
    if (!dir_is_empty(fs, ino)) {
        return -ENOTEMPTY;
    }
    rc = dir_remove(fs, parent_ino, name, (uint32_t)strlen(name), &ino);
    if (rc < 0) {
        return rc;
    }
    {
        uint64_t blocks = (ext2_inode_size(fs, &inode) + fs->block_size - 1) /
                          fs->block_size;

        free_range(fs, &inode, 0, blocks);
        free_containers(fs, &inode, 0, fs->block_size / 4);
    }
    inode.links_count = 0;
    inode.dtime = now_seconds();
    inode.blocks = 0;
    inode.size = 0;
    write_inode(fs, ino, &inode);
    free_inode(fs, ino, true);
    if (pinode.links_count > 1) {
        pinode.links_count--;
    }
    pinode.mtime = now_seconds();
    write_inode(fs, parent_ino, &pinode);
    write_super(fs);
    ext2_wsync(fs);
    return 0;
}
