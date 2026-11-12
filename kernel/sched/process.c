#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <compiler.h>
#include <console.h>
#include <errno.h>
#include <kio.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <string.h>
#include <types.h>
#include <loader.h>
#include <process.h>
#include <vfs.h>

#define USER_STACK_TOP   0x00007FFFFFF00000ull
#define USER_STACK_PAGES 4
#define PHYS_MAP_BASE    0xFFFF800000000000ull

#define USER_CS 0x1B
#define USER_SS 0x23
#define USER_RFLAGS 0x202ull

#define SCHED_QUANTUM 2

extern void sched_run_first(struct regs *frame, uint64_t kstack_top,
                            uint64_t cr3);
extern void sched_return_to_kernel(void) NORETURN;

static struct process g_procs[PROCESS_MAX];
static struct process *g_current;
static unsigned g_quantum;

/* Resources belonging to processes that have been switched away from. They
 * are released on a later switch, never while their owner's kernel stack is
 * still in use. */
static uint8_t *g_reap_stacks[PROCESS_MAX];
static unsigned g_reap_stack_count;
static uint64_t g_reap_spaces[PROCESS_MAX];
static unsigned g_reap_space_count;

static void copy_name(struct process *p, const char *path)
{
    const char *base = path;
    size_t i = 0;

    for (const char *c = path; *c; c++) {
        if (*c == '/') {
            base = c + 1;
        }
    }
    while (base[i] && i < PROCESS_NAME_MAX - 1) {
        p->name[i] = base[i];
        i++;
    }
    p->name[i] = '\0';
}

static struct process *find_pid(int pid)
{
    if (pid <= 0) {
        return NULL;
    }
    for (int i = 0; i < PROCESS_MAX; i++) {
        if (g_procs[i].state != PROC_FREE && g_procs[i].pid == pid) {
            return &g_procs[i];
        }
    }
    return NULL;
}

static struct process *process_alloc(int ppid, const char *path)
{
    for (int i = 0; i < PROCESS_MAX; i++) {
        struct process *p = &g_procs[i];

        if (p->state != PROC_FREE) {
            continue;
        }
        memset(p, 0, sizeof(*p));
        p->pid = i + 1;
        p->ppid = ppid;
        p->state = PROC_READY;
        p->fdt = -1;
        copy_name(p, path);
        return p;
    }
    return NULL;
}

static void reap_graveyard(void)
{
    for (unsigned i = 0; i < g_reap_stack_count; i++) {
        kfree(g_reap_stacks[i]);
    }
    g_reap_stack_count = 0;

    for (unsigned i = 0; i < g_reap_space_count; i++) {
        vmm_space_destroy(g_reap_spaces[i]);
    }
    g_reap_space_count = 0;
}

static void graveyard_process(struct process *p)
{
    if (!p) {
        return;
    }
    if (p->kstack && g_reap_stack_count < PROCESS_MAX) {
        g_reap_stacks[g_reap_stack_count++] = p->kstack;
        p->kstack = NULL;
        p->kstack_top = 0;
    }
    if (p->pml4_phys && g_reap_space_count < PROCESS_MAX) {
        g_reap_spaces[g_reap_space_count++] = p->pml4_phys;
        p->pml4_phys = 0;
    }
}

static void release_process(struct process *p)
{
    if (!p) {
        return;
    }
    if (p->fdt > 0) {
        vfs_fdtable_destroy(p->fdt);
        p->fdt = -1;
    }
    /* Zombies normally have their stack and space in the graveyard. */
    if (p->kstack) {
        kfree(p->kstack);
    }
    if (p->pml4_phys) {
        vmm_space_destroy(p->pml4_phys);
    }
    memset(p, 0, sizeof(*p));
    p->state = PROC_FREE;
}

static int map_user_stack(void)
{
    for (unsigned i = 0; i < USER_STACK_PAGES; i++) {
        uint64_t virt = USER_STACK_TOP - (uint64_t)(i + 1) * PAGE_SIZE;
        uint64_t frame = pmm_alloc_frame();

        if (frame == 0) {
            return -ENOMEM;
        }
        memset((void *)(uintptr_t)(PHYS_MAP_BASE + frame), 0, PAGE_SIZE);
        if (vmm_map(virt, frame, VMM_USER_FLAGS) != 0) {
            pmm_free_frame(frame);
            return -ENOMEM;
        }
    }
    return 0;
}

