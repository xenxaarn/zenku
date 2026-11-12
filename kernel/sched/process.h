#ifndef ZENKU_PROCESS_H
#define ZENKU_PROCESS_H

#include <arch/x86_64/idt.h>
#include <types.h>

#define PROCESS_MAX       16
#define PROCESS_NAME_MAX  16
#define PROCESS_KSTACK    16384
#define PROCESS_MAX_ARGS  16
#define PROCESS_ARG_MAX   128

enum process_state {
    PROC_FREE = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_ZOMBIE,
};

struct process_info {
    int pid;
    int ppid;
    int state;
    char name[PROCESS_NAME_MAX];
};

struct process {
    int pid;
    int ppid;
    enum process_state state;
    int exit_status;
    int wait_pid;
    uint64_t wait_status_ptr;
    bool block_io;
    uint64_t sleep_until;
    uint64_t pml4_phys;
    uint8_t *kstack;
    uint64_t kstack_top;
    int fdt;
    uint64_t heap_base;
    uint64_t heap_brk;
    uint64_t heap_limit;
    uint64_t mmap_next;
    uint64_t mmap_end;
    uint64_t stack_bottom;
    uint64_t stack_limit;
    struct regs regs;
    char name[PROCESS_NAME_MAX];
};

void process_init(void);
struct process *process_current(void);
bool process_active(void);

/* Create a process from an ELF file and run it. Returns when the process tree
 * has no runnable processes left. */
int process_run(const char *path);

int process_fork(struct regs *frame);
int process_exec(struct regs *frame, const char *path, int argc,
                 char *const argv[]);
void process_wait(struct regs *frame, int pid, uint64_t status_ptr);
void process_exit(struct regs *frame, int status);
void process_yield(struct regs *frame);
void process_tick(struct regs *frame);

/* Block the current process on a non-blocking device read. The syscall is
 * retried when process_wake_io() marks it runnable again. */
void process_block_io(struct regs *frame);
void process_wake_io(void);

/* Sleep for `ms` milliseconds; the syscall frame resumes after the call. */
void process_sleep(struct regs *frame, uint64_t ms);

/* Send SIGKILL (9) or SIGTERM (15). Returns 0, a negative errno, or 1 when
 * the current process terminated itself and switched away. */
int process_kill(struct regs *frame, int pid, int sig);

/* Heap break. new_brk == 0 queries the current break; changes outside the
 * reserved heap region are rejected and leave the break unchanged. */
int64_t process_brk(uint64_t new_brk);

/* Anonymous private mmap/munmap. Mappings are demand-zero only for the heap;
 * anonymous mmap pages are allocated eagerly. */
uint64_t process_mmap(uint64_t addr, uint64_t length, int prot, int flags);
int process_munmap(uint64_t addr, uint64_t length);

/* Resolve a not-present user fault with demand-zero heap pages or guarded
 * stack growth. Returns true when the faulting instruction may be retried. */
bool process_page_fault(uint64_t virt, uint64_t error);

int process_getpid(void);
int process_getppid(void);
void process_print(void);

/* Copy live process records into `out`, returning the number written. */
int process_snapshot(struct process_info *out, int max);

#endif /* ZENKU_PROCESS_H */
