#include <zenku.h>

#define LINE_MAX    128
#define ARG_MAX     16
#define PIPE_STAGES 4

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

/* Split a line into pipe-separated stage strings in place. */
static int split_pipeline(char *line, char *stages[], int max)
{
    int count = 0;
    char *p = line;

    if (!*p) {
        return 0;
    }
    stages[count++] = p;
    while (*p) {
        if (*p == '|') {
            *p = '\0';
            p++;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (!*p || count >= max) {
                return -1;
            }
            stages[count++] = p;
            continue;
        }
        p++;
    }
    return count;
}

static int build_path(const char *name, char *path, size_t cap)
{
    size_t len = text_len(name);

    if (name[0] == '/') {
        if (len >= cap) {
            return -1;
        }
        for (size_t i = 0; i <= len; i++) {
            path[i] = name[i];
        }
        return 0;
    }

    {
        static const char prefix[] = "/bin/";
        size_t plen = sizeof(prefix) - 1;

        if (plen + len >= cap) {
            return -1;
        }
        for (size_t i = 0; i < plen; i++) {
            path[i] = prefix[i];
        }
        for (size_t i = 0; i <= len; i++) {
            path[plen + i] = name[i];
        }
    }
    return 0;
}

static void exec_command(char *argv[])
{
    char path[64];

    if (build_path(argv[0], path, sizeof(path)) < 0) {
        print("sh: name too long\n");
        sys_exit(127);
    }
    sys_execve(path, argv, 0);
    print("sh: cannot execute ");
    print(path);
    print("\n");
    sys_exit(127);
}

static void wait_for(long pid)
{
    int status = -1;

    if (sys_wait4(pid, &status, 0) != pid) {
        print("sh: wait failed\n");
    }
}

static int apply_redirect(const char *path, int flags, int target)
{
    long fd = sys_open(path, flags);

    if (fd < 0) {
        return -1;
    }
    if (sys_dup2((int)fd, target) < 0) {
        sys_close((int)fd);
        return -1;
    }
    sys_close((int)fd);
    return 0;
}

static int parse_redirects(char *argv[], int *argc, char **in, char **out,
                           int *append)
{
    int kept = 0;

    *in = 0;
    *out = 0;
    *append = 0;
    for (int i = 0; i < *argc; i++) {
        int is_out = same(argv[i], ">");
        int is_append = same(argv[i], ">>");

        if (same(argv[i], "<") || is_out || is_append) {
            char *path = (i + 1 < *argc) ? argv[i + 1] : 0;

            if (!path) {
                print("sh: missing redirection target\n");
                return -1;
            }
            if (same(argv[i], "<")) {
                *in = path;
            } else {
                *out = path;
                *append = is_append;
            }
            i++;
        } else {
            argv[kept++] = argv[i];
        }
    }
    *argc = kept;
    argv[kept] = 0;
    return 0;
}

static void run_pipeline(char *stage_argv[][ARG_MAX], int stage_count,
                         char *stage_in[], char *stage_out[],
                         int stage_append[])
{
    int pipes[PIPE_STAGES - 1][2];
    long pids[PIPE_STAGES];
    int created = 0;

    for (int i = 0; i < stage_count - 1; i++) {
        if (sys_pipe(pipes[i]) < 0) {
            print("sh: cannot create pipe\n");
            stage_count = i + 1;
            break;
        }
        created++;
    }
    if (created < stage_count - 1) {
        stage_count = created + 1;
    }

    for (int i = 0; i < stage_count; i++) {
        long pid = sys_fork();

        if (pid == 0) {
            if (i > 0) {
                sys_dup2(pipes[i - 1][0], 0);
            }
            if (i < stage_count - 1) {
                sys_dup2(pipes[i][1], 1);
            }
            if (stage_in[i] &&
                apply_redirect(stage_in[i], O_RDONLY, 0) < 0) {
                print("sh: cannot open ");
                print(stage_in[i]);
                print("\n");
                sys_exit(1);
            }
            if (stage_out[i] &&
                apply_redirect(stage_out[i],
                               O_WRONLY | O_CREAT |
                               (stage_append[i] ? O_APPEND : O_TRUNC),
                               1) < 0) {
                print("sh: cannot open ");
                print(stage_out[i]);
                print("\n");
                sys_exit(1);
            }
            for (int j = 0; j < created; j++) {
                sys_close(pipes[j][0]);
                sys_close(pipes[j][1]);
            }
            exec_command(stage_argv[i]);
        }
        pids[i] = pid;
        if (pid < 0) {
            print("sh: fork failed\n");
            stage_count = i;
            break;
        }
    }

    for (int i = 0; i < created; i++) {
        sys_close(pipes[i][0]);
        sys_close(pipes[i][1]);
    }
    for (int i = 0; i < stage_count; i++) {
        if (pids[i] > 0) {
            wait_for(pids[i]);
        }
    }
}

int main(long argc, char **argv)
{
    char line[LINE_MAX];
    char *stages[PIPE_STAGES];
    char *args[PIPE_STAGES][ARG_MAX];
    char *redir_in[PIPE_STAGES];
    char *redir_out[PIPE_STAGES];
    int redir_append[PIPE_STAGES];
    int counts[PIPE_STAGES];

    (void)argc;
    (void)argv;

    for (;;) {
        int stage_count;
        int first_argc = 0;

        print("sh> ");
        if (read_line(line, sizeof(line)) <= 0) {
            print("\n");
            break;
        }

        stage_count = split_pipeline(line, stages, PIPE_STAGES);
        if (stage_count < 0) {
            print("sh: pipeline too long\n");
            continue;
        }
        for (int i = 0; i < stage_count; i++) {
            counts[i] = split(stages[i], args[i], ARG_MAX);
            if (counts[i] == 0 ||
                parse_redirects(args[i], &counts[i], &redir_in[i],
                                &redir_out[i], &redir_append[i]) < 0) {
                print("sh: invalid pipeline stage\n");
                stage_count = 0;
                break;
            }
        }
        if (stage_count == 0) {
            continue;
        }

        first_argc = counts[0];
        if (stage_count == 1 && !redir_in[0] && !redir_out[0]) {
            char *name = args[0][0];

            if (same(name, "exit") && first_argc == 1) {
                break;
            }
            if (same(name, "help") && first_argc == 1) {
                print("shell: builtins: cd, clear, exit, help, pwd; "
                      "external commands live in /bin\n");
                continue;
            }
            if (same(name, "clear") && first_argc == 1) {
                sys_clear();
                continue;
            }
            if (same(name, "pwd") && first_argc == 1) {
                char cwd[256];

                if (sys_getcwd(cwd, sizeof(cwd)) > 0) {
                    print(cwd);
                    print("\n");
                } else {
                    print("sh: pwd failed\n");
                }
                continue;
            }
            if (same(name, "cd") && first_argc <= 2) {
                const char *path = first_argc == 2 ? args[0][1] : "/";

                if (sys_chdir(path) < 0) {
                    print("sh: cd: cannot change directory to ");
                    print(path);
                    print("\n");
                }
                continue;
            }
        }

        run_pipeline(args, stage_count, redir_in, redir_out, redir_append);
    }

    return 0;
}
