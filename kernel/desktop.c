#include <boot_info.h>
#include <console.h>
#include <desktop.h>
#include <fb.h>
#include <kio.h>
#include <mm/pmm.h>
#include <mouse.h>
#include <pit.h>
#include <process.h>
#include <rtc.h>
#include <string.h>
#include <types.h>
#include <version.h>

#define TOPBAR_H    36
#define TASKBAR_H   34
#define TITLE_H     30
#define EDGE_GAP    12
#define WIN_MIN_W   480
#define WIN_MIN_H   260
#define RESIZE_GRIP 18

#define WALLPAPER    0x0A1020
#define WALLPAPER_LOW 0x0C1528
#define TOPBAR_BG    0x101A33
#define TASKBAR_BG   0x0D1526
#define ACCENT       0x2A6FDB
#define ACCENT_SOFT  0x4FC3F7
#define WINDOW_BG    0x0B1220
#define TITLE_ACTIVE 0x16213A
#define TITLE_IDLE   0x101827
#define WINDOW_EDGE  0x1E2A44
#define SHADOW       0x05070C
#define BUTTON_BG    0x16213A
#define BUTTON_ON    0x1E3358
#define TEXT_DIM     0x90A4AE
#define TEXT_BRIGHT  0xF5F7FA
#define TEXT_GREEN   0x81C784
#define TEXT_RED     0xFF7B72

static bool g_ready;
static bool g_open = true;
static bool g_focused = true;

static int g_win_x;
static int g_win_y;
static int g_win_w;
static int g_win_h;
static int g_con_x;
static int g_con_y;
static int g_con_w;
static int g_con_h;

static int g_task_x;
static int g_task_y;
static int g_task_w;
static int g_task_h;

static int g_drag_mode;
static int g_drag_dx;
static int g_drag_dy;
static uint8_t g_last_buttons;
static uint64_t g_last_second;

enum {
    DRAG_NONE = 0,
    DRAG_MOVE,
    DRAG_RESIZE,
};

static void draw_titlebar(void);

static int text_width(const char *s, int scale)
{
    return (int)strlen(s) * 8 * scale;
}

static void text_right(const char *s, int right, int y, int scale,
                       uint32_t fg, uint32_t bg)
{
    fb_text(s, right - text_width(s, scale), y, scale, fg, bg);
}

static char *append_str(char *p, const char *s)
{
    while (*s) {
        *p++ = *s++;
    }
    return p;
}

static char *append_uint(char *p, uint64_t value)
{
    char digits[21];
    int count = 0;

    if (value == 0) {
        *p++ = '0';
        return p;
    }
    while (value && count < (int)sizeof(digits)) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (count) {
        *p++ = digits[--count];
    }
    return p;
}

static char *append_pair(char *p, unsigned value)
{
    *p++ = (char)('0' + (value / 10) % 10);
    *p++ = (char)('0' + value % 10);
    return p;
}

static void format_duration(char *out, uint64_t seconds)
{
    char *p = out;

    p = append_pair(p, (unsigned)((seconds / 3600) % 100));
    *p++ = ':';
    p = append_pair(p, (unsigned)((seconds / 60) % 60));
    *p++ = ':';
    p = append_pair(p, (unsigned)(seconds % 60));
    *p = '\0';
}

