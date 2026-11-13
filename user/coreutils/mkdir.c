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
    int made = 0;
    int failed = 0;

    if (argc < 2) {
        print("usage: mkdir directory...\n");
        return 1;
    }
    for (long i = 1; i < argc; i++) {
        if (sys_mkdir(argv[i], 0755) == 0) {
            made++;
        } else {
            print("mkdir: cannot create ");
            print(argv[i]);
            print("\n");
            failed = 1;
        }
    }
    return failed ? 1 : (made ? 0 : 1);
}
