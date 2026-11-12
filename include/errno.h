#ifndef ZENKU_ERRNO_H
#define ZENKU_ERRNO_H

/* POSIX-ish error numbers returned as negative values from the VFS. */
#define EPERM    1
#define ENOENT   2
#define ESRCH    3
#define EIO      5
#define ENOEXEC  8
#define EBADF    9
#define ECHILD   10
#define EAGAIN   11
#define ENOMEM   12
#define EFAULT   14
#define EBUSY    16
#define ENODEV   19
#define ENOTDIR  20
#define EISDIR   21
#define EINVAL   22
#define ENFILE   23
#define EMFILE   24
#define ENOSPC   28
#define EROFS    30
#define EPIPE    32
#define ERANGE   34
#define ENAMETOOLONG 36
#define ENOSYS   38

#endif /* ZENKU_ERRNO_H */