static void draw_topbar(void)
{
    int width = (int)fb_width();
    char line[64];
    char *p;
    char clock[9];
    struct rtc_time now;

    fb_fill_rect(0, 0, width, TOPBAR_H, TOPBAR_BG);
    fb_fill_rect(0, TOPBAR_H, width, 2, ACCENT);

    fb_text("ZENKU", 14, 2, 2, TEXT_BRIGHT, TOPBAR_BG);
    fb_text(OS_VERSION_STRING, 108, 10, 1, TEXT_DIM, TOPBAR_BG);

    if (width >= 900) {
        fb_fill_rect(300, 13, 8, 8, TEXT_GREEN);
        fb_text("online", 314, 10, 1, TEXT_DIM, TOPBAR_BG);
    }

    /* Right cluster: memory, task count, uptime, wall clock. */
    p = append_str(line, "mem ");
    p = append_uint(p, pmm_used_frames() * PAGE_SIZE / MB(1));
    *p++ = '/';
    p = append_uint(p, pmm_total_frames() * PAGE_SIZE / MB(1));
    *p++ = 'M';
    *p = '\0';
    text_right(line, width - 380, 10, 1, TEXT_DIM, TOPBAR_BG);

    p = append_str(line, "tasks ");
    p = append_uint(p, (uint64_t)process_count());
    *p = '\0';
    text_right(line, width - 190, 10, 1, TEXT_DIM, TOPBAR_BG);

    p = append_str(line, "up ");
    {
        char duration[12];
        uint64_t seconds = pit_frequency() ?
                           pit_ticks() / pit_frequency() : 0;

        format_duration(duration, seconds);
        p = append_str(p, duration);
    }
    *p = '\0';
    text_right(line, width - 96, 10, 1, TEXT_DIM, TOPBAR_BG);

    if (rtc_read(&now) == 0) {
        rtc_format(&now, clock);
    } else {
        uint64_t seconds = pit_frequency() ?
                           pit_ticks() / pit_frequency() : 0;

        format_duration(clock, seconds);
    }
    text_right(clock, width - 14, 10, 1, TEXT_BRIGHT, TOPBAR_BG);
}

static void draw_taskbar(void)
{
    int width = (int)fb_width();
    int height = (int)fb_height();
    int y = height - TASKBAR_H;
    uint32_t button = (g_open && g_focused) ? BUTTON_ON : BUTTON_BG;

    fb_fill_rect(0, y, width, TASKBAR_H, TASKBAR_BG);
    fb_fill_rect(0, y, width, 1, WINDOW_EDGE);

    g_task_x = 12;
    g_task_y = y + 5;
    g_task_w = 132;
    g_task_h = TASKBAR_H - 10;

    fb_fill_rect(g_task_x, g_task_y, g_task_w, g_task_h, button);
    fb_fill_rect(g_task_x, g_task_y, 3, g_task_h, g_open ? ACCENT_SOFT : ACCENT);
    fb_fill_rect(g_task_x + 16, g_task_y + 8, 12, 2, TEXT_DIM);
    fb_fill_rect(g_task_x + 16, g_task_y + 12, 12, 2, TEXT_DIM);
    fb_fill_rect(g_task_x + 32, g_task_y + 7, 1, 8, TEXT_DIM);
    fb_text("console", g_task_x + 42, g_task_y + 5, 1,
            g_open ? TEXT_BRIGHT : TEXT_DIM, button);

    text_right("drag title bar - corner to resize", width - 14,
               y + 10, 1, 0x607D8B, TASKBAR_BG);
}

static bool in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static bool focus_window(void)
{
    if (g_focused) {
        return false;
    }
    g_focused = true;
    draw_titlebar();
    draw_taskbar();
    return true;
}

static bool in_close_button(int x, int y)
{
    return in_rect(x, y, g_win_x + g_win_w - 28, g_win_y + 7, 18, 18);
}

static bool in_resize_grip(int x, int y)
{
    return x >= g_win_x + g_win_w - RESIZE_GRIP &&
           x <= g_win_x + g_win_w + 2 &&
           y >= g_win_y + g_win_h - RESIZE_GRIP &&
           y <= g_win_y + g_win_h + 2;
}

