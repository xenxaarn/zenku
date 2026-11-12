#include <arch/x86_64/idt.h>
#include <arch/x86_64/irq.h>
#include <fb.h>
#include <io.h>
#include <kio.h>
#include <mouse.h>
#include <pic.h>
#include <types.h>

#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_COMMAND 0x64

#define PS2_STATUS_OUTPUT 0x01
#define PS2_STATUS_INPUT  0x02
#define PS2_STATUS_AUX    0x20

#define CURSOR_W 12
#define CURSOR_H 18

static const char *const g_sprite[CURSOR_H] = {
    "X           ",
    "XX          ",
    "X.X         ",
    "X..X        ",
    "X...X       ",
    "X....X      ",
    "X.....X     ",
    "X......X    ",
    "X.......X   ",
    "X........X  ",
    "X.....XXXXX ",
    "X..X..X     ",
    "X.X X..X    ",
    "XX  X..X    ",
    "X    X..X   ",
    "     X..X   ",
    "      X..X  ",
    "      XX    ",
};

static uint32_t g_saved[CURSOR_W * CURSOR_H];
static int g_x;
static int g_y;
static bool g_visible;
static bool g_ready;

static volatile int g_dx;
static volatile int g_dy;
static volatile uint64_t g_packets;
static volatile uint8_t g_buttons;

static uint8_t g_packet[3];
static uint8_t g_index;

static void mouse_irq(struct regs *r);

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

static void ps2_flush(void)
{
    while (inb(PS2_STATUS) & PS2_STATUS_OUTPUT) {
        (void)inb(PS2_DATA);
    }
}

static void aux_write(uint8_t value)
{
    ps2_command(0xD4); /* next data byte goes to the auxiliary device */
    ps2_write(value);
}

static uint8_t read_ack(void)
{
    if (ps2_wait_read()) {
        return inb(PS2_DATA);
    }
    return 0;
}

int mouse_init(void)
{
    uint8_t config;
    uint8_t ack1;
    uint8_t ack2;

    ps2_command(0xA8); /* enable auxiliary device */

    /* The keyboard may have left an ACK/self-test byte queued; a stale byte
     * here would be mistaken for the configuration byte. */
    ps2_flush();

    ps2_command(0x20);
    config = ps2_wait_read() ? inb(PS2_DATA) : 0;

    /*
     * A stale byte can be mistaken for the configuration byte. Force the
     * bits the keyboard path depends on instead of trusting the read:
     * IRQ1 + IRQ12 enabled, translation on, both clocks enabled.
     */
    config &= (uint8_t)~(0x10 | 0x20);
    config |= 0x01 | 0x02 | 0x40;
    ps2_command(0x60);
    ps2_write(config);

    aux_write(0xF6); /* set defaults */
    ack1 = read_ack();
    aux_write(0xF4); /* enable data reporting */
    ack2 = read_ack();

    ps2_flush();

    kprintf("mouse: controller config 0x%x, aux acks 0x%x/0x%x\n",
            config, ack1, ack2);

    g_index = 0;
    g_dx = 0;
    g_dy = 0;
    g_packets = 0;
    g_buttons = 0;

    g_x = (int)fb_width() / 2;
    g_y = (int)fb_height() / 2;
    g_ready = fb_ready();
    g_visible = false;

    irq_register(12, mouse_irq);
    pic_unmask(12);
    pic_unmask(2);

    return 0;
}

static void mouse_irq(struct regs *r)
{
    uint8_t status;
    uint8_t byte;
    int dx;
    int dy;

    (void)r;
    status = inb(PS2_STATUS);
    if (!(status & PS2_STATUS_OUTPUT) || !(status & PS2_STATUS_AUX)) {
        return;
    }

    byte = inb(PS2_DATA);

    if (g_index == 0 && !(byte & 0x08)) {
        return; /* lost sync: the first byte always has bit 3 set */
    }

    g_packet[g_index++] = byte;
    if (g_index < 3) {
        return;
    }
    g_index = 0;

    if (g_packet[0] & 0xC0) {
        return; /* X/Y overflow, drop the packet */
    }

    dx = (int8_t)g_packet[1];
    dy = (int8_t)g_packet[2];

    g_dx += dx;
    g_dy -= dy; /* PS/2 Y grows upward, screen Y grows downward */
    g_buttons = g_packet[0] & 0x07;
    g_packets++;
}

void mouse_cursor_hide(void)
{
    if (!g_visible) {
        return;
    }

    for (int row = 0; row < CURSOR_H; row++) {
        for (int col = 0; col < CURSOR_W; col++) {
            fb_raw_set(g_x + col, g_y + row,
                       g_saved[row * CURSOR_W + col]);
        }
    }
    g_visible = false;
}

void mouse_cursor_show(void)
{
    if (!g_ready || g_visible) {
        return;
    }

    for (int row = 0; row < CURSOR_H; row++) {
        for (int col = 0; col < CURSOR_W; col++) {
            char pixel = g_sprite[row][col];
            int x = g_x + col;
            int y = g_y + row;

            g_saved[row * CURSOR_W + col] = fb_raw_get(x, y);
            if (pixel == 'X') {
                fb_raw_set(x, y, 0x000000);
            } else if (pixel == '.') {
                fb_raw_set(x, y, 0xFFFFFF);
            }
        }
    }
    g_visible = true;
}

void mouse_poll(void)
{
    int dx;
    int dy;
    uint64_t flags;

    if (!g_ready) {
        return;
    }

    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    dx = g_dx;
    dy = g_dy;
    g_dx = 0;
    g_dy = 0;
    if (flags & (1ull << 9)) {
        __asm__ volatile("sti" : : : "memory");
    }

    if (dx == 0 && dy == 0) {
        return;
    }

    mouse_cursor_hide();
    g_x += dx;
    g_y += dy;

    if (g_x < 0) {
        g_x = 0;
    }
    if (g_y < 0) {
        g_y = 0;
    }
    if (g_x > (int)fb_width() - CURSOR_W) {
        g_x = (int)fb_width() - CURSOR_W;
    }
    if (g_y > (int)fb_height() - CURSOR_H) {
        g_y = (int)fb_height() - CURSOR_H;
    }

    mouse_cursor_show();
}

int mouse_x(void)
{
    return g_x;
}

int mouse_y(void)
{
    return g_y;
}

uint64_t mouse_packets(void)
{
    return g_packets;
}

uint8_t mouse_buttons(void)
{
    return g_buttons;
}
