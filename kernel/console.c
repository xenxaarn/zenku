#include <console.h>
#include <fb.h>
#include <kio.h>
#include <mm/pmm.h>
#include <mouse.h>
#include <pit.h>
#include <ps2.h>
#include <string.h>
#include <types.h>
#include <version.h>

#define CELL_WIDTH  8
#define CELL_HEIGHT 16

#define COLOR_BG     0x0B1220
#define COLOR_FG     0xD7E1EA
#define COLOR_ACCENT 0x4FC3F7
#define COLOR_ERROR  0xFF7B72

static int g_x;
static int g_y;
static int g_cell_w;
static int g_cell_h;
static int g_scale;
static int g_cols;
static int g_rows;
static int g_col;
static int g_row;
static uint32_t g_fg = COLOR_FG;

static char g_line[128];
static size_t g_line_len;

int console_columns(void)
{
    return g_cols;
}

int console_rows(void)
{
    return g_rows;
}

void console_set_fg(uint32_t rgb)
{
    g_fg = rgb;
}

void console_clear(void)
{
    mouse_cursor_hide();
    fb_fill_rect(g_x, g_y, g_cols * g_cell_w, g_rows * g_cell_h, COLOR_BG);
    g_col = 0;
    g_row = 0;
    g_line_len = 0;
    mouse_cursor_show();
}

void console_init(int x, int y, int width, int height, int scale)
{
    g_x = x;
    g_y = y;
    g_scale = scale > 0 ? scale : 1;
    g_cell_w = CELL_WIDTH * g_scale;
    g_cell_h = CELL_HEIGHT * g_scale;
    g_cols = width / g_cell_w;
    g_rows = height / g_cell_h;
    g_col = 0;
    g_row = 0;
    g_fg = COLOR_FG;
    g_line[0] = '\0';
    g_line_len = 0;

    console_clear();
}

static void scroll(void)
{
    fb_scroll_up(g_x, g_y, g_cols * g_cell_w, g_rows * g_cell_h,
                 g_cell_h, COLOR_BG);
    g_row = g_rows - 1;
}

static void newline(void)
{
    g_col = 0;
    g_row++;
    if (g_row >= g_rows) {
        scroll();
    }
}

static void console_putc_inner(char c)
{
    int px;
    int py;

    if (c == '\n') {
        serial_putc('\n');
        newline();
        return;
    }
    if (c == '\r') {
        g_col = 0;
        return;
    }
    if (c == '\b') {
        if (g_col > 0) {
            g_col--;
            fb_char(' ', g_x + g_col * g_cell_w,
                    g_y + g_row * g_cell_h, g_scale, COLOR_FG, COLOR_BG);
        }
        return;
    }
    if (c == '\t') {
        do {
            console_putc_inner(' ');
        } while (g_col != 0 && (g_col % 4) != 0);
        return;
    }
    if ((unsigned char)c < 32) {
        return;
    }

    if (g_col >= g_cols) {
        newline();
    }

    px = g_x + g_col * g_cell_w;
    py = g_y + g_row * g_cell_h;
    fb_char(c, px, py, g_scale, g_fg, COLOR_BG);
    serial_putc(c);
    g_col++;
}

void console_putc(char c)
{
    mouse_cursor_hide();
    console_putc_inner(c);
    mouse_cursor_show();
}

void console_puts(const char *s)
{
    while (*s) {
        console_putc(*s++);
    }
}

static void put_uint(uint64_t value)
{
    char digits[21];
    unsigned count = 0;

    if (value == 0) {
        console_putc('0');
        return;
    }
    while (value && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (count) {
        console_putc(digits[--count]);
    }
}

void console_prompt(void)
{
    uint32_t saved = g_fg;

    g_fg = COLOR_ACCENT;
    console_puts("zenku> ");
    g_fg = saved;
}

static void command_run(void)
{
    g_line[g_line_len] = '\0';
    console_putc('\n');

    if (g_line_len == 0) {
        console_prompt();
        return;
    }

    if (strcmp(g_line, "help") == 0) {
        console_puts("commands: help, mem, ticks, uptime, version, clear\n");
    } else if (strcmp(g_line, "mem") == 0) {
        console_puts("free frames: ");
        put_uint(pmm_free_frames());
        console_puts(" (");
        put_uint(pmm_free_frames() * PAGE_SIZE / MB(1));
        console_puts(" MiB)\n");
    } else if (strcmp(g_line, "ticks") == 0) {
        console_puts("timer ticks: ");
        put_uint(pit_ticks());
        console_putc('\n');
    } else if (strcmp(g_line, "uptime") == 0) {
        console_puts("uptime: ");
        put_uint(pit_ticks() / pit_frequency());
        console_puts(" s (");
        put_uint(pit_ticks());
        console_puts(" ticks)\n");
    } else if (strcmp(g_line, "version") == 0) {
        console_puts("Zenku 0.0.1 - Thunder, 64-bit higher half\n");
    } else if (strcmp(g_line, "clear") == 0) {
        console_clear();
        console_prompt();
        return;
    } else {
        g_fg = COLOR_ERROR;
        console_puts("unknown command: ");
        console_puts(g_line);
        console_putc('\n');
        g_fg = COLOR_FG;
    }

    console_prompt();
}

void console_input(int key)
{
    if (key < 0 || key >= 0x100) {
        return;
    }

    char c = (char)key;

    if (c == '\r') {
        c = '\n';
    }
    if (c == '\n') {
        command_run();
        g_line_len = 0;
        return;
    }
    if (c == '\b') {
        if (g_line_len > 0) {
            g_line_len--;
            console_putc('\b');
        }
        return;
    }
    if (c == '\t') {
        return;
    }
    if (c < 32 || c > 126) {
        return;
    }
    if (g_line_len + 1 >= sizeof(g_line)) {
        return;
    }

    g_line[g_line_len++] = c;
    console_putc(c);
}
