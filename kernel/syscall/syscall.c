#include <arch/x86_64/idt.h>
#include <console.h>
#include <errno.h>
#include <kio.h>
#include <mm/vmm.h>
#include <pit.h>
#include <process.h>
#include <string.h>
#include <syscall.h>
#include <types.h>
#include <vfs.h>

/* User space occupies the lower canonical half. */
#define USER_SPACE_END 0x0000800000000000ull

#define MAX_USER_PATH 256

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

static int64_t sys_dup2(int oldfd, int newfd)
{
    return vfs_dup2(oldfd, newfd);
}

static int64_t sys_readdir(uint64_t path_ptr, uint32_t index, uint64_t out_ptr)
{
    struct vfs_dirent ent;
    char path[MAX_USER_PATH];
    int rc;

    rc = copy_user_path(path_ptr, path, sizeof(path));
    if (rc < 0) {
        return rc;
    }
    if (!user_range_ok(out_ptr, sizeof(ent))) {
        return -EFAULT;
    }
    rc = vfs_readdir(path, index, &ent);
    if (rc < 0) {
        return rc;
    }
    memcpy((void *)(uintptr_t)out_ptr, &ent, sizeof(ent));
    return 0;
}

static int64_t sys_getprocs(uint64_t out_ptr, int max)
{
    struct process_info infos[PROCESS_MAX];
    int count;

    if (max < 0) {
        return -EINVAL;
    }
    if (max > PROCESS_MAX) {
        max = PROCESS_MAX;
    }
    if (max && !user_range_ok(out_ptr, (uint64_t)max * sizeof(infos[0]))) {
        return -EFAULT;
    }
    count = process_snapshot(infos, max);
    if (count > 0) {
        memcpy((void *)(uintptr_t)out_ptr, infos,
               (size_t)count * sizeof(infos[0]));
    }
    return count;
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

static int64_t sys_chdir(uint64_t path_ptr)
{
    char path[MAX_USER_PATH];

    if (copy_user_path(path_ptr, path, sizeof(path)) < 0) {
        return -EFAULT;
    }
    return vfs_chdir(path);
}

static int64_t sys_getcwd(uint64_t buf, uint64_t size)
{
    char path[VFS_NAME_MAX];
    int len;

    if (size == 0 || !user_range_ok(buf, size)) {
        return -EFAULT;
    }
    len = vfs_getcwd(path, sizeof(path));
    if (len < 0) {
        return len;
    }
    if ((uint64_t)len > size) {
        return -ERANGE;
    }
    memcpy((void *)(uintptr_t)buf, path, (size_t)len);
    return len;
}

static int64_t sys_getpid(void)
{
    return process_getpid();
}

static int64_t sys_getppid(void)
{
    return process_getppid();
}

static int64_t sys_execve(struct regs *r, uint64_t path_ptr, uint64_t argv_ptr)
{
    char path[MAX_USER_PATH];
    char args[PROCESS_MAX_ARGS][PROCESS_ARG_MAX];
    char *argv[PROCESS_MAX_ARGS];
    int argc = 0;
    int rc;

    rc = copy_user_path(path_ptr, path, sizeof(path));
    if (rc < 0) {
        return rc;
    }

    if (argv_ptr) {
        for (int i = 0; i < PROCESS_MAX_ARGS; i++) {
            uint64_t slot = argv_ptr + (uint64_t)i * sizeof(uint64_t);
            uint64_t arg_ptr;

            if (!user_range_ok(slot, sizeof(uint64_t))) {
                return -EFAULT;
            }
            arg_ptr = *(const uint64_t *)(uintptr_t)slot;
            if (arg_ptr == 0) {
                break;
            }
            rc = copy_user_path(arg_ptr, args[i], PROCESS_ARG_MAX);
            if (rc < 0) {
                return rc;
            }
            argv[i] = args[i];
            argc++;
        }
    }

    rc = process_exec(r, path, argc, argv);
    return rc;
}

static int64_t sys_wait(int pid, uint64_t status_ptr, struct regs *r)
{
    if (status_ptr && !user_range_ok(status_ptr, sizeof(int))) {
        return -EFAULT;
    }
    process_wait(r, pid, status_ptr);
    return 0;
}

void syscall_dispatch(struct regs *r)
{
    int64_t ret;

    switch (r->rax) {
    case SYS_read:
        ret = sys_read((int)r->rdi, r->rsi, r->rdx);
        if (ret == -EAGAIN && fd_is_console((int)r->rdi)) {
            console_set_user_input(true);
            process_block_io(r);
            return;
        }
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
    case SYS_mmap:
        ret = (int64_t)process_mmap(r->rdi, r->rsi, (int)r->rdx,
                                    (int)r->r10);
        break;
    case SYS_munmap:
        ret = process_munmap(r->rdi, r->rsi);
        break;
    case SYS_brk:
        ret = process_brk(r->rdi);
        break;
    case SYS_pipe:
        ret = sys_pipe(r->rdi);
        break;
    case SYS_dup2:
        ret = sys_dup2((int)r->rdi, (int)r->rsi);
        break;
    case SYS_readdir:
        ret = sys_readdir(r->rdi, (uint32_t)r->rsi, r->rdx);
        break;
    case SYS_getprocs:
        ret = sys_getprocs(r->rdi, (int)r->rsi);
        break;
    case SYS_sleep:
        process_sleep(r, r->rdi);
        return;
    case SYS_kill:
        ret = process_kill(r, (int)r->rdi, (int)r->rsi);
        if (ret == 0) {
            r->rax = 0;
        } else if (ret < 0) {
            r->rax = (uint64_t)ret;
        }
        return;
    case SYS_getticks:
        ret = (int64_t)pit_ticks();
        break;
    case SYS_clear:
        console_clear();
        ret = 0;
        break;
    case SYS_chdir:
        ret = sys_chdir(r->rdi);
        break;
    case SYS_getcwd:
        ret = sys_getcwd(r->rsi, r->rdx);
        break;
    case SYS_yield:
        process_yield(r);
        return;
    case SYS_getpid:
        ret = sys_getpid();
        break;
    case SYS_getppid:
        ret = sys_getppid();
        break;
    case SYS_fork:
        ret = process_fork(r);
        break;
    case SYS_execve:
        ret = sys_execve(r, r->rdi, r->rsi);
        if (ret < 0) {
            r->rax = (uint64_t)ret;
        }
        /* On success process_exec() replaced the frame; do not touch it. */
        return;
    case SYS_wait4:
        ret = sys_wait((int)r->rdi, r->rsi, r);
        if (ret < 0) {
            r->rax = (uint64_t)ret;
        }
        /* sys_wait() sets rax when it reaps and switches away otherwise. */
        return;
    case SYS_exit:
        process_exit(r, (int)r->rdi);
        return;
    default:
        kprintf("syscall: unknown number %llu\n", r->rax);
        ret = -ENOSYS;
        break;
    }

    r->rax = (uint64_t)ret;
}
