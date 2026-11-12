#include <kio.h>
#include <io.h>
#include <compiler.h>
#include <string.h>

#define COM1 0x3F8

static char digits_lower[] = "0123456789abcdef";
static char digits_upper[] = "0123456789ABCDEF";

void serial_init(void)
{
    outb(COM1 + 1, 0x00);   /* disable UART interrupts */
    outb(COM1 + 3, 0x80);   /* DLAB on */
    outb(COM1 + 0, 0x01);   /* divisor 1 -> 115200 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8 data bits, no parity, one stop bit */
    outb(COM1 + 2, 0xC7);   /* enable and clear FIFOs */
    outb(COM1 + 4, 0x0B);   /* DTR, RTS, OUT2 */
}

void serial_putc(char c)
{
    if (c == '\n') {
        serial_putc('\r');
    }

    while (!(inb(COM1 + 5) & 0x20)) {
    }
    outb(COM1, (uint8_t)c);
}

void serial_write(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        serial_putc(s[i]);
    }
}

void serial_puts(const char *s)
{
    while (*s) {
        serial_putc(*s++);
    }
}

struct fmt {
    uint64_t value;
    unsigned base;
    bool upper;
    bool negative;
    int width;
    bool zero_pad;
};

static void emit_number(struct fmt *f)
{
    char buf[24];
    int n = 0;
    uint64_t v = f->value;
    const char *digits = f->upper ? digits_upper : digits_lower;
    int digits_len;

    if (v == 0) {
        buf[n++] = '0';
    }
    while (v) {
        buf[n++] = digits[v % f->base];
        v /= f->base;
    }
    digits_len = n;

    int pad = f->width - digits_len - (f->negative ? 1 : 0);
    if (pad < 0) {
        pad = 0;
    }
    if (f->negative) {
        serial_putc('-');
    }
    if (f->zero_pad) {
        while (pad--) {
            serial_putc('0');
        }
        pad = 0;
    }
    for (int i = 0; i < pad; i++) {
        serial_putc(' ');
    }
    while (n) {
        serial_putc(buf[--n]);
    }
}

static void emit_string(const char *s, int width)
{
    int len = (int)strlen(s);

    for (int i = len; i < width; i++) {
        serial_putc(' ');
    }
    serial_puts(s);
}

void kvprintf(const char *fmt, va_list ap)
{
    while (*fmt) {
        if (*fmt != '%') {
            serial_putc(*fmt++);
            continue;
        }
        fmt++;

        struct fmt f = {
            .value = 0,
            .base = 10,
            .upper = false,
            .negative = false,
            .width = 0,
            .zero_pad = false,
        };
        bool is_signed = false;
        bool is_pointer = false;

        if (*fmt == '0') {
            f.zero_pad = true;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            f.width = f.width * 10 + (*fmt - '0');
            fmt++;
        }

        int longs = 0;
        while (*fmt == 'l') {
            longs++;
            fmt++;
        }

        switch (*fmt) {
        case 'd':
        case 'i':
            is_signed = true;
            break;
        case 'u':
        case 'x':
        case 'X':
        case 'p':
            break;
        case 'c': {
            char c = (char)va_arg(ap, int);
            serial_putc(c);
            fmt++;
            continue;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            emit_string(s ? s : "(null)", f.width);
            fmt++;
            continue;
        }
        case '%':
            serial_putc('%');
            fmt++;
            continue;
        case '\0':
            goto done;
        default:
            serial_putc('%');
            serial_putc(*fmt++);
            continue;
        }

        if (*fmt == 'p') {
            is_pointer = true;
            f.base = 16;
            f.upper = false;
            f.zero_pad = true;
            if (f.width < 16) {
                f.width = 16;
            }
        } else if (*fmt == 'x') {
            f.base = 16;
        } else if (*fmt == 'X') {
            f.base = 16;
            f.upper = true;
        }

        if (is_signed) {
            int64_t sv;
            if (longs >= 2) {
                sv = va_arg(ap, int64_t);
            } else if (longs == 1) {
                sv = va_arg(ap, long);
            } else {
                sv = va_arg(ap, int);
            }
            if (sv < 0) {
                f.negative = true;
                f.value = (uint64_t)(-(sv + 1)) + 1;
            } else {
                f.value = (uint64_t)sv;
            }
        } else if (is_pointer) {
            f.value = (uint64_t)(uintptr_t)va_arg(ap, void *);
        } else if (longs >= 2) {
            f.value = va_arg(ap, uint64_t);
        } else if (longs == 1) {
            f.value = va_arg(ap, unsigned long);
        } else {
            f.value = va_arg(ap, unsigned int);
        }

        emit_number(&f);
        fmt++;
    }

done:
    return;
}

void kprintf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}
