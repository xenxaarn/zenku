#include <stdio.h>
#include <stdlib.h>

#include <ext2.h>
#include <string.h>

#include "test.h"

#define MIB (1024ull * 1024ull)

struct file_bdev {
    struct bdev bdev;
    FILE *fp;
};

static int file_read(struct bdev *dev, uint64_t block, void *buf,
                     uint32_t count)
{
    struct file_bdev *fb = (struct file_bdev *)dev->ctx;
    long offset = (long)(block * dev->block_size);
    size_t bytes = (size_t)count * dev->block_size;

    if (fseek(fb->fp, offset, SEEK_SET) != 0) {
        return -1;
    }
    if (fread(buf, 1, bytes, fb->fp) != bytes) {
        return -1;
    }
    return 0;
}

static int open_image(const char *dir, const char *name, struct file_bdev *fb)
{
    char path[512];

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    fb->fp = fopen(path, "rb");
    if (!fb->fp) {
        fprintf(stderr, "cannot open image %s\n", path);
        return -1;
    }
    fb->bdev.ctx = fb;
    fb->bdev.block_size = 512;
    if (fseek(fb->fp, 0, SEEK_END) != 0) {
        return -1;
    }
    fb->bdev.block_count = (uint64_t)ftell(fb->fp) / 512;
    fb->bdev.read = file_read;
    return 0;
}

static void close_image(struct file_bdev *fb)
{
    if (fb->fp) {
        fclose(fb->fp);
        fb->fp = NULL;
    }
}

static void check_text_file(struct ext2_fs *fs, const char *path,
                            const char *expected)
{
    struct ext2_inode inode;
    char buf[128];
    uint32_t ino;
    int64_t got;

    CHECK_EQ(ext2_lookup(fs, path, &ino), 0);
    CHECK_EQ(ext2_read_inode(fs, ino, &inode), 0);
    memset(buf, 0, sizeof(buf));
    got = ext2_read(fs, &inode, 0, buf, sizeof(buf) - 1);
    CHECK_EQ(got, (long long)strlen(expected));
    CHECK(strcmp(buf, expected) == 0);
}

struct dir_scan {
    int count;
    int saw_first;
    int saw_last;
};

static int count_entry(const char *name, uint32_t name_len, uint32_t ino,
                       uint8_t type, void *ctx)
{
    struct dir_scan *scan = ctx;

    (void)ino;
    (void)type;
    scan->count++;
    if (name_len == 4 && memcmp(name, "f000", 4) == 0) {
        scan->saw_first = 1;
    }
    if (name_len == 4 && memcmp(name, "f199", 4) == 0) {
        scan->saw_last = 1;
    }
    return 0;
}

static void check_big_file(struct ext2_fs *fs, uint64_t expected_size)
{
    static const char pattern[] = "ZZZZ";
    struct ext2_inode inode;
    uint32_t ino;
    uint64_t bs = fs->block_size;
    uint64_t per = bs / 4;
    uint64_t direct_end = 12 * bs;
    uint64_t indirect_end = direct_end + per * bs;
    uint64_t double_end = indirect_end + per * per * bs;
    uint64_t offsets[8];
    size_t samples = 0;
    char buf[1024];

    CHECK_EQ(ext2_lookup(fs, "/big.bin", &ino), 0);
    CHECK_EQ(ext2_read_inode(fs, ino, &inode), 0);
    CHECK_EQ(ext2_inode_size(fs, &inode), expected_size);

    offsets[samples++] = 0;
    offsets[samples++] = bs * 11;
    offsets[samples++] = direct_end;
    offsets[samples++] = direct_end + bs;
    offsets[samples++] = indirect_end;
    offsets[samples++] = indirect_end + bs;
    if (double_end + bs < expected_size) {
        offsets[samples++] = double_end;
        offsets[samples++] = double_end + bs;
    } else if (expected_size > indirect_end + bs) {
        offsets[samples++] = expected_size - sizeof(pattern);
    }

    for (size_t i = 0; i < samples; i++) {
        int64_t got;

        memset(buf, 0, sizeof(buf));
        got = ext2_read(fs, &inode, offsets[i], buf, sizeof(pattern));
        CHECK_EQ(got, (long long)sizeof(pattern));
        CHECK(memcmp(buf, pattern, sizeof(pattern) - 1) == 0);
    }

    /* Unaligned span across block boundaries. */
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ext2_read(fs, &inode, 12345, buf, 777), 777);
    CHECK(memcmp(buf, "ZZZZZZZZ", 8) == 0);

    /* Read past the end returns zero bytes. */
    CHECK_EQ(ext2_read(fs, &inode, expected_size, buf, sizeof(buf)), 0);
    CHECK_EQ(ext2_read(fs, &inode, expected_size + 4096, buf, 8), 0);

    /* Size is clamped at EOF. */
    CHECK_EQ(ext2_read(fs, &inode, expected_size - 4, buf, 64), 4);
}

