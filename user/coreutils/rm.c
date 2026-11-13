#include <zenku.h>

static void print(const char *s)
{
    size_t n = 0;

    while (s[n]) {
        n++;
    }
    sys_write(1, s, n);
}

static int remove_one(const char *path)
{
    if (sys_unlink(path) == 0) {
        return 0;
    }
    if (sys_rmdir(path) == 0) {
        return 0;
    }
    return -1;
}

int main(long argc, char **argv)
{
    int failed = 0;

    if (argc < 2) {
        print("usage: rm path...\n");
        return 1;
    }
    for (long i = 1; i < argc; i++) {
        const char *path = argv[i];

        if (path[0] == '-' && path[1] != '\0' && path[2] == '\0' &&
            path[1] == 'f') {
            continue;
        }
        if (remove_one(path) != 0) {
            print("rm: cannot remove ");
            print(path);
            print("\n");
            failed = 1;
        }
    }
    return failed;
}
