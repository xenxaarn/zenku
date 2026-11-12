#include <console.h>
#include <desktop.h>
#include <fb.h>
#include <idle.h>
#include <kio.h>
#include <mouse.h>
#include <ps2.h>
#include <serial.h>
#include <types.h>

void idle_poll(void)
{
    static bool mouse_reported;
    static uint8_t last_buttons;
    uint8_t buttons;
    int key;

    __asm__ volatile("sti; hlt" : : : "memory");

    mouse_poll();
    buttons = mouse_buttons();
    if ((buttons & 0x01) && !(last_buttons & 0x01)) {
        desktop_click(mouse_x(), mouse_y(), buttons);
    }
    last_buttons = buttons;
    desktop_tick();
    if (!mouse_reported && mouse_packets() > 0) {
        kprintf("mouse: pointer active (%llu packets)\n", mouse_packets());
        mouse_reported = true;
    }

    while ((key = keyboard_pop()) >= 0) {
        console_input(key);
    }
    key = serial_poll_char();
    if (key >= 0) {
        console_input(key);
    }

    fb_present();
}
