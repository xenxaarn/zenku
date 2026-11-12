#include <arch/x86_64/gdt.h>
#include <string.h>

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} PACKED;

struct tss_entry_high {
    uint32_t base_high;
    uint32_t reserved;
} PACKED;

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} PACKED;

extern void gdt_load(const struct gdt_ptr *gdtr);
extern void tss_load(uint16_t selector);

static struct gdt_entry g_gdt[7];
static struct tss g_tss;
static struct gdt_ptr g_gdtr;

static uint8_t g_double_fault_stack[8192] ALIGNED(16);

static void set_entry(unsigned index, uint32_t base, uint32_t limit,
                      uint8_t access, uint8_t granularity)
{
    g_gdt[index].limit_low = (uint16_t)(limit & 0xFFFF);
    g_gdt[index].base_low = (uint16_t)(base & 0xFFFF);
    g_gdt[index].base_mid = (uint8_t)((base >> 16) & 0xFF);
    g_gdt[index].access = access;
    g_gdt[index].granularity =
        (uint8_t)((granularity & 0xF0) | ((limit >> 16) & 0x0F));
    g_gdt[index].base_high = (uint8_t)((base >> 24) & 0xFF);
}

static void set_tss_entry(unsigned index, uint64_t base, uint32_t limit)
{
    struct tss_entry_high *high;

    set_entry(index, (uint32_t)base, limit, 0x89, 0x00);

    /* The upper half of the TSS base lives in the following descriptor slot. */
    high = (struct tss_entry_high *)&g_gdt[index + 1];
    high->base_high = (uint32_t)(base >> 32);
    high->reserved = 0;
}

void tss_set_rsp0(uint64_t rsp)
{
    g_tss.rsp0 = rsp;
}

uint64_t tss_get_rsp0(void)
{
    return g_tss.rsp0;
}

void gdt_init(uint64_t kernel_stack_top)
{
    memset(g_gdt, 0, sizeof(g_gdt));
    memset(&g_tss, 0, sizeof(g_tss));

    set_entry(0, 0, 0, 0, 0);                    /* null */
    set_entry(1, 0, 0xFFFFF, 0x9A, 0x20);        /* kernel code, L=1 */
    set_entry(2, 0, 0xFFFFF, 0x92, 0x00);        /* kernel data */
    set_entry(3, 0, 0xFFFFF, 0xFA, 0x20);        /* user code, L=1 */
    set_entry(4, 0, 0xFFFFF, 0xF2, 0x00);        /* user data */
    set_tss_entry(5, (uint64_t)(uintptr_t)&g_tss, sizeof(g_tss) - 1);

    g_tss.rsp0 = kernel_stack_top;
    g_tss.ist[0] = (uint64_t)(uintptr_t)(g_double_fault_stack +
                                         sizeof(g_double_fault_stack));
    g_tss.iomap_base = sizeof(g_tss);

    g_gdtr.limit = sizeof(g_gdt) - 1;
    g_gdtr.base = (uint64_t)(uintptr_t)&g_gdt;

    gdt_load(&g_gdtr);
    tss_load(GDT_TSS);
}
