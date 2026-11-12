#include <zenku.h>

static size_t text_len(const char *s)
{
    size_t n = 0;

    while (s[n]) {
        n++;
    }
    return n;
}

int main(long argc, char **argv)
{
    for (long i = 1; i < argc; i++) {
        if (i > 1) {
            sys_write(1, " ", 1);
        }
        sys_write(1, argv[i], text_len(argv[i]));
    }
    sys_write(1, "\n", 1);
    return 0;
}
