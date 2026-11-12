#ifndef ZENKU_PANIC_H
#define ZENKU_PANIC_H

#include <compiler.h>

/* Print a panic message on COM1, signal QEMU (test builds), and halt. */
void kpanic(const char *fmt, ...) NORETURN;

#endif /* ZENKU_PANIC_H */
