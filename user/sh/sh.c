#include <zenku.h>

#define LINE_MAX 128
#define ARG_MAX  16

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

static int same(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a++ != *b++) {
            return 0;
        }
    }
    return *a == *b;
}

/* Read one terminal line. The kernel line discipline already handled echo
 * and editing, so the ring contains completed lines only. */
static int read_line(char *buf, size_t cap)
{
    size_t n = 0;

    for (;;) {
        char c;
        long rc = sys_read(0, &c, 1);

        if (rc < 0) {
            if (rc == -11) {
                sys_yield();
                continue;
            }
            return -1;
        }
        if (rc == 0) {
            if (n == 0) {
                return 0;
            }
            break;
        }
        if (c == '\n') {
            break;
        }
        if (c == '\r') {
            continue;
        }
        if (n + 1 < cap) {
            buf[n++] = c;
        }
    }
    buf[n] = '\0';
    return (int)n + 1;
}

static int split(char *line, char *argv[], int max)
{
    int argc = 0;
    char *p = line;

    while (*p) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (!*p) {
            break;
        }
        if (argc < max) {
            argv[argc++] = p;
        }
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }
        if (*p) {
            *p++ = '\0';
        }
    }
    if (argc < max) {
        argv[argc] = 0;
    } else {
        argv[max - 1] = 0;
    }
    return argc;
}

static void run_external(char *argv[])
{
    char path[64];
    size_t len = text_len(argv[0]);
    int status = -1;
    long pid;

    if (argv[0][0] == '/') {
        if (len >= sizeof(path)) {
            print("sh: path too long\n");
            return;
        }
        for (size_t i = 0; i <= len; i++) {
            path[i] = argv[0][i];
        }
    } else {
        static const char prefix[] = "/bin/";

        if (len + sizeof(prefix) >= sizeof(path)) {
            print("sh: name too long\n");
            return;
        }
        for (size_t i = 0; i < sizeof(prefix) - 1; i++) {
            path[i] = prefix[i];
        }
        for (size_t i = 0; i <= len; i++) {
            path[sizeof(prefix) - 1 + i] = argv[0][i];
        }
    }

    pid = sys_fork();
    if (pid == 0) {
        sys_execve(path, argv, 0);
        print("sh: cannot execute ");
        print(path);
        print("\n");
        sys_exit(127);
    }
    if (pid < 0) {
        print("sh: fork failed\n");
        return;
    }
    if (sys_wait4(pid, &status, 0) != pid) {
        print("sh: wait failed\n");
    }
}

int main(long argc, char **argv)
{
    char line[LINE_MAX];
    char *args[ARG_MAX];

    (void)argc;
    (void)argv;

    for (;;) {
        int n;
        int count;

        print("sh> ");
        n = read_line(line, sizeof(line));
        if (n <= 0) {
            print("\n");
            break;
        }

        count = split(line, args, ARG_MAX);
        if (count == 0) {
            continue;
        }

        if (same(args[0], "exit")) {
            break;
        }
        if (same(args[0], "help")) {
            print("shell: builtins: exit, help; "
                  "external commands live in /bin\n");
            continue;
        }

        run_external(args);
    }

    return 0;
}
