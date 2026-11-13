#ifndef ZENKU_CPU_H
#define ZENKU_CPU_H

#include <types.h>

/* MSRs used by the syscall path and paging. */
#define MSR_EFER            0xC0000080u
#define MSR_STAR            0xC0000081u
#define MSR_LSTAR           0xC0000082u
#define MSR_SFMASK          0xC0000084u
#define MSR_GS_BASE         0xC0000101u
#define MSR_KERNEL_GS_BASE  0xC0000102u

#define EFER_SCE  (1ull << 0)
#define EFER_NXE  (1ull << 11)

#define CR4_SMEP  (1ull << 20)
#define CR4_SMAP  (1ull << 21)

/* Per-CPU data pointed to by the kernel GS base while a syscall runs. */
struct percpu {
    uint64_t kernel_rsp;   /* offset 0: stack top loaded by syscall_entry */
    uint64_t user_rsp;     /* offset 8: user stack saved by syscall_entry */
};

struct cpu_features {
    bool nx;
    bool smep;
    bool smap;
    uint32_t max_basic;
    uint32_t max_extended;
    char vendor[13];
};

extern struct cpu_features g_cpu;

static inline uint64_t cpu_read_cr4(void)
{
    uint64_t value;

    __asm__ volatile("mov %%cr4, %0" : "=r"(value));
    return value;
}

static inline void cpu_write_cr4(uint64_t value)
{
    __asm__ volatile("mov %0, %%cr4" : : "r"(value) : "memory");
}

static inline uint64_t cpu_read_msr(uint32_t msr)
{
    uint32_t low;
    uint32_t high;

    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void cpu_write_msr(uint32_t msr, uint64_t value)
{
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);

    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high) : "memory");
}

void cpu_detect_features(void);
void cpu_enable_features(void);
void cpu_selftest(void);

/* Per-CPU GS state. */
void percpu_init(void);
void percpu_set_kernel_rsp(uint64_t rsp);
void percpu_restore_user(void);

/*
 * SMAP makes supervisor accesses to user pages fault unless EFLAGS.AC is set.
 * These helpers are no-ops on CPUs without SMAP so the kernel remains
 * compatible with older firmware/CPU configurations.
 */
static inline void usercopy_begin(void)
{
    if (g_cpu.smap) {
        __asm__ volatile("stac" : : : "memory");
    }
}

static inline void usercopy_end(void)
{
    if (g_cpu.smap) {
        __asm__ volatile("clac" : : : "memory");
    }
}

#endif /* ZENKU_CPU_H */
