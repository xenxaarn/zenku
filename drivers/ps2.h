#ifndef ZENKU_PS2_H
#define ZENKU_PS2_H

#include <types.h>

/* Special key codes returned alongside ASCII. */
#define KEY_UP     0x100
#define KEY_DOWN   0x101
#define KEY_LEFT   0x102
#define KEY_RIGHT  0x103
#define KEY_HOME   0x104
#define KEY_END    0x105
#define KEY_DELETE 0x106
#define KEY_INSERT 0x107
#define KEY_PGUP   0x108
#define KEY_PGDN   0x109
#define KEY_F1     0x110

void ps2_init(void);
bool keyboard_has_key(void);
int keyboard_pop(void);

/* Queue a byte as if it came from the keyboard (used by serial RX). */
void keyboard_inject(int key);

#endif /* ZENKU_PS2_H */
