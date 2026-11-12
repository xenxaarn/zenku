#include <string.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else if (d > s) {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;

    while (n--) {
        *d++ = (unsigned char)c;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *pa = a;
    const unsigned char *pb = b;

    while (n--) {
        if (*pa != *pb) {
            return (int)*pa - (int)*pb;
        }
        pa++;
        pb++;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;

    while (n--) {
        if (*p == (unsigned char)c) {
            return (void *)p;
        }
        p++;
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;

    while (s[n]) {
        n++;
    }
    return n;
}

size_t strnlen(const char *s, size_t max)
{
    size_t n = 0;

    while (n < max && s[n]) {
        n++;
    }
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *dst, const char *src)
{
    char *out = dst;

    while ((*dst++ = *src++)) {
    }
    return out;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    char *out = dst;

    while (n && *src) {
        *dst++ = *src++;
        n--;
    }
    while (n--) {
        *dst++ = '\0';
    }
    return out;
}

char *strcat(char *dst, const char *src)
{
    char *out = dst;

    while (*dst) {
        dst++;
    }
    while ((*dst++ = *src++)) {
    }
    return out;
}

char *strncat(char *dst, const char *src, size_t n)
{
    char *out = dst;

    while (*dst) {
        dst++;
    }
    while (n-- && *src) {
        *dst++ = *src++;
    }
    *dst = '\0';
    return out;
}

char *strchr(const char *s, int c)
{
    char target = (char)c;

    for (;;) {
        if (*s == target) {
            return (char *)s;
        }
        if (!*s) {
            return 0;
        }
        s++;
    }
}

char *strrchr(const char *s, int c)
{
    const char *last = 0;
    char target = (char)c;

    for (;;) {
        if (*s == target) {
            last = s;
        }
        if (!*s) {
            return (char *)last;
        }
        s++;
    }
}

char *strstr(const char *haystack, const char *needle)
{
    size_t n = strlen(needle);

    if (n == 0) {
        return (char *)haystack;
    }
    for (; *haystack; haystack++) {
        if (*haystack == *needle && strncmp(haystack, needle, n) == 0) {
            return (char *)haystack;
        }
    }
    return 0;
}
