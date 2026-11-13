#include <zenku.h>

static void print(const char *s)
{
    size_t n = 0;

    while (s[n]) {
        n++;
    }
    sys_write(1, s, n);
}

int main(long argc, char **argv)
{
    int failed = 0;

    if (argc < 2) {
        print("usage: touch file...\n");
        return 1;
    }
    for (long i = 1; i < argc; i++) {
        long fd = sys_open(argv[i], O_WRONLY | O_CREAT);

        if (fd < 0) {
            print("touch: cannot create ");
            print(argv[i]);
            print("\n");
            failed = 1;
            continue;
        }
        sys_close((int)fd);
    }
    return failed;
}