static void draw_titlebar(void)
{
    uint32_t bg = g_focused ? TITLE_ACTIVE : TITLE_IDLE;
    int x = g_win_x + g_win_w - 26;
    int y = g_win_y + 8;

    mouse_cursor_hide();
    fb_fill_rect(g_win_x + 1, g_win_y + 1, g_win_w - 2, TITLE_H - 1, bg);
    fb_fill_rect(g_win_x, g_win_y, g_win_w, 2,
                 g_focused ? ACCENT : WINDOW_EDGE);
    fb_text("thunder console", g_win_x + 12, g_win_y + 8, 1,
            g_focused ? TEXT_BRIGHT : TEXT_DIM, bg);

    fb_fill_rect(x, y, 14, 14, TEXT_RED);
    for (int i = 0; i < 8; i++) {
        fb_pixel(x + 3 + i, y + 3 + i, 0x2B0B0E);
        fb_pixel(x + 10 - i, y + 3 + i, 0x2B0B0E);
    }
    mouse_cursor_show();
}

static void draw_window_frame(void)
{
    fb_fill_rect(g_win_x + 8, g_win_y + 8, g_win_w, g_win_h, SHADOW);
    fb_fill_rect(g_win_x, g_win_y, g_win_w, g_win_h, WINDOW_BG);
    fb_fill_rect(g_win_x, g_win_y, 1, g_win_h, WINDOW_EDGE);
    fb_fill_rect(g_win_x + g_win_w - 1, g_win_y, 1, g_win_h, WINDOW_EDGE);
    fb_fill_rect(g_win_x, g_win_y + g_win_h - 1, g_win_w, 1, WINDOW_EDGE);
    draw_titlebar();

    /* Resize grip in the bottom-right corner. */
    for (int i = 0; i < 3; i++) {
        fb_fill_rect(g_win_x + g_win_w - 4 - i * 5,
                     g_win_y + g_win_h - 4, 3, 3, ACCENT);
    }
}

static void update_console_geometry(void)
{
    g_con_x = g_win_x + 12;
    g_con_y = g_win_y + TITLE_H + 8;
    g_con_w = g_win_w - 24;
    g_con_h = g_win_h - TITLE_H - 20;
    if (g_con_w < 8) {
        g_con_w = 8;
    }
    if (g_con_h < 8) {
        g_con_h = 8;
    }
}

static void draw_background(void)
{
    int width = (int)fb_width();
    int height = (int)fb_height();
    int horizon = TOPBAR_H + (height - TOPBAR_H) / 2;

    fb_clear(WALLPAPER);
    /* A soft horizon band keeps the desktop from looking like a flat fill. */
    for (int y = horizon; y < height; y++) {
        uint32_t color = WALLPAPER_LOW;

        if (y < horizon + 24) {
            color = 0x0B1325;
        }
        fb_fill_rect(0, y, width, 1, color);
    }
}

static void draw_static_desktop(void)
{
    draw_background();
    draw_topbar();
    draw_taskbar();
    if (g_open) {
        draw_window_frame();
    }
}

static void redraw_desktop(void)
{
    if (!g_ready) {
        return;
    }

    mouse_cursor_hide();
    draw_static_desktop();
    if (g_open) {
        update_console_geometry();
        console_resize(g_con_x, g_con_y, g_con_w, g_con_h);
    }
    mouse_cursor_show();
}

void desktop_init(const struct boot_info *bi)
{
    int width = (int)fb_width();
    int height = (int)fb_height();

    if (!fb_ready() || !(bi->flags & BOOT_FLAG_FONT)) {
        kprintf("desktop: framebuffer or font unavailable\n");
        return;
    }

    g_win_w = width >= 900 ? width - 280 : width - 96;
    g_win_h = height >= 700 ? height - 230 : height - TOPBAR_H - 30 -
              TASKBAR_H - 12;
    g_win_x = (width - g_win_w) / 2;
    g_win_y = TOPBAR_H + 30;
    update_console_geometry();

    draw_static_desktop();

    console_init(g_con_x, g_con_y, g_con_w, g_con_h, 1);
    console_puts("Zenku 0.0.1 (Thunder) - higher-half kernel\n");
    console_puts("memory managers online, PS/2 keyboard and mouse ready\n");
    console_puts("drag the title bar, resize from the corner, click console\n");
    console_puts("type 'help' for commands, PgUp/PgDn to scroll\n\n");
    console_prompt();

    g_ready = true;
    g_last_second = 0;

    mouse_init();
    kprintf("desktop: terminal ready (%dx%d cells), mouse ready\n",
            console_columns(), console_rows());
}

