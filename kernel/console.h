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

/* Route typed input to a user process instead of the kernel command shell.
 * The process's terminal read consumes whole lines queued by console_input. */
void console_set_user_input(bool enabled);
bool console_user_input(void);

int console_columns(void);
int console_rows(void);

#endif /* ZENKU_CONSOLE_H */
