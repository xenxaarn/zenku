#include <arch/x86_64/idt.h>
#include <arch/x86_64/irq.h>
#include <io.h>
#include <kio.h>
#include <pic.h>
#include <ps2.h>
#include <serial.h>
#include <types.h>

#define COM1_BASE 0x3F8
#define COM1_IER  (COM1_BASE + 1)

#define UART_IER_RX 0x01

/*
 * Received bytes go through the same queue as the keyboard, so the console
 * sees one input stream regardless of where the keystroke came from. A real
 * terminal sends CR for Enter and DEL for Backspace.
 */
static void serial_rx_irq(struct regs *r)
{
    int c;

    (void)r;
    while ((c = serial_poll_char()) >= 0) {
        if (c == '\r') {
            c = '\n';
        } else if (c == 0x7F) {
            c = '\b';
        }
        keyboard_inject(c);
    }
}

void serial_enable_rx_irq(void)
{
    irq_register(4, serial_rx_irq);
    outb(COM1_IER, UART_IER_RX);
    pic_unmask(4);
}
