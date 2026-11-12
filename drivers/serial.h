#ifndef ZENKU_SERIAL_H
#define ZENKU_SERIAL_H

/* Switch COM1 from polled receive to IRQ4. Call after the IDT/PIC are up. */
void serial_enable_rx_irq(void);

#endif /* ZENKU_SERIAL_H */
