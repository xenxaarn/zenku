#ifndef ZENKU_MOUSE_H
#define ZENKU_MOUSE_H

#include <types.h>

int mouse_init(void);

/* Apply queued packets and redraw the cursor (main context only). */
void mouse_poll(void);

/* Hide/restore the pixels under the cursor around console drawing. */
void mouse_cursor_hide(void);
void mouse_cursor_show(void);

int mouse_x(void);
int mouse_y(void);
uint64_t mouse_packets(void);
uint8_t mouse_buttons(void);

#endif /* ZENKU_MOUSE_H */
