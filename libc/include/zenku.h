#ifndef ZENKU_USER_H
#define ZENKU_USER_H

/*
 * Tiny freestanding user-space ABI for Zenku. Each helper is a static inline
 * int 0x80 wrapper; there is no libc yet.
 */

typedef unsigned long size_t;
typedef long ssize_t;
typedef long off_t;

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR   0x0002
#define O_ACCMODE 0x0003

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define SYS_read    0
#define SYS_write   1
#define SYS_open    2
#define SYS_close   3
#define SYS_fstat   5
#define SYS_lseek   8
#define SYS_pipe    22
#define SYS_yield   24
#define SYS_dup2    33
#define SYS_getpid  39
#define SYS_fork    57
#define SYS_execve  59
#define SYS_exit    60
#define SYS_wait4   61
#define SYS_readdir 78
#define SYS_getprocs 79
#define SYS_sleep  80
#define SYS_getticks 81
#define SYS_getppid 110

#define ZP_FREE   0
#define ZP_READY  1
#define ZP_RUN    2
#define ZP_BLOCK  3
#define ZP_ZOMBIE 4

#define ZENKU_NAME_MAX 256

struct zenku_stat {
    unsigned int ino;
    unsigned int mode;
    unsigned long size;
    unsigned int links;
};

struct zenku_dirent {
    unsigned int ino;
    unsigned char type;
    unsigned char name_len;
    char name[ZENKU_NAME_MAX];
};

struct zenku_procinfo {
    int pid;
    int ppid;
    int state;
    char name[16];
};

#define ZS_IFMT  0170000
#define ZS_IFREG 0100000
#define ZS_IFDIR 0040000
#define ZS_IFCHR 0020000

static inline long syscall1(long number, long arg)
{
    long ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(number), "D"(arg)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall3(long number, long a, long b, long c)
{
    long ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(number), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_read(int fd, void *buf, size_t len)
{
    return syscall3(SYS_read, fd, (long)buf, (long)len);
}

static inline long sys_write(int fd, const void *buf, size_t len)
{
    return syscall3(SYS_write, fd, (long)buf, (long)len);
}

static inline long sys_open(const char *path, int flags)
{
    return syscall3(SYS_open, (long)path, flags, 0);
}

static inline long sys_close(int fd)
{
    return syscall1(SYS_close, fd);
}

static inline long sys_lseek(int fd, off_t offset, int whence)
{
    return syscall3(SYS_lseek, fd, (long)offset, whence);
}

static inline long sys_fstat(int fd, struct zenku_stat *st)
{
    return syscall3(SYS_fstat, fd, (long)st, 0);
}

static inline long sys_pipe(int fds[2])
{
    return syscall3(SYS_pipe, (long)fds, 0, 0);
}

static inline long sys_dup2(int oldfd, int newfd)
{
    return syscall3(SYS_dup2, oldfd, newfd, 0);
}

static inline long sys_readdir(const char *path, unsigned int index,
                               struct zenku_dirent *out)
{
    return syscall3(SYS_readdir, (long)path, index, (long)out);
}

static inline long sys_getprocs(struct zenku_procinfo *out, int max)
{
    return syscall3(SYS_getprocs, (long)out, max, 0);
}

static inline long sys_sleep_ms(unsigned long ms)
{
    return syscall1(SYS_sleep, (long)ms);
}

static inline unsigned long sys_getticks(void)
{
    return (unsigned long)syscall1(SYS_getticks, 0);
}

static inline long sys_yield(void)
{
    return syscall1(SYS_yield, 0);
}

static inline long sys_fork(void)
{
    return syscall1(SYS_fork, 0);
}

static inline long sys_execve(const char *path, char *const argv[],
                              char *const envp[])
{
    return syscall3(SYS_execve, (long)path, (long)argv, (long)envp);
}

static inline long sys_wait4(long pid, int *status, int options)
{
    return syscall3(SYS_wait4, pid, (long)status, options);
}

static inline long sys_getpid(void)
{
    return syscall1(SYS_getpid, 0);
}

static inline long sys_getppid(void)
{
    return syscall1(SYS_getppid, 0);
}

static inline void sys_exit(int status)
{
    syscall1(SYS_exit, status);
    for (;;) {
    }
}

#endif /* ZENKU_USER_H */
