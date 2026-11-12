#include <arch/x86_64/idt.h>
#include <errno.h>
#include <kio.h>
#include <mm/vmm.h>
#include <string.h>
#include <syscall.h>
#include <types.h>
#include <vfs.h>

/* User space occupies the lower canonical half. */
#define USER_SPACE_END 0x0000800000000000ull

#define MAX_USER_PATH 256

extern void user_exit_to_kernel(void) NORETURN;

static bool user_range_ok(uint64_t addr, uint64_t len)
{
    uint64_t end;

    if (len == 0) {
        return true;
    }
    if (addr >= USER_SPACE_END || len > USER_SPACE_END - addr) {
        return false;
    }

    end = addr + len;
    for (uint64_t page = addr & ~(PAGE_SIZE - 1); page < end;
         page += PAGE_SIZE) {
        if (!vmm_is_mapped(page)) {
            return false;
        }
    }
    return true;
}

static int copy_user_path(uint64_t addr, char *out, size_t cap)
{
    for (size_t i = 0; i < cap; i++) {
        if (!user_range_ok(addr + i, 1)) {
            return -EFAULT;
        }
        out[i] = *(const char *)(uintptr_t)(addr + i);
        if (out[i] == '\0') {
            return 0;
        }
    }
    return -ENAMETOOLONG;
}

static int64_t sys_read(int fd, uint64_t buf, uint64_t len)
{
    if (!user_range_ok(buf, len)) {
        return -EFAULT;
    }
    return fd_read(fd, (void *)(uintptr_t)buf, (size_t)len);
}

static int64_t sys_write(int fd, uint64_t buf, uint64_t len)
{
    if (!user_range_ok(buf, len)) {
        return -EFAULT;
    }
    return fd_write(fd, (const void *)(uintptr_t)buf, (size_t)len);
}

static int64_t sys_open(uint64_t path_ptr, int flags)
{
    char path[MAX_USER_PATH];
    int rc;

    rc = copy_user_path(path_ptr, path, sizeof(path));
    if (rc < 0) {
        return rc;
    }
    return vfs_open(path, flags);
}

static int64_t sys_close(int fd)
{
    return fd_close(fd);
}

static int64_t sys_lseek(int fd, int64_t offset, int whence)
{
    return fd_lseek(fd, offset, whence);
}

static int64_t sys_fstat(int fd, uint64_t stat_ptr)
{
    struct vfs_stat st;
    int rc;

    if (!user_range_ok(stat_ptr, sizeof(st))) {
        return -EFAULT;
    }
    rc = fd_fstat(fd, &st);
    if (rc < 0) {
        return rc;
    }
    memcpy((void *)(uintptr_t)stat_ptr, &st, sizeof(st));
    return 0;
}

static int64_t sys_exit(int status)
{
    kprintf("user: exit status %d\n", status);
    user_exit_to_kernel();
}

static int64_t sys_pipe(uint64_t fds_ptr)
{
    int fds[2];
    int rc;

    if (!user_range_ok(fds_ptr, sizeof(fds))) {
        return -EFAULT;
    }
    rc = vfs_pipe(fds);
    if (rc < 0) {
        return rc;
    }
    memcpy((void *)(uintptr_t)fds_ptr, fds, sizeof(fds));
    return 0;
}

static int64_t sys_getpid(void)
{
    /* One user process runs at a time in Phase 4. */
    return 1;
}

static int64_t sys_getppid(void)
{
    return 0;
}

void syscall_dispatch(struct regs *r)
{
    int64_t ret;

    switch (r->rax) {
    case SYS_read:
        ret = sys_read((int)r->rdi, r->rsi, r->rdx);
        break;
    case SYS_write:
        ret = sys_write((int)r->rdi, r->rsi, r->rdx);
        break;
    case SYS_open:
        ret = sys_open(r->rdi, (int)r->rsi);
        break;
    case SYS_close:
        ret = sys_close((int)r->rdi);
        break;
    case SYS_fstat:
        ret = sys_fstat((int)r->rdi, r->rsi);
        break;
    case SYS_lseek:
        ret = sys_lseek((int)r->rdi, (int64_t)r->rsi, (int)r->rdx);
        break;
    case SYS_pipe:
        ret = sys_pipe(r->rdi);
        break;
    case SYS_getpid:
        ret = sys_getpid();
        break;
    case SYS_getppid:
        ret = sys_getppid();
        break;
    case SYS_exit:
        ret = sys_exit((int)r->rdi);
        break;
    default:
        kprintf("syscall: unknown number %llu\n", r->rax);
        ret = -ENOSYS;
        break;
    }

    r->rax = (uint64_t)ret;
}
