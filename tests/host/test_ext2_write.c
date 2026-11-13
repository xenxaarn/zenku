#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <ext2.h>
#include <ext2_write.h>
#include <string.h>

#include "test.h"

struct wr_bdev {
    struct bdev bdev;
    FILE *fp;
};

static int wr_read(struct bdev *dev, uint64_t block, void *buf,
                   uint32_t count)
{
    struct wr_bdev *fb = (struct wr_bdev *)dev->ctx;
    size_t bytes = (size_t)count * dev->block_size;

    if (fseek(fb->fp, (long)(block * dev->block_size), SEEK_SET) != 0) {
        return -EIO;
    }
    if (fread(buf, 1, bytes, fb->fp) != bytes) {
        return -EIO;
    }
    return 0;
}

static int wr_write(struct bdev *dev, uint64_t block, const void *buf,
                    uint32_t count)
{
    struct wr_bdev *fb = (struct wr_bdev *)dev->ctx;
    size_t bytes = (size_t)count * dev->block_size;

    if (fseek(fb->fp, (long)(block * dev->block_size), SEEK_SET) != 0) {
        return -EIO;
    }
    if (fwrite(buf, 1, bytes, fb->fp) != bytes) {
        return -EIO;
    }
    return 0;
}

static int wr_flush(struct bdev *dev)
{
    struct wr_bdev *fb = (struct wr_bdev *)dev->ctx;

    return fflush(fb->fp) == 0 ? 0 : -EIO;
}

static int copy_image(const char *dir, const char *name, char *out,
                      size_t cap)
{
    char src[512];
    unsigned char buf[8192];
    FILE *in;
    FILE *dst;
    size_t n;

    snprintf(src, sizeof(src), "%s/%s", dir, name);
    snprintf(out, cap, "/tmp/zenku-ext2-write.img");
    in = fopen(src, "rb");
    if (!in) {
        return -1;
    }
    dst = fopen(out, "wb");
    if (!dst) {
        fclose(in);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) {
            fclose(in);
            fclose(dst);
            return -1;
        }
    }
    fclose(in);
    fclose(dst);
    return 0;
}

static int open_writable(const char *path, struct wr_bdev *fb)
{
    fb->fp = fopen(path, "r+b");
    if (!fb->fp) {
        return -1;
    }
    fb->bdev.ctx = fb;
    fb->bdev.block_size = 512;
    fb->bdev.name = "host-file";
    fb->bdev.read_only = false;
    if (fseek(fb->fp, 0, SEEK_END) != 0) {
        return -1;
    }
    fb->bdev.block_count = (uint64_t)ftell(fb->fp) / 512;
    fb->bdev.read = wr_read;
    fb->bdev.write = wr_write;
    fb->bdev.flush = wr_flush;
    return 0;
}

static void check_contents(struct ext2_fs *fs, const char *path,
                           const char *expected, size_t len)
{
    struct ext2_inode inode;
    char buf[256];
    uint32_t ino;
    int64_t n;

    CHECK_EQ(ext2_lookup(fs, path, &ino), 0);
    CHECK_EQ(ext2_read_inode(fs, ino, &inode), 0);
    CHECK_EQ(ext2_inode_size(fs, &inode), len);
    memset(buf, 0, sizeof(buf));
    n = ext2_read(fs, &inode, 0, buf, sizeof(buf));
    CHECK_EQ(n, (long long)len);
    CHECK(memcmp(buf, expected, len) == 0);
}

