#include <zenku.h>

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

int main(long argc, char **argv)
{
    unsigned long seconds = 0;

    if (argc != 2) {
        print("usage: sleep SECONDS\n");
        return 1;
    }
    for (const char *p = argv[1]; *p; p++) {
        if (*p < '0' || *p > '9') {
            print("sleep: invalid interval\n");
            return 1;
        }
        seconds = seconds * 10 + (unsigned long)(*p - '0');
    }

    sys_sleep_ms(seconds * 1000);
    return 0;
}
