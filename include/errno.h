#ifndef ZENKU_ERRNO_H
#define ZENKU_ERRNO_H

/* POSIX-ish error numbers returned as negative values from the VFS. */
#define EPERM    1
#define ENOENT   2
#define EIO      5
#define ENOEXEC  8
#define EBADF    9
#define EAGAIN   11
#define ENOMEM   12
#define EFAULT   14
#define ENODEV   19
#define ENOTDIR  20
#define EISDIR   21
#define EINVAL   22
#define EMFILE   24
#define ENOSPC   28
#define EROFS    30
#define ENAMETOOLONG 36
#define ENOSYS   38

#endif /* ZENKU_ERRNO_H */