static void build_initial_frame(struct process *p, uint64_t entry, int argc,
                                char *const argv[])
{
    uint64_t sp = USER_STACK_TOP;
    uint64_t argv_va;
    uint64_t argv_ptrs[PROCESS_MAX_ARGS];
    int n = argc;

    if (n < 0) {
        n = 0;
    }
    if (n > PROCESS_MAX_ARGS) {
        n = PROCESS_MAX_ARGS;
    }

    sp -= 16;
    for (int i = n - 1; i >= 0; i--) {
        size_t len = strlen(argv[i]);

        if (len > PROCESS_ARG_MAX - 1) {
            len = PROCESS_ARG_MAX - 1;
        }
        sp -= len + 1;
        memcpy((void *)(uintptr_t)sp, argv[i], len);
        ((char *)(uintptr_t)sp)[len] = '\0';
        argv_ptrs[i] = sp;
    }

    sp &= ~15ull;
    sp -= (uint64_t)(n + 1) * sizeof(uint64_t);
    argv_va = sp;
    for (int i = 0; i < n; i++) {
        ((uint64_t *)(uintptr_t)sp)[i] = argv_ptrs[i];
    }
    ((uint64_t *)(uintptr_t)sp)[n] = 0;

    /* The ABI starts user code with the stack 16-byte aligned; crt0 calls
     * main(argc, argv) with the first two argument registers. */
    sp -= 8;
    *(uint64_t *)(uintptr_t)sp = 0;
    sp &= ~15ull;

    memset(&p->regs, 0, sizeof(p->regs));
    p->regs.rip = entry;
    p->regs.cs = USER_CS;
    p->regs.rflags = USER_RFLAGS;
    p->regs.rsp = sp;
    p->regs.ss = USER_SS;
    p->regs.rdi = (uint64_t)n;
    p->regs.rsi = argv_va;
    p->regs.rax = (uint64_t)n;
}

static int load_into_process(struct process *p, const char *path, int argc,
                             char *const argv[])
{
    uint64_t entry = 0;
    int rc;

    vmm_space_switch(p->pml4_phys);
    rc = elf_load(path, &entry);
    if (rc < 0) {
        return rc;
    }
    rc = map_user_stack();
    if (rc < 0) {
        return rc;
    }
    build_initial_frame(p, entry, argc, argv);
    return 0;
}

static struct process *pick_next(void)
{
    int start = g_current ? (int)(g_current - g_procs) : -1;

    for (int i = 1; i <= PROCESS_MAX; i++) {
        int index = (start + i + PROCESS_MAX) % PROCESS_MAX;

        if (g_procs[index].state == PROC_READY) {
            return &g_procs[index];
        }
    }
    return NULL;
}

/* Save the current frame, choose a runnable process, and overwrite *frame
 * with its saved register set. Returning to isr.asm's epilogue then iretq's
 * into the new process. */
static void process_schedule(struct regs *frame)
{
    struct process *prev = g_current;
    struct process *next;

    reap_graveyard();

    if (prev) {
        if (prev->state == PROC_RUNNING) {
            prev->state = PROC_READY;
        }
        prev->regs = *frame;
    }

    next = pick_next();
    if (!next) {
        graveyard_process(prev);
        g_current = NULL;
        vfs_fdtable_select(0);
        vmm_space_switch(vmm_kernel_space());
        sched_return_to_kernel();
    }

    next->state = PROC_RUNNING;
    g_current = next;
    vmm_space_switch(next->pml4_phys);
    tss_set_rsp0(next->kstack_top);
    vfs_fdtable_select(next->fdt);

    if (prev && prev->state == PROC_ZOMBIE) {
        graveyard_process(prev);
    }

    *frame = next->regs;
}

void process_init(void)
{
    memset(g_procs, 0, sizeof(g_procs));
    g_current = NULL;
    g_quantum = 0;
    g_reap_stack_count = 0;
    g_reap_space_count = 0;
}

struct process *process_current(void)
{
    return g_current;
}

bool process_active(void)
{
    return g_current != NULL;
}

