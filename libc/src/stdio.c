#include <stdio.h>
#include <string.h>

struct out_buffer {
    char *buf;
    size_t cap;
    size_t len;
};

static void out_char(struct out_buffer *out, char c)
{
    if (out->len + 1 < out->cap) {
        out->buf[out->len] = c;
    }
    out->len++;
}

static void out_repeat(struct out_buffer *out, char c, size_t count)
{
    while (count--) {
        out_char(out, c);
    }
}

static size_t format_unsigned(char *digits, unsigned long long value,
                              unsigned int base, int upper)
{
    const char *table = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t count = 0;

    if (value == 0) {
        digits[count++] = '0';
        return count;
    }
    while (value) {
        digits[count++] = table[value % base];
        value /= base;
    }
    return count;
}

int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap)
{
    struct out_buffer out = { buf, cap, 0 };

    if (cap) {
        buf[0] = '\0';
    }

    while (*fmt) {
        int left = 0;
        int zero = 0;
        int plus = 0;
        int space = 0;
        int alternate = 0;
        int width = 0;
        int precision = -1;
        int length = 0;
        char conversion;

        if (*fmt != '%') {
            out_char(&out, *fmt++);
            continue;
        }
        fmt++;

        for (;;) {
            if (*fmt == '-') {
                left = 1;
            } else if (*fmt == '0') {
                zero = 1;
            } else if (*fmt == '+') {
                plus = 1;
            } else if (*fmt == ' ') {
                space = 1;
            } else if (*fmt == '#') {
                alternate = 1;
            } else {
                break;
            }
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt++ - '0');
        }
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                precision = precision * 10 + (*fmt++ - '0');
            }
        }
        if (*fmt == 'l') {
            fmt++;
            length = 1;
            if (*fmt == 'l') {
                fmt++;
                length = 2;
            }
        } else if (*fmt == 'z') {
            fmt++;
            length = 3;
        } else if (*fmt == 'h') {
            fmt++;
            length = -1;
            if (*fmt == 'h') {
                fmt++;
                length = -2;
            }
        }

        conversion = *fmt;
        if (!conversion) {
            break;
        }
        fmt++;

        if (conversion == '%') {
            out_char(&out, '%');
            continue;
        }
        if (conversion == 'c') {
            char c = (char)va_arg(ap, int);
            int pad = width > 1 ? width - 1 : 0;

            if (!left) {
                out_repeat(&out, ' ', (size_t)pad);
            }
            out_char(&out, c);
            if (left) {
                out_repeat(&out, ' ', (size_t)pad);
            }
            continue;
        }
        if (conversion == 's') {
            const char *s = va_arg(ap, const char *);
            size_t len;
            int pad;

            if (!s) {
                s = "(null)";
            }
            len = precision >= 0 ? strnlen(s, (size_t)precision) : strlen(s);
            pad = width > (int)len ? width - (int)len : 0;
            if (!left) {
                out_repeat(&out, ' ', (size_t)pad);
            }
            for (size_t i = 0; i < len; i++) {
                out_char(&out, s[i]);
            }
            if (left) {
                out_repeat(&out, ' ', (size_t)pad);
            }
            continue;
        }

        if (conversion == 'd' || conversion == 'i') {
            long long value;
            unsigned long long magnitude;
            char digits[32];
            size_t count;
            size_t zeros;
            size_t prefix = 0;
            char sign = 0;
            size_t total;

            if (length == 2) {
                value = va_arg(ap, long long);
            } else if (length == 1 || length == 3) {
                value = va_arg(ap, long);
            } else {
                value = va_arg(ap, int);
            }
            if (value < 0) {
                sign = '-';
                magnitude = (unsigned long long)(-(value + 1)) + 1;
            } else {
                magnitude = (unsigned long long)value;
                if (plus) {
                    sign = '+';
                } else if (space) {
                    sign = ' ';
                }
            }
            count = format_unsigned(digits, magnitude, 10, 0);
            zeros = precision > (int)count ? (size_t)(precision - (int)count) : 0;
            if (precision == 0 && magnitude == 0) {
                count = 0;
            }
            prefix = sign ? 1 : 0;
            total = prefix + (zeros > count ? zeros : count);
            if (!left && !zero && width > (int)total) {
                out_repeat(&out, ' ', (size_t)(width - (int)total));
            }
            if (sign) {
                out_char(&out, sign);
            }
            if (zero && precision < 0 && !left && width > (int)total) {
                out_repeat(&out, '0', (size_t)(width - (int)total));
            }
            out_repeat(&out, '0', zeros);
            while (count) {
                out_char(&out, digits[--count]);
            }
            if (left && width > (int)total) {
                out_repeat(&out, ' ', (size_t)(width - (int)total));
            }
            continue;
        }

        if (conversion == 'u' || conversion == 'x' || conversion == 'X' ||
            conversion == 'o' || conversion == 'p') {
            unsigned long long value;
            unsigned int base = 10;
            int upper = 0;
            const char *prefix = "";
            char digits[32];
            size_t count;
            size_t zeros;
            size_t total;

            if (conversion == 'p') {
                value = (unsigned long long)(uintptr_t)va_arg(ap, void *);
                base = 16;
                prefix = "0x";
                if (width == 0) {
                    width = 16;
                    zero = 1;
                }
            } else {
                if (length == 2) {
                    value = va_arg(ap, unsigned long long);
                } else if (length == 1 || length == 3) {
                    value = va_arg(ap, unsigned long);
                } else {
                    value = va_arg(ap, unsigned int);
                }
                if (conversion == 'x' || conversion == 'X') {
                    base = 16;
                    upper = conversion == 'X';
                    if (alternate && value) {
                        prefix = upper ? "0X" : "0x";
                    }
                } else if (conversion == 'o') {
                    base = 8;
                }
            }
            count = format_unsigned(digits, value, base, upper);
            zeros = precision > (int)count ? (size_t)(precision - (int)count) : 0;
            if (precision == 0 && value == 0) {
                count = 0;
            }
            total = strlen(prefix) + (zeros > count ? zeros : count);
            if (!left && !zero && width > (int)total) {
                out_repeat(&out, ' ', (size_t)(width - (int)total));
            }
            for (const char *p = prefix; *p; p++) {
                out_char(&out, *p);
            }
            if (zero && precision < 0 && !left && width > (int)total) {
                out_repeat(&out, '0', (size_t)(width - (int)total));
            }
            out_repeat(&out, '0', zeros);
            while (count) {
                out_char(&out, digits[--count]);
            }
            if (left && width > (int)total) {
                out_repeat(&out, ' ', (size_t)(width - (int)total));
            }
            continue;
        }

        out_char(&out, '%');
        out_char(&out, conversion);
    }

    if (cap) {
        size_t end = out.len < cap - 1 ? out.len : cap - 1;

        buf[end] = '\0';
    }
    return (int)out.len;
}

int snprintf(char *buf, size_t cap, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

int vprintf(const char *fmt, va_list ap)
{
    char buf[1024];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);

    if (n > 0) {
        size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;

        sys_write(1, buf, len);
    }
    return n;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int fputc(int c, int fd)
{
    unsigned char byte = (unsigned char)c;

    if (sys_write(fd, &byte, 1) != 1) {
        return -1;
    }
    return byte;
}

int putchar(int c)
{
    return fputc(c, 1);
}

int fputs(const char *s, int fd)
{
    size_t len = strlen(s);

    if (fd < 0) {
        return (int)len;
    }
    if (sys_write(fd, s, len) != (long)len) {
        return -1;
    }
    return (int)len;
}

int puts(const char *s)
{
    if (fputs(s, 1) < 0 || fputc('\n', 1) < 0) {
        return -1;
    }
    return 0;
}
