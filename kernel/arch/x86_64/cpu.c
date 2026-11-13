#include <arch/x86_64/cpu.h>
#include <kio.h>
#include <kpanic.h>
#include <string.h>
#include <types.h>

struct cpu_features g_cpu;

static struct percpu g_percpu ALIGNED(64);

static inline void cpuid(uint32_t leaf, uint32_t sub, uint32_t *a,
                         uint32_t *b, uint32_t *c, uint32_t *d)
{
    __asm__ volatile("cpuid"
                     : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf), "c"(sub));
}

static bool cpu_has_bit(uint32_t leaf, uint32_t sub, uint32_t reg, uint32_t bit)
{
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t value;

    cpuid(leaf, sub, &a, &b, &c, &d);
    value = reg == 0 ? a : reg == 1 ? b : reg == 2 ? c : d;
    return (value >> bit) & 1u;
}

void cpu_detect_features(void)
{
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;

    memset(&g_cpu, 0, sizeof(g_cpu));

    cpuid(0, 0, &a, &b, &c, &d);
    g_cpu.max_basic = a;
    memcpy(g_cpu.vendor + 0, &b, 4);
    memcpy(g_cpu.vendor + 4, &d, 4);
    memcpy(g_cpu.vendor + 8, &c, 4);
    g_cpu.vendor[12] = '\0';

    cpuid(0x80000000u, 0, &a, &b, &c, &d);
    g_cpu.max_extended = a;

    if (g_cpu.max_extended >= 0x80000001u) {
        g_cpu.nx = cpu_has_bit(0x80000001u, 0, 3, 20);
    }
    if (g_cpu.max_basic >= 7u) {
        g_cpu.smep = cpu_has_bit(7u, 0, 1, 7);
        g_cpu.smap = cpu_has_bit(7u, 0, 1, 20);
    }

    kprintf("cpu: %s, nx %s smep %s smap %s\n", g_cpu.vendor,
            g_cpu.nx ? "yes" : "no", g_cpu.smep ? "yes" : "no",
            g_cpu.smap ? "yes" : "no");
}

void cpu_enable_features(void)
{
    uint64_t efer;
    uint64_t cr4;

    if (g_cpu.nx) {
        efer = cpu_read_msr(MSR_EFER);
        efer |= EFER_NXE;
        cpu_write_msr(MSR_EFER, efer);
    }

    cr4 = cpu_read_cr4();
    if (g_cpu.smep) {
        cr4 |= CR4_SMEP;
    }
    if (g_cpu.smap) {
        cr4 |= CR4_SMAP;
    }
    cpu_write_cr4(cr4);

    /* Keep AC clear on every kernel entry; SMAP must never leak across the
     * user/kernel boundary. */
    if (g_cpu.smap) {
        __asm__ volatile("clac" : : : "memory");
    }
}

void cpu_selftest(void)
{
    uint64_t efer = cpu_read_msr(MSR_EFER);
    uint64_t cr4 = cpu_read_cr4();

    if (g_cpu.nx && !(efer & EFER_NXE)) {
        kpanic("cpu: NX is supported but EFER.NXE is clear");
    }
    if (g_cpu.smep && !(cr4 & CR4_SMEP)) {
        kpanic("cpu: SMEP is supported but CR4.SMEP is clear");
    }
    if (g_cpu.smap && !(cr4 & CR4_SMAP)) {
        kpanic("cpu: SMAP is supported but CR4.SMAP is clear");
    }

    kprintf("cpu: hardening active (nx %u smep %u smap %u)\n",
            g_cpu.nx ? 1u : 0u, g_cpu.smep ? 1u : 0u, g_cpu.smap ? 1u : 0u);
}

void percpu_init(void)
{
    memset(&g_percpu, 0, sizeof(g_percpu));
    percpu_restore_user();
}

void percpu_set_kernel_rsp(uint64_t rsp)
{
    g_percpu.kernel_rsp = rsp;
}

void percpu_restore_user(void)
{
    /* User mode runs with GS.base = 0; the kernel GS base holds the per-CPU
     * block so syscall_entry's swapgs can recover it. */
    cpu_write_msr(MSR_GS_BASE, 0);
    cpu_write_msr(MSR_KERNEL_GS_BASE, (uint64_t)(uintptr_t)&g_percpu);
}
