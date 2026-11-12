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
    const char *path = ".";
    struct zenku_dirent ent;
    unsigned int index = 0;

    if (argc > 1 && argv[1][0] != '-') {
        path = argv[1];
    }

    for (;;) {
        long rc = sys_readdir(path, index++, &ent);

        if (rc == -2) { /* ENOENT: end of directory */
            break;
        }
        if (rc < 0) {
            print("ls: cannot list ");
            print(path);
            print("\n");
            return 1;
        }
        if ((ent.name_len == 1 && ent.name[0] == '.') ||
            (ent.name_len == 2 && ent.name[0] == '.' && ent.name[1] == '.')) {
            continue;
        }
        print(ent.name);
        if (ent.type == 2) { /* EXT2_FT_DIR */
            print("/");
        }
        print("\n");
    }
    return 0;
}
