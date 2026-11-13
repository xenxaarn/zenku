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

/* Move the terminal without discarding its scrollback. */
void console_set_origin(int x, int y);
/* Re-render the scrollback and, when appropriate, the prompt cursor. */
void console_repaint(void);
/* Resize and reflow the terminal, preserving history. */
void console_resize(int x, int y, int width, int height);

/* Route typed input to a user process instead of the kernel command shell.
 * The process's terminal read consumes whole lines queued by console_input. */
void console_set_user_input(bool enabled);
bool console_user_input(void);

int console_columns(void);
int console_rows(void);

#endif /* ZENKU_CONSOLE_H */
