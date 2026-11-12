#include <compiler.h>
#include <io.h>
#include <kio.h>
#include <kpanic.h>
#include <types.h>

void kpanic(const char *fmt, ...)
{
    va_list ap;

    irq_disable();
    serial_puts("\nPANIC: ");
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    serial_puts("\n");

    /* Test builds use isa-debug-exit; a panic is exit status 65. */
    outb(0x501, 0x20);
    cpu_halt();
    __builtin_unreachable();
}
