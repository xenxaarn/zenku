#include <arch/x86_64/idt.h>
#include <arch/x86_64/irq.h>
#include <io.h>
#include <pic.h>
#include <ps2.h>
#include <types.h>

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_COMMAND 0x64

#define PS2_STATUS_OUTPUT 0x01
#define PS2_STATUS_INPUT  0x02

#define KEY_QUEUE_SIZE 256

static const char g_map_base[128] = {
    [0x01] = 27,  [0x02] = '1', [0x03] = '2', [0x04] = '3',
    [0x05] = '4', [0x06] = '5', [0x07] = '6', [0x08] = '7',
    [0x09] = '8', [0x0A] = '9', [0x0B] = '0', [0x0C] = '-',
    [0x0D] = '=', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
    [0x1C] = '\n', [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd',
    [0x21] = 'f', [0x22] = 'g', [0x23] = 'h', [0x24] = 'j',
    [0x25] = 'k', [0x26] = 'l', [0x27] = ';', [0x28] = '\'',
    [0x29] = '`', [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x',
    [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n',
    [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x37] = '*', [0x39] = ' ',
};

static const char g_map_shift[128] = {
    [0x01] = 27,  [0x02] = '!', [0x03] = '@', [0x04] = '#',
    [0x05] = '$', [0x06] = '%', [0x07] = '^', [0x08] = '&',
    [0x09] = '*', [0x0A] = '(', [0x0B] = ')', [0x0C] = '_',
    [0x0D] = '+', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
    [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
    [0x1C] = '\n', [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D',
    [0x21] = 'F', [0x22] = 'G', [0x23] = 'H', [0x24] = 'J',
    [0x25] = 'K', [0x26] = 'L', [0x27] = ':', [0x28] = '"',
    [0x29] = '~', [0x2B] = '|', [0x2C] = 'Z', [0x2D] = 'X',
    [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N',
    [0x32] = 'M', [0x33] = '<', [0x34] = '>', [0x35] = '?',
    [0x37] = '*', [0x39] = ' ',
};

static volatile int g_queue[KEY_QUEUE_SIZE];
static volatile uint8_t g_head;
static volatile uint8_t g_tail;

static bool g_shift;
static bool g_ctrl;
static bool g_alt;
static bool g_caps;
static bool g_extended;

static bool ps2_wait_write(void)
{
    for (unsigned i = 0; i < 100000; i++) {
        if (!(inb(PS2_STATUS) & PS2_STATUS_INPUT)) {
            return true;
        }
    }
    return false;
}

static bool ps2_wait_read(void)
{
    for (unsigned i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS) & PS2_STATUS_OUTPUT) {
            return true;
        }
    }
    return false;
}

static void ps2_write(uint8_t value)
{
    ps2_wait_write();
    outb(PS2_DATA, value);
}

static void ps2_command(uint8_t command)
{
    ps2_wait_write();
    outb(PS2_COMMAND, command);
}

static void queue_push(int key)
{
    uint8_t next = (uint8_t)((g_head + 1) % KEY_QUEUE_SIZE);

    if (next == g_tail) {
        return; /* drop when full */
    }
    g_queue[g_head] = key;
    g_head = next;
}

void keyboard_inject(int key)
{
    uint64_t flags;

    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    queue_push(key);
    if (flags & (1ull << 9)) {
        __asm__ volatile("sti" : : : "memory");
    }
}

bool keyboard_has_key(void)
{
    return g_head != g_tail;
}

int keyboard_pop(void)
{
    int key;
    uint64_t flags;

    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    if (g_head == g_tail) {
        key = -1;
    } else {
        key = g_queue[g_tail];
        g_tail = (uint8_t)((g_tail + 1) % KEY_QUEUE_SIZE);
    }
    if (flags & (1ull << 9)) {
        __asm__ volatile("sti" : : : "memory");
    }
    return key;
}

static int extended_key(uint8_t code)
{
    switch (code & 0x7F) {
    case 0x48: return KEY_UP;
    case 0x50: return KEY_DOWN;
    case 0x4B: return KEY_LEFT;
    case 0x4D: return KEY_RIGHT;
    case 0x47: return KEY_HOME;
    case 0x4F: return KEY_END;
    case 0x53: return KEY_DELETE;
    case 0x52: return KEY_INSERT;
    case 0x49: return KEY_PGUP;
    case 0x51: return KEY_PGDN;
    default: return -1;
    }
}

static int translate(uint8_t scancode)
{
    uint8_t code = scancode & 0x7F;
    bool released = (scancode & 0x80) != 0;
    char ch;

    if (g_extended) {
        g_extended = false;
        if (code == 0x1D) {
            g_ctrl = !released;
            return -1;
        }
        if (code == 0x38) {
            g_alt = !released;
            return -1;
        }
        return released ? -1 : extended_key(code);
    }

    if (code == 0x2A || code == 0x36) {
        g_shift = !released;
        return -1;
    }
    if (code == 0x1D) {
        g_ctrl = !released;
        return -1;
    }
    if (code == 0x38) {
        g_alt = !released;
        return -1;
    }
    if (code == 0x3A) {
        if (!released) {
            g_caps = !g_caps;
        }
        return -1;
    }
    if (released) {
        return -1;
    }

    if (code == 0xE0) {
        g_extended = true;
        return -1;
    }
    if (code >= 128) {
        return -1;
    }

    ch = g_shift ? g_map_shift[code] : g_map_base[code];
    if (ch == 0) {
        return -1;
    }
    if (g_caps && ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - 'a' + 'A');
    }
    if (g_ctrl && ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - 'a' + 1);
    }

    return (unsigned char)ch;
}

static void ps2_irq(struct regs *r)
{
    uint8_t scancode;

    (void)r;
    if (!(inb(PS2_STATUS) & PS2_STATUS_OUTPUT)) {
        return;
    }
    scancode = inb(PS2_DATA);

    /* 0xE0 is the extended-key prefix; feed it through translate(). */
    if (scancode == 0xE0) {
        g_extended = true;
        return;
    }

    int key = translate(scancode);
    if (key >= 0) {
        queue_push(key);
    }
}

void ps2_init(void)
{
    uint8_t config;

    ps2_command(0xAD); /* disable keyboard */
    ps2_command(0xA7); /* disable mouse */

    while (inb(PS2_STATUS) & PS2_STATUS_OUTPUT) {
        (void)inb(PS2_DATA);
    }

    ps2_command(0x20); /* read controller configuration */
    if (ps2_wait_read()) {
        config = inb(PS2_DATA);
    } else {
        config = 0;
    }

    config |= 0x01;   /* enable IRQ1 */
    config |= 0x40;   /* enable scancode translation (set 2 -> set 1) */
    config &= (uint8_t)~0x10;

    ps2_command(0x60); /* write controller configuration */
    ps2_write(config);

    ps2_command(0xAE); /* enable keyboard */

    irq_register(1, ps2_irq);
    pic_unmask(1);
}
