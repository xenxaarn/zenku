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
    (void)argc;
    (void)argv;
    if (sys_sync() < 0) {
        print("sync: failed\n");
        return 1;
    }
    return 0;
}