static void test_one_image(const char *dir, unsigned block_size,
                           uint64_t big_size, int check_indirect)
{
    struct file_bdev fb;
    static struct ext2_fs fs;
    struct dir_scan scan;
    char name[64];

    snprintf(name, sizeof(name), "ext2-%u.img", block_size);
    CHECK_EQ(open_image(dir, name, &fb), 0);
    CHECK_EQ(ext2_mount(&fs, &fb.bdev), 0);
    CHECK_EQ(fs.block_size, block_size);
    CHECK_EQ(fs.group_count > 0, 1);

    check_text_file(&fs, "/hello.txt", "hello ext2\n");
    check_text_file(&fs, "/deep/a/b/c/d/e/f.txt", "deep file\n");
    CHECK(ext2_lookup(&fs, "/does-not-exist", &(uint32_t){0}) != 0);
    CHECK(ext2_lookup(&fs, "/deep/nope/f.txt", &(uint32_t){0}) != 0);

    /* Dot and dot-dot components resolve. */
    check_text_file(&fs, "/deep/./a/b/../b/c/d/e/f.txt", "deep file\n");

    memset(&scan, 0, sizeof(scan));
    CHECK_EQ(ext2_iterate(&fs, EXT2_ROOT_INO, count_entry, &scan) == 0, 1);
    CHECK(scan.count >= 4);

    uint32_t many_ino;
    memset(&scan, 0, sizeof(scan));
    CHECK_EQ(ext2_lookup(&fs, "/many", &many_ino), 0);
    CHECK_EQ(ext2_iterate(&fs, many_ino, count_entry, &scan), 0);
    CHECK_EQ(scan.count, 202); /* ".", "..", and 200 files */
    CHECK_EQ(scan.saw_first, 1);
    CHECK_EQ(scan.saw_last, 1);

    if (check_indirect) {
        check_big_file(&fs, big_size);
    }

    close_image(&fb);
    printf("ext2: %u-byte blocks mounted and read\n", block_size);
}

static void test_rejects(const char *dir)
{
    struct file_bdev fb;
    static struct ext2_fs fs;
    struct dir_scan scan;

    CHECK_EQ(open_image(dir, "bad-magic.img", &fb), 0);
    CHECK(ext2_mount(&fs, &fb.bdev) != 0);
    close_image(&fb);

    CHECK_EQ(open_image(dir, "unsupported-extents.img", &fb), 0);
    CHECK(ext2_mount(&fs, &fb.bdev) != 0);
    close_image(&fb);

    CHECK_EQ(open_image(dir, "bad-rec-len.img", &fb), 0);
    CHECK_EQ(ext2_mount(&fs, &fb.bdev), 0);
    memset(&scan, 0, sizeof(scan));
    CHECK(ext2_iterate(&fs, EXT2_ROOT_INO, count_entry, &scan) != 0);
    close_image(&fb);

    printf("ext2: corrupt images rejected\n");
}

void test_ext2(const char *image_dir)
{
    test_one_image(image_dir, 1024, 66 * MIB, 1);
    test_one_image(image_dir, 2048, 4 * MIB, 1);
    test_one_image(image_dir, 4096, 6 * MIB, 1);
    test_rejects(image_dir);
}
