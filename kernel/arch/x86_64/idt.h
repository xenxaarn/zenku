#ifndef ZENKU_IDT_H
#define ZENKU_IDT_H

#include <compiler.h>
#include <types.h>

#define IDT_VECTORS      256
#define IDT_EXCEPTIONS   32
#define IDT_IRQ_BASE     32
#define IDT_IRQ_COUNT    16

/* Stack frame built by isr.asm, in ascending address order. */
struct regs {
    uint64_t r15, r14, r13, r12;
    uint64_t r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx;
    uint64_t rcx, rbx, rax;
    uint64_t vector;
    uint64_t error;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} PACKED;

void idt_init(void);
void isr_dispatch(struct regs *r);

/* Provided by isr.asm. */
extern void isr_default(void);
extern void (*const isr_stub_table[IDT_EXCEPTIONS + IDT_IRQ_COUNT])(void);

#endif /* ZENKU_IDT_H */
