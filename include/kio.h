#ifndef ZENKU_KIO_H
#define ZENKU_KIO_H

#include <types.h>
#include <compiler.h>

/* COM1 serial console shared by stage3 and the kernel. */
void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s, size_t n);
void serial_puts(const char *s);

/* Non-blocking COM1 receive; returns -1 when the FIFO is empty. */
int serial_poll_char(void);

/* Minimal formatted output: %c %s %d %i %u %x %X %p, with 0-padding width
 * and the l/ll length modifiers. */
void kprintf(const char *fmt, ...);
void kvprintf(const char *fmt, va_list ap);

#endif /* ZENKU_KIO_H */
