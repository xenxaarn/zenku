#ifndef ZENKU_DESKTOP_H
#define ZENKU_DESKTOP_H

#include <boot_info.h>
#include <types.h>

void desktop_init(const struct boot_info *bi);
bool desktop_ready(void);

/* Refresh the top-bar clock and live statistics when the second changes. */
void desktop_tick(void);

/* Handle pointer state: focus, window drag/resize, close, and taskbar clicks.
 * Returns true when the desktop was redrawn. */
bool desktop_update(int x, int y, uint8_t buttons);

#endif /* ZENKU_DESKTOP_H */
