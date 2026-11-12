#include <errno.h>
#include <unistd.h>

static long check(long rc)
{
    if (rc < 0) {
        errno = (int)-rc;
        return -1;
    }
    return rc;
}

ssize_t read(int fd, void *buf, size_t count)
{
    return (ssize_t)check(sys_read(fd, buf, count));
}

ssize_t write(int fd, const void *buf, size_t count)
{
    return (ssize_t)check(sys_write(fd, buf, count));
}

int open(const char *path, int flags)
{
    return (int)check(sys_open(path, flags));
}

int close(int fd)
{
    return (int)check(sys_close(fd));
}

off_t lseek(int fd, off_t offset, int whence)
{
    return (off_t)check(sys_lseek(fd, offset, whence));
}

int dup2(int oldfd, int newfd)
{
    return (int)check(sys_dup2(oldfd, newfd));
}

int pipe(int fds[2])
{
    return (int)check(sys_pipe(fds));
}

pid_t fork(void)
{
    return (pid_t)check(sys_fork());
}

int execve(const char *path, char *const argv[], char *const envp[])
{
    return (int)check(sys_execve(path, argv, envp));
}

pid_t waitpid(pid_t pid, int *status, int options)
{
    return (pid_t)check(sys_wait4(pid, status, options));
}

pid_t getpid(void)
{
    return (pid_t)sys_getpid();
}

pid_t getppid(void)
{
    return (pid_t)sys_getppid();
}

void _exit(int status)
{
    sys_exit(status);
    __builtin_unreachable();
}

char *getcwd(char *buf, size_t size)
{
    long rc = sys_getcwd(buf, size);

    if (rc < 0) {
        errno = (int)-rc;
        return 0;
    }
    return buf;
}

int chdir(const char *path)
{
    return (int)check(sys_chdir(path));
}

unsigned int sleep(unsigned int seconds)
{
    sys_sleep_ms((unsigned long)seconds * 1000UL);
    return 0;
}

void yield(void)
{
    sys_yield();
}
