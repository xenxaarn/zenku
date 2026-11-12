#include <zenku.h>

#define MAX_PROCS 16

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

static void print_padded(long value, int width)
{
    char buf[24];
    int n = 0;
    unsigned long v;

    if (value < 0) {
        v = (unsigned long)(-value);
    } else {
        v = (unsigned long)value;
    }
    do {
        buf[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n < width) {
        buf[n++] = ' ';
    }
    if (value < 0) {
        buf[n++] = '-';
    }
    while (n) {
        sys_write(1, &buf[--n], 1);
    }
}

int main(long argc, char **argv)
{
    static const char *const states[] = {
        "free", "ready", "run", "block", "zombie",
    };
    struct zenku_procinfo procs[MAX_PROCS];
    long count;

    (void)argc;
    (void)argv;

    print("PID PPID STATE NAME\n");
    count = sys_getprocs(procs, MAX_PROCS);
    if (count < 0) {
        print("ps: cannot read process table\n");
        return 1;
    }
    for (long i = 0; i < count; i++) {
        print_padded(procs[i].pid, 4);
        sys_write(1, " ", 1);
        print_padded(procs[i].ppid, 5);
        sys_write(1, " ", 1);
        if (procs[i].state >= 0 &&
            (unsigned long)procs[i].state < sizeof(states) / sizeof(states[0])) {
            const char *s = states[procs[i].state];

            sys_write(1, s, text_len(s));
            for (size_t n = text_len(s); n < 6; n++) {
                sys_write(1, " ", 1);
            }
        } else {
            print("?     ");
        }
        print(procs[i].name);
        print("\n");
    }
    return 0;
}
