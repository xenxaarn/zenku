#ifndef ZENKU_SYSCALL_H
#define ZENKU_SYSCALL_H

#include <arch/x86_64/idt.h>

/* int 0x80 is the only syscall gate in Phase 4. */
#define SYSCALL_VECTOR 0x80

/* Linux x86_64 syscall numbers for the subset we implement. */
#define SYS_read   0
#define SYS_write  1
#define SYS_open   2
#define SYS_close  3
#define SYS_fstat  5
#define SYS_lseek  8
#define SYS_pipe   22
#define SYS_getpid 39
#define SYS_exit   60
#define SYS_getppid 110

void syscall_dispatch(struct regs *r);

#endif /* ZENKU_SYSCALL_H */
