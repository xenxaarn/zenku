#include <boot_info.h>
#include <console.h>
#include <desktop.h>
#include <fb.h>
#include <kio.h>
#include <mouse.h>
#include <pit.h>
#include <types.h>
#include <version.h>

#define WALLPAPER   0x0A1020
#define TASKBAR_BG  0x101A33
#define ACCENT      0x2A6FDB
#define WINDOW_BG   0x0B1220
#define WINDOW_EDGE 0x1E2A44
#define SHADOW      0x05070C
#define TEXT_DIM    0x90A4AE
#define TEXT_BRIGHT 0xF5F7FA

static bool g_ready;
static bool g_focused = true;
static int g_win_x;
static int g_win_y;
static int g_win_w;
static int g_win_h;
static int g_clock_x;
static uint64_t g_last_second;

static void draw_titlebar(void)
{
    mouse_cursor_hide();
    fb_fill_rect(g_win_x, g_win_y, g_win_w, 28, WINDOW_BG);
    fb_fill_rect(g_win_x, g_win_y, g_win_w, 2,
                 g_focused ? ACCENT : WINDOW_EDGE);
    fb_text("thunder console", g_win_x + 12, g_win_y + 7, 1,
            g_focused ? TEXT_BRIGHT : TEXT_DIM, WINDOW_BG);

    fb_fill_rect(g_win_x + g_win_w - 66, g_win_y + 10, 9, 9, 0x81C784);
    fb_fill_rect(g_win_x + g_win_w - 48, g_win_y + 10, 9, 9, 0xFFF176);
    fb_fill_rect(g_win_x + g_win_w - 30, g_win_y + 10, 9, 9, 0xFF7B72);
    mouse_cursor_show();
}

static void draw_taskbar(int width)
{
    fb_fill_rect(0, 0, width, 36, TASKBAR_BG);
    fb_fill_rect(0, 36, width, 2, ACCENT);

    fb_text("ZENKU", 14, 10, 2, TEXT_BRIGHT, TASKBAR_BG);
    fb_text(OS_VERSION_STRING, 100, 10, 1, TEXT_DIM, TASKBAR_BG);

    fb_text("PIT 100 Hz", width - 300, 10, 1, TEXT_DIM, TASKBAR_BG);
    fb_text("00:00", width - 170, 10, 1, TEXT_DIM, TASKBAR_BG);

    fb_fill_rect(width - 100, 10, 10, 10, 0x81C784);
    fb_text("online", width - 84, 10, 1, TEXT_DIM, TASKBAR_BG);
}

void desktop_init(const struct boot_info *bi)
{
    int width = (int)fb_width();
    int height = (int)fb_height();
    int wx = 48;
    int wy = 84;
    int ww = width - 96;
    int wh = height - 132;
    int cx = wx + 12;
    int cy = wy + 38;

    g_win_x = wx;
    g_win_y = wy;
    g_win_w = ww;
    g_win_h = wh;

    if (!fb_ready() || !(bi->flags & BOOT_FLAG_FONT)) {
        kprintf("desktop: framebuffer or font unavailable\n");
        return;
    }

    fb_clear(WALLPAPER);
    fb_fill_rect(0, height / 2, width, height / 2, 0x0C1528);
    draw_taskbar(width);
    g_clock_x = width - 170;

    /* Terminal window with a soft shadow and a small title bar. */
    fb_fill_rect(wx + 8, wy + 8, ww, wh, SHADOW);
    fb_fill_rect(wx, wy, ww, wh, WINDOW_BG);
    fb_fill_rect(wx, wy, ww, 2, ACCENT);
    fb_fill_rect(wx, wy + 28, ww, 1, WINDOW_EDGE);
    fb_fill_rect(wx, wy + wh - 1, ww, 1, WINDOW_EDGE);
    fb_fill_rect(wx, wy, 1, wh, WINDOW_EDGE);
    fb_fill_rect(wx + ww - 1, wy, 1, wh, WINDOW_EDGE);

    draw_titlebar();

    console_init(cx, cy, ww - 24, wh - 50, 1);
    console_puts("Zenku 0.0.1 (Thunder) - higher-half kernel\n");
    console_puts("memory managers online, PS/2 keyboard ready\n");
    console_puts("type 'help' for commands, PgUp/PgDn to scroll\n\n");
    console_prompt();

    g_ready = true;

    mouse_init();
    kprintf("desktop: terminal ready (%dx%d cells), mouse ready\n",
            console_columns(), console_rows());
}

static void format_clock(char *out, uint64_t seconds)
{
    unsigned minutes = (unsigned)((seconds / 60) % 100);
    unsigned secs = (unsigned)(seconds % 60);

    out[0] = (char)('0' + minutes / 10);
    out[1] = (char)('0' + minutes % 10);
    out[2] = ':';
    out[3] = (char)('0' + secs / 10);
    out[4] = (char)('0' + secs % 10);
    out[5] = '\0';
}

void desktop_tick(void)
{
    char clock[8];
    uint64_t seconds;

    if (!g_ready) {
        return;
    }

    seconds = pit_ticks() / pit_frequency();
    if (seconds == g_last_second) {
        return;
    }
    g_last_second = seconds;

    format_clock(clock, seconds);
    mouse_cursor_hide();
    fb_text(clock, g_clock_x, 10, 1, TEXT_DIM, TASKBAR_BG);
    mouse_cursor_show();
}

void desktop_click(int x, int y, uint8_t buttons)
{
    bool inside;

    if (!g_ready || !(buttons & 0x01)) {
        return;
    }

    inside = x >= g_win_x && x < g_win_x + g_win_w &&
             y >= g_win_y && y < g_win_y + g_win_h;
    if (inside != g_focused) {
        g_focused = inside;
        draw_titlebar();
    }
}

bool desktop_ready(void)
{
    return g_ready;
}
