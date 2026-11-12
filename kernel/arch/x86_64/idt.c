#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/irq.h>
#include <compiler.h>
#include <kpanic.h>
#include <kio.h>
#include <mm/vmm.h>
#include <string.h>
#include <syscall.h>
#include <types.h>

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} PACKED;

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} PACKED;

static struct idt_entry g_idt[IDT_VECTORS];
static struct idt_ptr g_idtr;

static const char *const g_exception_names[IDT_EXCEPTIONS] = {
    "divide error",
    "debug",
    "non-maskable interrupt",
    "breakpoint",
    "overflow",
    "bound range exceeded",
    "invalid opcode",
    "device not available",
    "double fault",
    "coprocessor segment overrun",
    "invalid TSS",
    "segment not present",
    "stack-segment fault",
    "general protection fault",
    "page fault",
    "reserved",
    "x87 floating-point exception",
    "alignment check",
    "machine check",
    "SIMD floating-point exception",
    "virtualization exception",
    "control protection exception",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "hypervisor injection exception",
    "VMM communication exception",
    "security exception",
    "reserved",
};

static void set_gate_type(unsigned vector, void (*handler)(void), uint8_t ist,
                          uint8_t type)
{
    uint64_t address = (uint64_t)(uintptr_t)handler;

    g_idt[vector].offset_low = (uint16_t)(address & 0xFFFF);
    g_idt[vector].selector = GDT_KERNEL_CODE;
    g_idt[vector].ist = (uint8_t)(ist & 0x07);
    g_idt[vector].type = type;
    g_idt[vector].offset_mid = (uint16_t)((address >> 16) & 0xFFFF);
    g_idt[vector].offset_high = (uint32_t)(address >> 32);
    g_idt[vector].reserved = 0;
}

static void set_gate(unsigned vector, void (*handler)(void), uint8_t ist)
{
    /* Present, DPL 0, 64-bit interrupt gate. */
    set_gate_type(vector, handler, ist, 0x8E);
}

void idt_init(void)
{
    /* Every vector starts as a diagnostic trap; known ones get real stubs. */
    for (unsigned i = 0; i < IDT_VECTORS; i++) {
        set_gate(i, isr_default, 0);
    }
    for (unsigned i = 0; i < IDT_EXCEPTIONS + IDT_IRQ_COUNT; i++) {
        set_gate(i, isr_stub_table[i], 0);
    }

    /* Double fault gets its own IST stack so a broken kernel stack is safe. */
    set_gate(8, isr_stub_table[8], 1);

    /* int 0x80: present, DPL 3, 64-bit interrupt gate for user syscalls. */
    set_gate_type(0x80, isr128, 0, 0xEE);

    g_idtr.limit = sizeof(g_idt) - 1;
    g_idtr.base = (uint64_t)(uintptr_t)&g_idt;
    __asm__ volatile("lidt %0" : : "m"(g_idtr));
}

static void dump_frame(struct regs *r)
{
    uint64_t cr0;
    uint64_t cr2;
    uint64_t cr3;
    uint64_t cr4;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));

    kprintf("  vector: %llu error: 0x%llx\n", r->vector, r->error);
    kprintf("  rip: 0x%llx cs: 0x%llx rflags: 0x%llx\n",
            r->rip, r->cs, r->rflags);
    kprintf("  rsp: 0x%llx ss: 0x%llx\n", r->rsp, r->ss);
    kprintf("  rax: 0x%llx rbx: 0x%llx rcx: 0x%llx rdx: 0x%llx\n",
            r->rax, r->rbx, r->rcx, r->rdx);
    kprintf("  rsi: 0x%llx rdi: 0x%llx rbp: 0x%llx\n",
            r->rsi, r->rdi, r->rbp);
    kprintf("  r8: 0x%llx r9: 0x%llx r10: 0x%llx r11: 0x%llx\n",
            r->r8, r->r9, r->r10, r->r11);
    kprintf("  r12: 0x%llx r13: 0x%llx r14: 0x%llx r15: 0x%llx\n",
            r->r12, r->r13, r->r14, r->r15);
    kprintf("  cr0: 0x%llx cr2: 0x%llx cr3: 0x%llx cr4: 0x%llx\n",
            cr0, cr2, cr3, cr4);
}

static void exception_panic(struct regs *r)
{
    const char *name = "unknown";

    if (r->vector < IDT_EXCEPTIONS) {
        name = g_exception_names[r->vector];
    }

    kprintf("\nEXCEPTION %llu: %s\n", r->vector, name);
    dump_frame(r);
    kpanic("unhandled exception %llu (%s)", r->vector, name);
}

void isr_dispatch(struct regs *r)
{
    if (r->vector == SYSCALL_VECTOR) {
        syscall_dispatch(r);
        return;
    }
    if (r->vector < IDT_EXCEPTIONS) {
        if (r->vector == 14) {
            uint64_t cr2;

            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            if (vmm_cow_fault(cr2, r->error)) {
                return;
            }
        }
        exception_panic(r);
        return;
    }
    if (r->vector >= IDT_IRQ_BASE &&
        r->vector < IDT_IRQ_BASE + IDT_IRQ_COUNT) {
        irq_dispatch((uint8_t)(r->vector - IDT_IRQ_BASE), r);
        return;
    }

    kprintf("\nINTERRUPT %llu: no handler\n", r->vector);
    dump_frame(r);
    kpanic("unexpected interrupt %llu", r->vector);
}
