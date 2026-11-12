#include <zenku.h>

#define BUF_SIZE 512

static size_t text_len(const char *s)
{
    size_t n = 0;

    while (s[n]) {
        n++;
    }
    return n;
}

static void print(const char *s)
{
    sys_write(1, s, text_len(s));
}

static void copy_file(const char *path)
{
    char buf[BUF_SIZE];
    long fd;
    long n;

    fd = sys_open(path, O_RDONLY);
    if (fd < 0) {
        print("cat: cannot open ");
        print(path);
        print("\n");
        return;
    }
    while ((n = sys_read((int)fd, buf, sizeof(buf))) > 0) {
        sys_write(1, buf, (size_t)n);
    }
    sys_close((int)fd);
}

int main(long argc, char **argv)
{
    if (argc < 2) {
        char buf[BUF_SIZE];
        long n;

        while ((n = sys_read(0, buf, sizeof(buf))) > 0) {
            sys_write(1, buf, (size_t)n);
        }
        return 0;
    }

    for (long i = 1; i < argc; i++) {
        copy_file(argv[i]);
    }
    return 0;
}
