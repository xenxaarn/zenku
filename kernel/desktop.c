#include <boot_info.h>
#include <console.h>
#include <desktop.h>
#include <fb.h>
#include <kio.h>
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

static void draw_taskbar(int width)
{
    fb_fill_rect(0, 0, width, 36, TASKBAR_BG);
    fb_fill_rect(0, 36, width, 2, ACCENT);

    fb_text("ZENKU", 14, 10, 2, TEXT_BRIGHT, TASKBAR_BG);
    fb_text(OS_VERSION_STRING, 100, 10, 1, TEXT_DIM, TASKBAR_BG);

    fb_text("PIT 100 Hz", width - 300, 10, 1, TEXT_DIM, TASKBAR_BG);
    fb_text("PMM/VMM/heap", width - 200, 10, 1, TEXT_DIM, TASKBAR_BG);

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

    if (!fb_ready() || !(bi->flags & BOOT_FLAG_FONT)) {
        kprintf("desktop: framebuffer or font unavailable\n");
        return;
    }

    fb_clear(WALLPAPER);
    fb_fill_rect(0, height / 2, width, height / 2, 0x0C1528);
    draw_taskbar(width);

    /* Terminal window with a soft shadow and a small title bar. */
    fb_fill_rect(wx + 8, wy + 8, ww, wh, SHADOW);
    fb_fill_rect(wx, wy, ww, wh, WINDOW_BG);
    fb_fill_rect(wx, wy, ww, 2, ACCENT);
    fb_fill_rect(wx, wy + 28, ww, 1, WINDOW_EDGE);
    fb_fill_rect(wx, wy + wh - 1, ww, 1, WINDOW_EDGE);
    fb_fill_rect(wx, wy, 1, wh, WINDOW_EDGE);
    fb_fill_rect(wx + ww - 1, wy, 1, wh, WINDOW_EDGE);

    fb_text("raijin console", wx + 12, wy + 7, 1, TEXT_BRIGHT, WINDOW_BG);

    fb_fill_rect(wx + ww - 66, wy + 10, 9, 9, 0x81C784);
    fb_fill_rect(wx + ww - 48, wy + 10, 9, 9, 0xFFF176);
    fb_fill_rect(wx + ww - 30, wy + 10, 9, 9, 0xFF7B72);

    console_init(cx, cy, ww - 24, wh - 50, 1);
    console_puts("Zenku 0.0.1 (Raijin) - higher-half kernel\n");
    console_puts("memory managers online, PS/2 keyboard ready\n");
    console_puts("type 'help' for commands\n\n");
    console_prompt();

    g_ready = true;
    kprintf("desktop: terminal ready (%dx%d cells)\n",
            console_columns(), console_rows());
}

bool desktop_ready(void)
{
    return g_ready;
}