void desktop_tick(void)
{
    uint64_t seconds;

    if (!g_ready) {
        return;
    }

    seconds = pit_ticks() / pit_frequency();
    if (seconds == g_last_second) {
        return;
    }
    g_last_second = seconds;

    mouse_cursor_hide();
    draw_topbar();
    mouse_cursor_show();
}

bool desktop_update(int x, int y, uint8_t buttons)
{
    bool changed = false;
    bool down = (buttons & 0x01) != 0;

    if (!g_ready) {
        g_last_buttons = buttons;
        return false;
    }

    if (!down && (g_last_buttons & 0x01)) {
        g_drag_mode = DRAG_NONE;
    }

    if (down && !(g_last_buttons & 0x01)) {
        bool taskbar = in_rect(x, y, g_task_x, g_task_y, g_task_w, g_task_h);
        bool in_window = g_open &&
                         in_rect(x, y, g_win_x, g_win_y, g_win_w, g_win_h);

        if (taskbar) {
            if (!g_open) {
                g_open = true;
                g_focused = true;
                update_console_geometry();
                redraw_desktop();
                changed = true;
            } else if (focus_window()) {
                changed = true;
            }
        } else if (g_open && in_close_button(x, y)) {
            g_open = false;
            g_focused = false;
            g_drag_mode = DRAG_NONE;
            redraw_desktop();
            changed = true;
        } else if (g_open && in_resize_grip(x, y)) {
            if (focus_window()) {
                changed = true;
            }
            g_drag_mode = DRAG_RESIZE;
        } else if (in_window) {
            if (focus_window()) {
                changed = true;
            }
            if (y < g_win_y + TITLE_H) {
                g_drag_mode = DRAG_MOVE;
                g_drag_dx = x - g_win_x;
                g_drag_dy = y - g_win_y;
            }
        } else if (g_focused) {
            g_focused = false;
            draw_titlebar();
            draw_taskbar();
            changed = true;
        }
    }

    if (g_drag_mode != DRAG_NONE && down) {
        if (g_drag_mode == DRAG_MOVE) {
            int nx = x - g_drag_dx;
            int ny = y - g_drag_dy;
            int max_x = (int)fb_width() - g_win_w - 4;
            int max_y = (int)fb_height() - TASKBAR_H - g_win_h - 4;

            if (nx < 4) {
                nx = 4;
            }
            if (ny < TOPBAR_H + 4) {
                ny = TOPBAR_H + 4;
            }
            if (nx > max_x) {
                nx = max_x;
            }
            if (ny > max_y) {
                ny = max_y;
            }
            if (nx != g_win_x || ny != g_win_y) {
                g_win_x = nx;
                g_win_y = ny;
                update_console_geometry();
                redraw_desktop();
                changed = true;
            }
        } else if (g_drag_mode == DRAG_RESIZE) {
            int nw = x - g_win_x + 4;
            int nh = y - g_win_y + 4;
            int max_w = (int)fb_width() - g_win_x - 4;
            int max_h = (int)fb_height() - TASKBAR_H - g_win_y - 4;

            if (nw < WIN_MIN_W) {
                nw = WIN_MIN_W;
            }
            if (nh < WIN_MIN_H) {
                nh = WIN_MIN_H;
            }
            if (nw > max_w) {
                nw = max_w;
            }
            if (nh > max_h) {
                nh = max_h;
            }
            if (nw != g_win_w || nh != g_win_h) {
                g_win_w = nw;
                g_win_h = nh;
                update_console_geometry();
                redraw_desktop();
                changed = true;
            }
        }
    }

    g_last_buttons = buttons;
    return changed;
}

bool desktop_ready(void)
{
    return g_ready;
}
