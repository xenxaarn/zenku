#ifndef ZENKU_FB_H
#define ZENKU_FB_H

#include <boot_info.h>
#include <types.h>

int fb_init(const struct boot_info *bi);
bool fb_ready(void);
uint32_t fb_width(void);
uint32_t fb_height(void);

void fb_clear(uint32_t rgb);
void fb_pixel(int x, int y, uint32_t rgb);
void fb_fill_rect(int x, int y, int w, int h, uint32_t rgb);
void fb_scroll_up(int x, int y, int w, int h, int pixels, uint32_t rgb);
uint32_t fb_raw_get(int x, int y);
void fb_raw_set(int x, int y, uint32_t value);
void fb_char(char ch, int x, int y, int scale, uint32_t fg, uint32_t bg);
void fb_text(const char *s, int x, int y, int scale, uint32_t fg, uint32_t bg);

void fb_splash(const struct boot_info *bi);

#endif /* ZENKU_FB_H */
