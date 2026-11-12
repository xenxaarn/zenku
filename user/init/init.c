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

void _start(void)
{
    char buf[64];
    struct zenku_stat st;
    long fd;
    long n;
    long total = 0;
    long pid;
    long ppid;
    unsigned short cs;

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

    print("init: exiting with status 0\n");
    sys_exit(0);
}
