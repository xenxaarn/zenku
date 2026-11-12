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

static int parse_int(const char *s, long *out)
{
    long value = 0;
    int digits = 0;

    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (*s - '0');
        s++;
        digits++;
    }
    if (!digits || *s) {
        return -1;
    }
    *out = value;
    return 0;
}

int main(long argc, char **argv)
{
    long pid;
    long sig = 15;

    if (argc < 2 || argc > 3) {
        print("usage: kill PID [SIGNAL]\n");
        return 1;
    }
    if (parse_int(argv[1], &pid) < 0 ||
        (argc == 3 && parse_int(argv[2], &sig) < 0)) {
        print("kill: invalid argument\n");
        return 1;
    }

    if (sys_kill(pid, (int)sig) < 0) {
        print("kill: no such process\n");
        return 1;
    }
    return 0;
}