int process_run(const char *path)
{
    struct process *p;
    char *argv[2];
    uint64_t saved_rsp0 = tss_get_rsp0();
    int fdt;
    int rc;

    if (!path) {
        return -EINVAL;
    }
    if (g_current) {
        return -EBUSY;
    }
    if (g_procs[0].state != PROC_FREE) {
        process_init();
    }

    p = process_alloc(0, path);
    if (!p) {
        return -EAGAIN;
    }

    fdt = vfs_fdtable_create();
    if (fdt < 0) {
        p->state = PROC_FREE;
        return fdt;
    }
    p->fdt = fdt;
    vfs_fdtable_select(fdt);

    if (vfs_open("/dev/console", O_RDONLY) < 0 ||
        vfs_open("/dev/console", O_WRONLY) < 0 ||
        vfs_open("/dev/console", O_WRONLY) < 0) {
        vfs_fdtable_destroy(fdt);
        p->fdt = -1;
        p->state = PROC_FREE;
        vfs_fdtable_select(0);
        return -ENODEV;
    }

    p->pml4_phys = vmm_space_create();
    p->kstack = kmalloc(PROCESS_KSTACK);
    if (!p->pml4_phys || !p->kstack) {
        vmm_space_switch(vmm_kernel_space());
        release_process(p);
        vfs_fdtable_select(0);
        return -ENOMEM;
    }
    p->kstack_top = ((uint64_t)(uintptr_t)p->kstack + PROCESS_KSTACK) & ~15ull;

    argv[0] = (char *)path;
    argv[1] = NULL;
    rc = load_into_process(p, path, 1, argv);
    if (rc < 0) {
        kprintf("user: cannot load %s (%d)\n", path, rc);
        vmm_space_switch(vmm_kernel_space());
        release_process(p);
        vfs_fdtable_select(0);
        return rc;
    }

    kprintf("user: entering ring 3 at 0x%llx (stack 0x%llx, %u pages)\n",
            p->regs.rip, USER_STACK_TOP, USER_STACK_PAGES);

    p->state = PROC_RUNNING;
    g_current = p;
    tss_set_rsp0(p->kstack_top);
    sched_run_first(&p->regs, p->kstack_top, p->pml4_phys);

    /* All processes are gone: sched_run_first() has returned. */
    kprintf("user: %s returned to the kernel\n", path);
    reap_graveyard();
    vmm_space_switch(vmm_kernel_space());
    for (int i = 0; i < PROCESS_MAX; i++) {
        if (g_procs[i].state != PROC_FREE) {
            release_process(&g_procs[i]);
        }
    }
    g_current = NULL;
    g_quantum = 0;
    vfs_fdtable_select(0);
    tss_set_rsp0(saved_rsp0);
    return 0;
}

int process_fork(struct regs *frame)
{
    struct process *parent = g_current;
    struct process *child;
    int fdt;

    if (!parent || !frame) {
        return -ESRCH;
    }

    child = process_alloc(parent->pid, parent->name);
    if (!child) {
        return -EAGAIN;
    }

    child->pml4_phys = vmm_space_clone(parent->pml4_phys);
    child->kstack = kmalloc(PROCESS_KSTACK);
    fdt = vfs_fdtable_clone(parent->fdt);
    if (!child->pml4_phys || !child->kstack || fdt < 0) {
        if (fdt >= 0) {
            vfs_fdtable_destroy(fdt);
        }
        release_process(child);
        return -ENOMEM;
    }
    child->fdt = fdt;
    child->kstack_top = ((uint64_t)(uintptr_t)child->kstack + PROCESS_KSTACK) &
                        ~15ull;
    child->regs = *frame;
    child->regs.rax = 0;
    child->state = PROC_READY;
    return child->pid;
}

int process_exec(struct regs *frame, const char *path, int argc,
                 char *const argv[])
{
    struct process *p = g_current;
    uint64_t old_space;
    uint64_t new_space;
    int rc;

    if (!p || !frame || !path) {
        return -ESRCH;
    }

    new_space = vmm_space_create();
    if (!new_space) {
        return -ENOMEM;
    }

    old_space = p->pml4_phys;
    p->pml4_phys = new_space;
    rc = load_into_process(p, path, argc, argv);
    if (rc < 0) {
        p->pml4_phys = old_space;
        vmm_space_switch(old_space);
        vmm_space_destroy(new_space);
        return rc;
    }

    vmm_space_destroy(old_space);
    *frame = p->regs;
    return 0;
}

