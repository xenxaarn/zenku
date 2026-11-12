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

static void print_hex(unsigned long value)
{
    static const char digits[] = "0123456789abcdef";
    int shift = 60;

    while (shift > 0 && ((value >> shift) & 0xF) == 0) {
        shift -= 4;
    }
    for (; shift >= 0; shift -= 4) {
        char c = digits[(value >> shift) & 0xF];

        sys_write(1, &c, 1);
    }
}

static void print_uint(unsigned long value)
{
    char digits[21];
    int count = 0;

    if (value == 0) {
        digits[count++] = '0';
    }
    while (value && count < 20) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (count > 0) {
        char c = digits[--count];

        sys_write(1, &c, 1);
    }
}

int main(long argc, char **argv)
{
    char buf[64];
    struct zenku_stat st;
    long fd;
    long n;
    long total = 0;
    long pid;
    long ppid;
    unsigned short cs;

    (void)argc;
    (void)argv;

    print("init: hello from ring 3\n");

    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    print("init: cs 0x");
    print_hex(cs);
    print("\n");

    pid = sys_getpid();
    ppid = sys_getppid();
    print("init: pid ");
    print_uint((unsigned long)pid);
    print(" ppid ");
    print_uint((unsigned long)ppid);
    print("\n");

    if (sys_write(1, (const void *)0xdead0000, 4) == -14) {
        print("init: bad user pointer rejected\n");
    } else {
        print("init: bad user pointer FAILED\n");
    }

    {
        int pfd[2];
        char pbuf[16];
        long w;
        long r;

        if (sys_pipe(pfd) == 0) {
            w = sys_write(pfd[1], "hello pipe", 10);
            r = sys_read(pfd[0], pbuf, sizeof(pbuf));
            if (w == 10 && r == 10 && pbuf[0] == 'h' && pbuf[9] == 'e') {
                print("init: pipe transfer ok\n");
            } else {
                print("init: pipe transfer FAILED\n");
            }

            if (sys_read(pfd[0], pbuf, 4) == -11) {
                print("init: empty pipe returned EAGAIN\n");
            } else {
                print("init: empty pipe FAILED\n");
            }

            sys_close(pfd[1]);
            if (sys_read(pfd[0], pbuf, 4) == 0) {
                print("init: pipe EOF after close ok\n");
            } else {
                print("init: pipe EOF FAILED\n");
            }
            sys_close(pfd[0]);
        } else {
            print("init: pipe creation FAILED\n");
        }
    }

    fd = sys_open("/etc/motd", O_RDONLY);
    if (fd < 0) {
        print("init: open /etc/motd failed\n");
    } else {
        if (sys_fstat((int)fd, &st) == 0) {
            print("init: /etc/motd is ");
            print_uint((unsigned long)st.size);
            print(" bytes\n");
        }
        print("init: reading /etc/motd\n");
        while ((n = sys_read((int)fd, buf, sizeof(buf))) > 0) {
            sys_write(1, buf, (size_t)n);
            total += n;
        }
        if (sys_lseek((int)fd, 0, SEEK_SET) == 0) {
            print("init: lseek back to start ok\n");
        }
        sys_close((int)fd);
        print("init: read ");
        print_uint((unsigned long)total);
        print(" bytes\n");
    }

    {
        static char *const echo_argv[] = {
            "echo", "hello", "from", "exec", 0
        };
        int status = -1;
        long child;

        child = sys_fork();
        if (child == 0) {
            sys_execve("/bin/echo", echo_argv, 0);
            print("init: exec /bin/echo FAILED\n");
            return 1;
        } else if (child > 0) {
            print("init: forked child ");
            print_uint((unsigned long)child);
            print("\n");
            if (sys_wait4(child, &status, 0) == child && status == 0) {
                print("init: child exec and exit ok\n");
            } else {
                print("init: child wait FAILED\n");
            }
        } else {
            print("init: fork FAILED\n");
        }
    }

    {
        int pfd[2];
        long writer;
        int status = -1;
        char pbuf[32];

        if (sys_pipe(pfd) == 0) {
            writer = sys_fork();
            if (writer == 0) {
                sys_close(pfd[0]);
                sys_write(pfd[1], "pipe across fork", 16);
                sys_close(pfd[1]);
                sys_exit(7);
            }
            sys_close(pfd[1]);
            do {
                n = sys_read(pfd[0], pbuf, sizeof(pbuf));
                if (n == -11) {
                    sys_yield();
                }
            } while (n == -11);
            if (n == 16 && pbuf[0] == 'p') {
                print("init: pipe across fork ok\n");
            } else {
                print("init: pipe across fork FAILED\n");
            }
            sys_close(pfd[0]);
            if (writer > 0 && sys_wait4(writer, &status, 0) == writer &&
                status == 7) {
                print("init: child exit status 7 ok\n");
            } else {
                print("init: child exit status FAILED\n");
            }
        } else {
            print("init: pipe across fork FAILED\n");
        }
    }

    {
        long a;
        long b;
        int s1 = -1;
        int s2 = -1;

        a = sys_fork();
        if (a == 0) {
            volatile unsigned long x = 0;

            for (unsigned long i = 0; i < 3000000UL; i++) {
                x += i;
            }
            print("preempt: child A finished\n");
            sys_exit(0);
        }
        b = sys_fork();
        if (b == 0) {
            print("preempt: child B ran\n");
            sys_exit(0);
        }
        if (a > 0 && sys_wait4(a, &s1, 0) == a) {
            print("preempt: child A reaped\n");
        }
        if (b > 0 && sys_wait4(b, &s2, 0) == b) {
            print("preempt: child B reaped\n");
        }
    }

    print("init: exiting with status 0\n");
    return 0;
}
