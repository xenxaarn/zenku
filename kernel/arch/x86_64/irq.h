#ifndef ZENKU_IRQ_H
#define ZENKU_IRQ_H

#include <arch/x86_64/idt.h>
#include <types.h>

typedef void (*irq_handler_t)(struct regs *r);

void irq_init(void);
int irq_register(uint8_t irq, irq_handler_t handler);
void irq_unregister(uint8_t irq);
void irq_dispatch(uint8_t irq, struct regs *r);

#endif /* ZENKU_IRQ_H */