void process_wait(struct regs *frame, int pid, uint64_t status_ptr)
{
    struct process *p = g_current;
    bool has_child = false;

    if (!frame) {
        return;
    }
    if (!p) {
        frame->rax = (uint64_t)(int64_t)-ECHILD;
        return;
    }

    for (int i = 0; i < PROCESS_MAX; i++) {
        struct process *child = &g_procs[i];

        if (child->state == PROC_FREE || child->ppid != p->pid) {
            continue;
        }
        if (pid > 0 && child->pid != pid) {
            continue;
        }
        has_child = true;

        if (child->state == PROC_ZOMBIE) {
            int status = child->exit_status;
            int child_pid = child->pid;

            if (status_ptr) {
                *(int *)(uintptr_t)status_ptr = status;
            }
            release_process(child);
            frame->rax = (uint64_t)(int64_t)child_pid;
            return;
        }
    }

    if (!has_child) {
        frame->rax = (uint64_t)(int64_t)-ECHILD;
        return;
    }

    /* Block until a child changes state. The syscall is retried with the
     * original register frame, so the kernel stack can be discarded. */
    p->wait_pid = pid;
    p->wait_status_ptr = status_ptr;
    p->state = PROC_BLOCKED;
    frame->rip -= 2; /* int 0x80 is two bytes */
    process_schedule(frame);
}

void process_exit(struct regs *frame, int status)
{
    struct process *p = g_current;
    int new_ppid = 0;

    if (!p) {
        sched_return_to_kernel();
    }

    kprintf("user: exit status %d\n", status);
    p->exit_status = status;
    p->state = PROC_ZOMBIE;

    if (p->fdt > 0) {
        vfs_fdtable_destroy(p->fdt);
        p->fdt = -1;
    }

    if (p->pid != 1 && find_pid(1)) {
        new_ppid = 1;
    }
    for (int i = 0; i < PROCESS_MAX; i++) {
        if (g_procs[i].state != PROC_FREE && g_procs[i].ppid == p->pid &&
            &g_procs[i] != p) {
            g_procs[i].ppid = new_ppid;
        }
    }

    {
        struct process *parent = find_pid(p->ppid);

        if (parent && parent->state == PROC_BLOCKED &&
            (parent->wait_pid <= 0 || parent->wait_pid == p->pid)) {
            parent->state = PROC_READY;
        }
    }

    process_schedule(frame);
}

void process_yield(struct regs *frame)
{
    if (!g_current) {
        return;
    }
    frame->rax = 0;
    process_schedule(frame);
}

void process_tick(struct regs *frame)
{
    if (!g_current || !frame || !(frame->cs & 3)) {
        return;
    }
    if (++g_quantum < SCHED_QUANTUM) {
        return;
    }
    g_quantum = 0;
    process_schedule(frame);
}

int process_getpid(void)
{
    return g_current ? g_current->pid : 0;
}

int process_getppid(void)
{
    return g_current ? g_current->ppid : 0;
}

void process_print(void)
{
    static const char *const names[] = {
        "free", "ready", "run", "block", "zombie",
    };
    char digits[12];
    bool any = false;

    for (int i = 0; i < PROCESS_MAX; i++) {
        struct process *p = &g_procs[i];
        int count;
        int value;

        if (p->state == PROC_FREE) {
            continue;
        }
        any = true;
        console_puts("  ");
        value = p->pid;
        count = 0;
        do {
            digits[count++] = (char)('0' + value % 10);
            value /= 10;
        } while (value && count < (int)sizeof(digits));
        while (count) {
            console_putc(digits[--count]);
        }
        console_puts("  ");
        value = p->ppid;
        count = 0;
        do {
            digits[count++] = (char)('0' + value % 10);
            value /= 10;
        } while (value && count < (int)sizeof(digits));
        while (count) {
            console_putc(digits[--count]);
        }
        console_puts("  ");
        console_puts(names[p->state]);
        console_puts("  ");
        console_puts(p->name);
        console_putc('\n');
    }
    if (!any) {
        console_puts("  no processes\n");
    }
}
