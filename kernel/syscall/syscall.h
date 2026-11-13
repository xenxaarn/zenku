#ifndef ZENKU_SYSCALL_H
#define ZENKU_SYSCALL_H

#include <arch/x86_64/idt.h>

/* int 0x80 remains the compatibility gate; SYSCALL is the fast path. */
#define SYSCALL_VECTOR 0x80

/* Linux x86_64 syscall numbers for the subset we implement. */
#define SYS_read   0
#define SYS_write  1
#define SYS_open   2
#define SYS_close  3
#define SYS_fstat  5
#define SYS_lseek  8
#define SYS_mmap   9
#define SYS_munmap 11
#define SYS_brk    12
#define SYS_pipe   22
#define SYS_yield  24
#define SYS_dup2   33
#define SYS_getpid 39
#define SYS_fork   57
#define SYS_execve 59
#define SYS_exit   60
#define SYS_wait4  61
#define SYS_kill   62
#define SYS_readdir 78
#define SYS_getprocs 79
#define SYS_sleep  80
#define SYS_getticks 81
#define SYS_clear  82
#define SYS_chdir  83
#define SYS_getcwd 84
#define SYS_getppid 110
#define SYS_poweroff 169

void syscall_dispatch(struct regs *r);
void syscall_init(void);

#endif /* ZENKU_SYSCALL_H */
