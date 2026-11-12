#include <zenku.h>

#define DEFAULT_LINES 10

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

static int parse_count(const char *s, long *out)
{
    long value = 0;
    int digits = 0;

    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (*s - '0');
        s++;
        digits++;
    }
    if (!digits || *s || value <= 0) {
        return -1;
    }
    *out = value;
    return 0;
}

int main(long argc, char **argv)
{
    long limit = DEFAULT_LINES;
    long lines = 0;

    for (long i = 1; i < argc; i++) {
        if (text_len(argv[i]) == 2 && argv[i][0] == '-' && argv[i][1] == 'n') {
            if (i + 1 >= argc || parse_count(argv[++i], &limit) < 0) {
                print("usage: head [-n LINES]\n");
                return 1;
            }
        } else if (parse_count(argv[i], &limit) < 0) {
            print("usage: head [-n LINES]\n");
            return 1;
        }
    }

    while (lines < limit) {
        char c;
        long rc = sys_read(0, &c, 1);

        if (rc == -11) {
            sys_yield();
            continue;
        }
        if (rc <= 0) {
            break;
        }
        sys_write(1, &c, 1);
        if (c == '\n') {
            lines++;
        }
    }
    return 0;
}
