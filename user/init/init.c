#include <zenku.h>

static volatile unsigned long cow_value = 0x1234;
static int cow_pipe[2];

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

static __attribute__((noinline)) int stack_growth_probe(void)
{
    volatile unsigned char *probe;
    unsigned long sp;

    __asm__ volatile("mov %%rsp, %0" : "=r"(sp));
    probe = (volatile unsigned char *)(sp - 65536);
    probe[0] = 0x5a;
    return probe[0] == 0x5a;
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

    if (sys_write(1, (const void *)0xFFFF800000000000ul, 4) == -14) {
        print("init: kernel pointer rejected\n");
    } else {
        print("init: kernel pointer FAILED\n");
    }

    if (sys_read(0, (void *)(uintptr_t)&main, 1) == -14) {
        print("init: write to text rejected\n");
    } else {
        print("init: write to text FAILED\n");
    }

    {
        void *wx = sys_mmap(0, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (wx == MAP_FAILED) {
            print("init: writable-exec mapping rejected\n");
        } else {
            sys_munmap(wx, 4096);
            print("init: writable-exec mapping FAILED\n");
        }
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
        long child;
        int status = -1;

        cow_value = 0x1234;
        child = sys_fork();
        if (child == 0) {
            if (cow_value == 0x1234) {
                print("cow: child inherited value\n");
            } else {
                print("cow: child inherit FAILED\n");
            }
            cow_value = 0x5678;
            if (cow_value == 0x5678) {
                print("cow: child private write ok\n");
            } else {
                print("cow: child private write FAILED\n");
            }
            sys_exit(0);
        } else if (child > 0) {
            sys_wait4(child, &status, 0);
            if (cow_value == 0x1234) {
                print("cow: parent value preserved\n");
            } else {
                print("cow: parent value FAILED\n");
            }
        } else {
            print("cow: fork FAILED\n");
        }
    }

    {
        long child;
        int status = -1;

        cow_pipe[0] = -1;
        cow_pipe[1] = -1;
        child = sys_fork();
        if (child == 0) {
            if (sys_pipe(cow_pipe) == 0) {
                print("cow: child pipe fds ok\n");
                sys_close(cow_pipe[0]);
                sys_close(cow_pipe[1]);
            } else {
                print("cow: child pipe fds FAILED\n");
            }
            sys_exit(0);
        } else if (child > 0) {
            sys_wait4(child, &status, 0);
            if (cow_pipe[0] == -1 && cow_pipe[1] == -1) {
                print("cow: parent fd array preserved\n");
            } else {
                print("cow: parent fd array FAILED ");
                print_uint((unsigned long)(unsigned int)cow_pipe[0]);
                print(" ");
                print_uint((unsigned long)(unsigned int)cow_pipe[1]);
                print("\n");
            }
        } else {
            print("cow: pipe fork FAILED\n");
        }
    }

    {
        int ok = 1;

        for (int i = 0; i < 100; i++) {
            long child;
            int status = -1;

            cow_value = 0x1234;
            child = sys_fork();
            if (child == 0) {
                cow_value = 0x5000 + (unsigned long)i;
                sys_exit(cow_value == 0x5000 + (unsigned long)i ? 0 : 1);
            } else if (child > 0) {
                if (sys_wait4(child, &status, 0) != child || status != 0 ||
                    cow_value != 0x1234) {
                    ok = 0;
                }
            } else {
                ok = 0;
            }
        }
        if (ok) {
            print("cow: 100 fork stress ok\n");
        } else {
            print("cow: fork stress FAILED\n");
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

            for (unsigned long i = 0; i < 50000000UL; i++) {
                x += i;
            }
            (void)x;
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

    {
        unsigned long before = sys_getticks();
        unsigned long after;

        sys_sleep_ms(100);
        after = sys_getticks();
        if (after > before && after - before >= 5) {
            print("sleep: timer wake ok\n");
        } else {
            print("sleep: timer wake FAILED ");
            print_uint(after - before);
            print("\n");
        }
    }

    {
        long child = sys_fork();

        if (child == 0) {
            sys_sleep_ms(100000);
            sys_exit(0);
        } else if (child > 0) {
            int status = -1;

            if (sys_kill(child, 15) == 0 &&
                sys_wait4(child, &status, 0) == child && status == 143) {
                print("kill: child terminated ok\n");
            } else {
                print("kill: child FAILED\n");
            }
        } else {
            print("kill: fork FAILED\n");
        }
    }

    {
        unsigned char *base = (unsigned char *)(uintptr_t)sys_brk(0);
        unsigned char *next = base + 8192;
        volatile unsigned char *heap = (volatile unsigned char *)base;

        if ((unsigned char *)(uintptr_t)sys_brk(next) == next) {
            heap[0] = 0x11;
            heap[4097] = 0x22;
            heap[8191] = 0x33;
            if (heap[0] == 0x11 && heap[4097] == 0x22 &&
                heap[8191] == 0x33) {
                print("vm: brk demand-zero pages ok\n");
            } else {
                print("vm: brk data FAILED\n");
            }
        } else {
            print("vm: brk grow FAILED\n");
        }
    }

    {
        volatile unsigned char *map = (volatile unsigned char *)sys_mmap(
            0, 3 * 4096, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (map != (void *)-1) {
            map[0] = 0x41;
            map[4096] = 0x42;
            map[8191] = 0x43;
            if (map[0] == 0x41 && map[4096] == 0x42 && map[8191] == 0x43 &&
                sys_munmap((void *)map, 3 * 4096) == 0) {
                print("vm: anonymous mmap and munmap ok\n");
            } else {
                print("vm: mmap data FAILED\n");
            }
        } else {
            print("vm: mmap FAILED\n");
        }
    }

    if (stack_growth_probe()) {
        print("vm: stack guard growth ok\n");
    } else {
        print("vm: stack growth FAILED\n");
    }

    {
        long child = sys_fork();

        if (child == 0) {
            char *args[] = {"/bin/missing", 0};

            if (sys_execve("/bin/missing", args, 0) < 0) {
                print("exec: failure rollback ok\n");
                sys_exit(7);
            }
            sys_exit(1);
        } else if (child > 0) {
            int status = -1;

            if (sys_wait4(child, &status, 0) == child && status == 7) {
                print("exec: rollback status ok\n");
            } else {
                print("exec: rollback FAILED\n");
            }
        } else {
            print("exec: fork FAILED\n");
        }
    }

    {
        long child = sys_fork();

        if (child == 0) {
            volatile unsigned long *bad = (volatile unsigned long *)0;

            *bad = 1;
            sys_exit(0);
        } else if (child > 0) {
            int status = -1;

            if (sys_wait4(child, &status, 0) == child && status == 139) {
                print("fault: user SIGSEGV isolated ok\n");
            } else {
                print("fault: isolation FAILED\n");
            }
        } else {
            print("fault: fork FAILED\n");
        }
    }

    {
        int spawned = 0;
        int failed = 0;

        for (int i = 0; i < 32; i++) {
            long child = sys_fork();

            if (child == 0) {
                sys_sleep_ms(300);
                sys_exit(0);
            } else if (child > 0) {
                spawned++;
            } else {
                failed = 1;
                break;
            }
        }

        while (sys_wait4(-1, 0, 0) > 0) {
        }
        if (failed && spawned >= 10) {
            print("fork: process limit enforced ok\n");
        } else {
            print("fork: process limit FAILED\n");
        }
    }

    print("init: exiting with status 0\n");
    return 0;
}
