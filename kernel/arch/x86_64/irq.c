#include <arch/x86_64/irq.h>
#include <io.h>
#include <pic.h>
#include <types.h>

static irq_handler_t g_handlers[PIC_IRQ_COUNT];

void irq_init(void)
{
    for (unsigned i = 0; i < PIC_IRQ_COUNT; i++) {
        g_handlers[i] = NULL;
    }
}

int irq_register(uint8_t irq, irq_handler_t handler)
{
    if (irq >= PIC_IRQ_COUNT || !handler) {
        return -1;
    }
    g_handlers[irq] = handler;
    return 0;
}

void irq_unregister(uint8_t irq)
{
    if (irq < PIC_IRQ_COUNT) {
        g_handlers[irq] = NULL;
    }
}

void irq_dispatch(uint8_t irq, struct regs *r)
{
    if (irq >= PIC_IRQ_COUNT) {
        return;
    }

    if (pic_is_spurious(irq)) {
        return;
    }

    if (g_handlers[irq]) {
        g_handlers[irq](r);
    }

    pic_send_eoi(irq);
}
