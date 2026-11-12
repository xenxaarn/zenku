#include <io.h>
#include <pic.h>
#include <types.h>

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define PIC_ICW1_INIT 0x10
#define PIC_ICW1_ICW4 0x01
#define PIC_ICW4_8086 0x01
#define PIC_EOI       0x20

void pic_remap(uint8_t master_offset, uint8_t slave_offset)
{
    uint8_t master_mask = inb(PIC1_DATA);
    uint8_t slave_mask = inb(PIC2_DATA);

    outb(PIC1_CMD, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    io_wait();
    outb(PIC2_CMD, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    io_wait();

    outb(PIC1_DATA, master_offset);
    io_wait();
    outb(PIC2_DATA, slave_offset);
    io_wait();

    outb(PIC1_DATA, 0x04);  /* slave is on IRQ2 */
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();

    outb(PIC1_DATA, PIC_ICW4_8086);
    io_wait();
    outb(PIC2_DATA, PIC_ICW4_8086);
    io_wait();

    outb(PIC1_DATA, master_mask);
    outb(PIC2_DATA, slave_mask);
}

void pic_disable(void)
{
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_mask(uint8_t irq)
{
    uint16_t port;
    uint8_t value;

    if (irq >= PIC_IRQ_COUNT) {
        return;
    }
    port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    value = inb(port) | (uint8_t)(1u << (irq & 7));
    outb(port, value);
}

void pic_unmask(uint8_t irq)
{
    uint16_t port;
    uint8_t value;

    if (irq >= PIC_IRQ_COUNT) {
        return;
    }
    port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    value = inb(port) & (uint8_t)~(1u << (irq & 7));
    outb(port, value);
}

void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);
    }
    outb(PIC1_CMD, PIC_EOI);
}

uint16_t pic_get_isr(void)
{
    outb(PIC1_CMD, 0x0B);
    outb(PIC2_CMD, 0x0B);

    uint16_t isr = (uint16_t)inb(PIC1_CMD) |
                   ((uint16_t)inb(PIC2_CMD) << 8);
    return isr;
}

bool pic_is_spurious(uint8_t irq)
{
    uint16_t isr;

    /* IRQ7 and IRQ15 can be spurious; only trust them if the ISR bit is set. */
    if (irq != 7 && irq != 15) {
        return false;
    }
    isr = pic_get_isr();
    return (isr & (1u << irq)) == 0;
}
