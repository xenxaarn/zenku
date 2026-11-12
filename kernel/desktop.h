#ifndef ZENKU_DESKTOP_H
#define ZENKU_DESKTOP_H

#include <boot_info.h>

void desktop_init(const struct boot_info *bi);
bool desktop_ready(void);

/* Repaint the taskbar clock when the second changes. */
void desktop_tick(void);

#endif /* ZENKU_DESKTOP_H */