static void test_write_suite(const char *dir)
{
    char path[512];
    struct wr_bdev fb;
    struct ext2_fs fs;
    uint32_t ino;
    uint8_t block[4096];
    char chunk[65536];
    uint64_t written = 0;
    int status;

    CHECK_EQ(copy_image(dir, "ext2-1024.img", path, sizeof(path)), 0);
    CHECK_EQ(open_writable(path, &fb), 0);
    CHECK_EQ(ext2_mount(&fs, &fb.bdev), 0);
    CHECK(fs.label[0] == '\0' || strcmp(fs.label, "ZENKU") == 0);

    /* Create, write, append. */
    CHECK_EQ(ext2_wcreate(&fs, "/newfile", 0644, &ino), 0);
    CHECK_EQ(ext2_wwrite(&fs, ino, 0, "hello", 5), 5);
    CHECK_EQ(ext2_wwrite(&fs, ino, 5, " world", 6), 6);
    check_contents(&fs, "/newfile", "hello world", 11);

    /* Truncate. */
    CHECK_EQ(ext2_wtruncate(&fs, ino, 5), 0);
    check_contents(&fs, "/newfile", "hello", 5);
    CHECK_EQ(ext2_wtruncate(&fs, ino, 2048), 0);
    CHECK_EQ(ext2_wwrite(&fs, ino, 2048, "tail", 4), 4);
    {
        struct ext2_inode inode;

        CHECK_EQ(ext2_read_inode(&fs, ino, &inode), 0);
        CHECK_EQ(ext2_inode_size(&fs, &inode), 2052);
        memset(block, 0xAA, sizeof(block));
        CHECK_EQ(ext2_read(&fs, &inode, 5, block, 10), 10);
        CHECK(block[0] == 0 && block[9] == 0);
        CHECK_EQ(ext2_read(&fs, &inode, 2048, block, 4), 4);
        CHECK(memcmp(block, "tail", 4) == 0);
        /* Block 0 and the sparse block 2 are allocated; block 1 is a hole. */
        CHECK_EQ(inode.blocks, 4);
    }

    /* Directory create/populate/remove. */
    CHECK_EQ(ext2_wmkdir(&fs, "/dir", 0755), 0);
    CHECK_EQ(ext2_wmkdir(&fs, "/dir/sub", 0755), 0);
    CHECK_EQ(ext2_wcreate(&fs, "/dir/sub/file", 0644, NULL), 0);
    CHECK_EQ(ext2_wrmdir(&fs, "/dir"), -ENOTEMPTY);
    CHECK_EQ(ext2_wrmdir(&fs, "/dir/sub"), -ENOTEMPTY);
    CHECK_EQ(ext2_wunlink(&fs, "/dir/sub/file"), 0);
    CHECK_EQ(ext2_wrmdir(&fs, "/dir/sub"), 0);
    CHECK_EQ(ext2_wrmdir(&fs, "/dir"), 0);
    CHECK(ext2_lookup(&fs, "/dir", &ino) != 0);

    /* Many entries force directory block extension. */
    CHECK_EQ(ext2_wmkdir(&fs, "/grow", 0755), 0);
    for (int i = 0; i < 80; i++) {
        char name[128];

        snprintf(name, sizeof(name), "/grow/entry-with-a-long-name-%03d", i);
        CHECK_EQ(ext2_wcreate(&fs, name, 0644, NULL), 0);
    }
    for (int i = 0; i < 80; i++) {
        char name[128];

        snprintf(name, sizeof(name), "/grow/entry-with-a-long-name-%03d", i);
        CHECK_EQ(ext2_wunlink(&fs, name), 0);
    }
    CHECK_EQ(ext2_wrmdir(&fs, "/grow"), 0);

    /* Cross the single-indirect boundary. */
    memset(chunk, 'Q', sizeof(chunk));
    CHECK_EQ(ext2_wcreate(&fs, "/bigwrite", 0644, &ino), 0);
    while (written < 3 * 1024 * 1024) {
        int rc = (int)ext2_wwrite(&fs, ino, written, chunk, sizeof(chunk));

        CHECK_EQ(rc, (long long)sizeof(chunk));
        written += sizeof(chunk);
    }
    {
        struct ext2_inode inode;

        CHECK_EQ(ext2_read_inode(&fs, ino, &inode), 0);
        CHECK_EQ(ext2_inode_size(&fs, &inode), written);
        memset(block, 0, sizeof(block));
        CHECK_EQ(ext2_read(&fs, &inode, written - 16, block, 16), 16);
        for (int i = 0; i < 16; i++) {
            CHECK(block[i] == 'Q');
        }
        /* 3072 data + 13 indirect containers, at two 512-byte sectors each. */
        CHECK_EQ(inode.blocks, (3072 + 13) * 2);
    }

    /* Shrinking back into the single-indirect range drops the double tree. */
    CHECK_EQ(ext2_wtruncate(&fs, ino, 200 * 1024), 0);
    {
        struct ext2_inode inode;

        CHECK_EQ(ext2_read_inode(&fs, ino, &inode), 0);
        CHECK_EQ(ext2_inode_size(&fs, &inode), 200 * 1024);
        /* 200 data blocks plus one single-indirect container. */
        CHECK_EQ(inode.blocks, (200 + 1) * 2);
        memset(block, 0, sizeof(block));
        CHECK_EQ(ext2_read(&fs, &inode, 200 * 1024 - 8, block, 8), 8);
        for (int i = 0; i < 8; i++) {
            CHECK(block[i] == 'Q');
        }
    }
    CHECK_EQ(ext2_wtruncate(&fs, ino, 0), 0);
    {
        struct ext2_inode inode;

        CHECK_EQ(ext2_read_inode(&fs, ino, &inode), 0);
        CHECK_EQ(ext2_inode_size(&fs, &inode), 0);
        CHECK_EQ(inode.blocks, 0);
    }

    CHECK_EQ(ext2_wunlink(&fs, "/newfile"), 0);
    CHECK_EQ(ext2_wunlink(&fs, "/bigwrite"), 0);
    CHECK_EQ(ext2_wsync(&fs), 0);
    fclose(fb.fp);

    {
        char cmd[1024];

        snprintf(cmd, sizeof(cmd), "e2fsck -fn '%s' >/dev/null 2>&1", path);
        status = system(cmd);
    }
    CHECK_EQ(status, 0);
    remove(path);
}

static uint32_t fixed_time(void)
{
    return 1700000000u;
}

void test_ext2_write(const char *image_dir)
{
    ext2_set_time_source(fixed_time);
    test_write_suite(image_dir);
}
