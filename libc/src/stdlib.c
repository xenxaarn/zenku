#include <stdlib.h>
#include <string.h>

#define MALLOC_ALIGN 16
#define MALLOC_MAGIC 0x5a454e4bu

struct malloc_header {
    size_t size;
    unsigned int magic;
    unsigned int reserved;
};

static unsigned char *g_heap_start;
static unsigned char *g_heap_break;
static unsigned char *g_heap_end;

static size_t align_up(size_t value)
{
    return (value + MALLOC_ALIGN - 1) & ~(size_t)(MALLOC_ALIGN - 1);
}

static int heap_ready(void)
{
    if (g_heap_start) {
        return 0;
    }

    g_heap_start = (unsigned char *)(uintptr_t)sys_brk(0);
    if ((long)(intptr_t)g_heap_start <= 0) {
        g_heap_start = 0;
        return -1;
    }
    g_heap_break = g_heap_start;
    g_heap_end = g_heap_start;
    return 0;
}

void *malloc(size_t size)
{
    struct malloc_header *header;
    size_t need;

    if (size == 0) {
        size = 1;
    }
    if (heap_ready() < 0) {
        return 0;
    }

    need = sizeof(*header) + align_up(size);
    if (g_heap_break + need > g_heap_end) {
        unsigned char *want = g_heap_break + need;
        unsigned char *got = (unsigned char *)(uintptr_t)sys_brk(want);

        if (got < want) {
            return 0;
        }
        g_heap_end = got;
    }

    header = (struct malloc_header *)g_heap_break;
    header->size = size;
    header->magic = MALLOC_MAGIC;
    header->reserved = 0;
    g_heap_break += need;
    return header + 1;
}

void *calloc(size_t count, size_t size)
{
    size_t total;
    void *ptr;

    if (count && size > (size_t)-1 / count) {
        return 0;
    }
    total = count * size;
    ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void free(void *ptr)
{
    struct malloc_header *header;

    if (!ptr) {
        return;
    }
    header = (struct malloc_header *)ptr - 1;
    if (header->magic == MALLOC_MAGIC) {
        header->magic = 0;
    }
}

void *realloc(void *ptr, size_t size)
{
    struct malloc_header *header;
    void *next;

    if (!ptr) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return 0;
    }

    header = (struct malloc_header *)ptr - 1;
    if (header->magic != MALLOC_MAGIC || header->size >= size) {
        return ptr;
    }
    next = malloc(size);
    if (!next) {
        return 0;
    }
    memcpy(next, ptr, header->size);
    free(ptr);
    return next;
}

long atol(const char *s)
{
    long value = 0;
    int negative = 0;

    while (*s == ' ' || *s == '\t' || *s == '\n') {
        s++;
    }
    if (*s == '-' || *s == '+') {
        negative = *s == '-';
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (*s - '0');
        s++;
    }
    return negative ? -value : value;
}

int atoi(const char *s)
{
    return (int)atol(s);
}

void exit(int status)
{
    sys_exit(status);
    __builtin_unreachable();
}

void abort(void)
{
    exit(134);
}
