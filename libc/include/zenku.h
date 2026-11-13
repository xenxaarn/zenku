#ifndef ZENKU_USER_H
#define ZENKU_USER_H

/*
 * Tiny freestanding user-space ABI for Zenku. Each helper is a static inline
 * SYSCALL wrapper. The standard headers in this directory provide a small
 * C library built on top of these calls.
 */

typedef unsigned long size_t;
typedef long ssize_t;
typedef long off_t;
typedef unsigned long uintptr_t;
typedef long intptr_t;

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR   0x0002
#define O_ACCMODE 0x0003
#define O_CREAT  0x0040
#define O_EXCL   0x0080
#define O_TRUNC  0x0200
#define O_APPEND 0x0400

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20

#define MAP_FAILED ((void *)-1)

#define SYS_read    0
#define SYS_write   1
#define SYS_open    2
#define SYS_close   3
#define SYS_fstat   5
#define SYS_lseek   8
#define SYS_mmap    9
#define SYS_munmap  11
#define SYS_brk     12
#define SYS_pipe    22
#define SYS_yield   24
#define SYS_dup2    33
#define SYS_getpid  39
#define SYS_fork    57
#define SYS_execve  59
#define SYS_exit    60
#define SYS_wait4   61
#define SYS_kill    62
#define SYS_readdir 78
#define SYS_getprocs 79
#define SYS_sleep  80
#define SYS_getticks 81
#define SYS_clear  82
#define SYS_chdir  83
#define SYS_getcwd 84
#define SYS_getppid 110
#define SYS_poweroff 169
#define SYS_mkdir    170
#define SYS_rmdir    171
#define SYS_unlink   172
#define SYS_truncate 173
#define SYS_fsync    174
#define SYS_sync     175

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

    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(number), "D"(arg)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall3(long number, long a, long b, long c)
{
    long ret;

    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(number), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall6(long number, long a, long b, long c, long d,
                            long e, long f)
{
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    long ret;

    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(number), "D"(a), "S"(b), "d"(c), "r"(r10),
                       "r"(r8), "r"(r9)
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

static inline long sys_brk(void *addr)
{
    return syscall1(SYS_brk, (long)addr);
}

static inline void *sys_mmap(void *addr, size_t length, int prot, int flags,
                             int fd, off_t offset)
{
    long ret = syscall6(SYS_mmap, (long)addr, (long)length, prot, flags, fd,
                        (long)offset);

    return ret < 0 ? MAP_FAILED : (void *)ret;
}

static inline long sys_munmap(void *addr, size_t length)
{
    return syscall3(SYS_munmap, (long)addr, (long)length, 0);
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

static inline long sys_clear(void)
{
    return syscall1(SYS_clear, 0);
}

static inline long sys_chdir(const char *path)
{
    return syscall1(SYS_chdir, (long)path);
}

static inline long sys_getcwd(char *buf, size_t size)
{
    return syscall3(SYS_getcwd, 0, (long)buf, (long)size);
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

static inline long sys_kill(long pid, int sig)
{
    return syscall3(SYS_kill, pid, sig, 0);
}

static inline long sys_getpid(void)
{
    return syscall1(SYS_getpid, 0);
}

static inline long sys_poweroff(void)
{
    return syscall1(SYS_poweroff, 0);
}

static inline long sys_mkdir(const char *path, unsigned int mode)
{
    return syscall3(SYS_mkdir, (long)path, (long)mode, 0);
}

static inline long sys_rmdir(const char *path)
{
    return syscall1(SYS_rmdir, (long)path);
}

static inline long sys_unlink(const char *path)
{
    return syscall1(SYS_unlink, (long)path);
}

static inline long sys_truncate(const char *path, unsigned long length)
{
    return syscall3(SYS_truncate, (long)path, (long)length, 0);
}

static inline long sys_fsync(int fd)
{
    return syscall1(SYS_fsync, fd);
}

static inline long sys_sync(void)
{
    return syscall1(SYS_sync, 0);
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
