#ifndef ZENKU_LIBC_STDLIB_H
#define ZENKU_LIBC_STDLIB_H

#include <zenku.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

long atol(const char *s);
int atoi(const char *s);

void exit(int status) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));

#endif /* ZENKU_LIBC_STDLIB_H */
