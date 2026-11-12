#ifndef ZENKU_LIBC_UNISTD_H
#define ZENKU_LIBC_UNISTD_H

#include <zenku.h>

typedef int pid_t;

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int open(const char *path, int flags);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
int dup2(int oldfd, int newfd);
int pipe(int fds[2]);

pid_t fork(void);
int execve(const char *path, char *const argv[], char *const envp[]);
pid_t waitpid(pid_t pid, int *status, int options);
pid_t getpid(void);
pid_t getppid(void);
void _exit(int status) __attribute__((noreturn));

char *getcwd(char *buf, size_t size);
int chdir(const char *path);
unsigned int sleep(unsigned int seconds);
void yield(void);

#endif /* ZENKU_LIBC_UNISTD_H */
