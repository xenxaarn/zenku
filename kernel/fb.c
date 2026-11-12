#include <fb.h>
#include <string.h>

#define PHYS_MAP_BASE 0xFFFF800000000000ull

static struct {
    uint32_t *addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch_pixels;
    uint8_t bpp;
    uint8_t red_pos;
    uint8_t red_size;
    uint8_t green_pos;
    uint8_t green_size;
    uint8_t blue_pos;
    uint8_t blue_size;
    const uint8_t *font;
    uint32_t font_height;
    bool ready;
} g_fb;

int fb_init(const struct boot_info *bi)
{
    memset(&g_fb, 0, sizeof(g_fb));

    if (!(bi->flags & BOOT_FLAG_FB) || bi->fb_addr == 0) {
        return -1;
    }
    if (bi->fb_bpp != 32 || bi->fb_width == 0 || bi->fb_height == 0 ||
        bi->fb_pitch == 0) {
        return -1;
    }
    if (bi->fb_addr >= 4 * GB(1)) {
        return -1;
    }

    g_fb.addr = (uint32_t *)(uintptr_t)(PHYS_MAP_BASE + bi->fb_addr);
    g_fb.width = bi->fb_width;
    g_fb.height = bi->fb_height;
    g_fb.pitch_pixels = bi->fb_pitch / 4;
    g_fb.bpp = bi->fb_bpp;
    g_fb.red_pos = bi->fb_red_pos;
    g_fb.red_size = bi->fb_red_size;
    g_fb.green_pos = bi->fb_green_pos;
    g_fb.green_size = bi->fb_green_size;
    g_fb.blue_pos = bi->fb_blue_pos;
    g_fb.blue_size = bi->fb_blue_size;

    if ((bi->flags & BOOT_FLAG_FONT) && bi->font_addr && bi->font_height) {
        g_fb.font = (const uint8_t *)(uintptr_t)(PHYS_MAP_BASE + bi->font_addr);
        g_fb.font_height = bi->font_height;
    }

    g_fb.ready = true;
    return 0;
}

bool fb_ready(void)
{
    return g_fb.ready;
}

uint32_t fb_width(void)
{
    return g_fb.width;
}

uint32_t fb_height(void)
{
    return g_fb.height;
}

static uint32_t pack(uint32_t rgb)
{
    uint32_t r = (rgb >> 16) & 0xFF;
    uint32_t g = (rgb >> 8) & 0xFF;
    uint32_t b = rgb & 0xFF;
    uint32_t value = 0;

    if (g_fb.red_size) {
        value |= (r >> (8 - g_fb.red_size)) << g_fb.red_pos;
    }
    if (g_fb.green_size) {
        value |= (g >> (8 - g_fb.green_size)) << g_fb.green_pos;
    }
    if (g_fb.blue_size) {
        value |= (b >> (8 - g_fb.blue_size)) << g_fb.blue_pos;
    }
    return value;
}

void fb_pixel(int x, int y, uint32_t rgb)
{
    if (!g_fb.ready || x < 0 || y < 0 ||
        (uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height) {
        return;
    }
    g_fb.addr[(uint64_t)y * g_fb.pitch_pixels + (uint32_t)x] = pack(rgb);
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t rgb)
{
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            fb_pixel(x + col, y + row, rgb);
        }
    }
}

uint32_t fb_raw_get(int x, int y)
{
    if (!g_fb.ready || x < 0 || y < 0 ||
        (uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height) {
        return 0;
    }
    return g_fb.addr[(uint64_t)y * g_fb.pitch_pixels + (uint32_t)x];
}

void fb_raw_set(int x, int y, uint32_t value)
{
    if (!g_fb.ready || x < 0 || y < 0 ||
        (uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height) {
        return;
    }
    g_fb.addr[(uint64_t)y * g_fb.pitch_pixels + (uint32_t)x] = value;
}

void fb_scroll_up(int x, int y, int w, int h, int pixels, uint32_t rgb)
{
    if (!g_fb.ready || pixels <= 0 || pixels >= h) {
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if ((uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height) {
        return;
    }
    if ((uint32_t)(x + w) > g_fb.width) {
        w = (int)g_fb.width - x;
    }
    if ((uint32_t)(y + h) > g_fb.height) {
        h = (int)g_fb.height - y;
    }

    for (int row = 0; row < h - pixels; row++) {
        uint32_t *dst = g_fb.addr +
                        (uint64_t)(y + row) * g_fb.pitch_pixels + x;
        const uint32_t *src = g_fb.addr +
                              (uint64_t)(y + row + pixels) *
                              g_fb.pitch_pixels + x;

        memmove(dst, src, (size_t)w * sizeof(uint32_t));
    }
    fb_fill_rect(x, y + h - pixels, w, pixels, rgb);
}

void fb_clear(uint32_t rgb)
{
    if (!g_fb.ready) {
        return;
    }
    for (uint32_t y = 0; y < g_fb.height; y++) {
        for (uint32_t x = 0; x < g_fb.width; x++) {
            g_fb.addr[(uint64_t)y * g_fb.pitch_pixels + x] = pack(rgb);
        }
    }
}

void fb_char(char ch, int x, int y, int scale, uint32_t fg, uint32_t bg)
{
    const uint8_t *glyph;
    uint32_t rows;

    if (!g_fb.ready || !g_fb.font || scale <= 0) {
        return;
    }

    rows = g_fb.font_height;
    glyph = g_fb.font + (uint8_t)ch * rows;

    for (uint32_t row = 0; row < rows; row++) {
        uint8_t bits = glyph[row];

        for (int col = 0; col < 8; col++) {
            uint32_t color = (bits & (0x80u >> col)) ? fg : bg;

            if (bg == fg && !(bits & (0x80u >> col))) {
                continue;
            }
            fb_fill_rect(x + col * scale, y + (int)row * scale,
                         scale, scale, color);
        }
    }
}

void fb_text(const char *s, int x, int y, int scale, uint32_t fg, uint32_t bg)
{
    int cursor = x;

    while (*s) {
        fb_char(*s++, cursor, y, scale, fg, bg);
        cursor += 8 * scale;
    }
}

void fb_splash(const struct boot_info *bi)
{
    static const uint32_t bars[] = {
        0xFF5252, 0xFFB74D, 0xFFF176, 0x81C784,
        0x4FC3F7, 0x7986CB, 0xBA68C8, 0xFFFFFF,
    };
    uint32_t bg = 0x0A1020;
    int width = (int)bi->fb_width;
    int height = (int)bi->fb_height;

    fb_clear(bg);

    fb_fill_rect(0, 0, width, 6, 0x2A6FDB);
    fb_fill_rect(0, height - 6, width, 6, 0x2A6FDB);

    fb_text("ZENKU", 64, 72, 6, 0xFFFFFF, bg);
    fb_text("Thunder", 68, 180, 3, 0x4FC3F7, bg);
    fb_text("higher-half kernel running", 68, 236, 2, 0xB0BEC5, bg);

    fb_text("Zenku 0.0.1 - Thunder", 68, 290, 1, 0x90A4AE, bg);

    for (unsigned i = 0; i < ARRAY_SIZE(bars); i++) {
        fb_fill_rect(68 + (int)i * 48, 330, 40, 40, bars[i]);
    }

    fb_text("serial: COM1 115200 8N1", 68, 400, 1, 0x78909C, bg);
}
