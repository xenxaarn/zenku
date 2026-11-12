#ifndef ZENKU_CONSOLE_H
#define ZENKU_CONSOLE_H

#include <types.h>

void console_init(int x, int y, int width, int height, int scale);
void console_clear(void);
void console_putc(char c);
void console_puts(const char *s);
void console_set_fg(uint32_t rgb);
void console_prompt(void);
void console_input(int key);

int console_columns(void);
int console_rows(void);

#endif /* ZENKU_CONSOLE_H */
