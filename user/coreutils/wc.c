#include <zenku.h>

#define BUF_SIZE 256

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
           c == '\f';
}

static void print_num(unsigned long value)
{
    char buf[24];
    int n = 0;

    do {
        buf[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n) {
        sys_write(1, &buf[--n], 1);
    }
}

int main(long argc, char **argv)
{
    char buf[BUF_SIZE];
    unsigned long lines = 0;
    unsigned long words = 0;
    unsigned long bytes = 0;
    int in_word = 0;

    (void)argc;
    (void)argv;

    for (;;) {
        long n = sys_read(0, buf, sizeof(buf));

        if (n == -11) { /* EAGAIN: empty pipe or terminal */
            sys_yield();
            continue;
        }
        if (n < 0) {
            break;
        }
        if (n == 0) {
            break;
        }
        for (long i = 0; i < n; i++) {
            char c = buf[i];

            bytes++;
            if (c == '\n') {
                lines++;
            }
            if (is_space(c)) {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }

    print_num(lines);
    sys_write(1, " ", 1);
    print_num(words);
    sys_write(1, " ", 1);
    print_num(bytes);
    sys_write(1, "\n", 1);
    return 0;
}
