#ifndef ZENKU_LIBC_STDIO_H
#define ZENKU_LIBC_STDIO_H

#include <zenku.h>
#include <stdarg.h>

int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int snprintf(char *buf, size_t cap, const char *fmt, ...);
int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);

int putchar(int c);
int puts(const char *s);
int fputs(const char *s, int fd);
int fputc(int c, int fd);

#endif /* ZENKU_LIBC_STDIO_H */
