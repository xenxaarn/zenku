#include <console.h>
#include <desktop.h>
#include <fb.h>
#include <idle.h>
#include <kio.h>
#include <mouse.h>
#include <pit.h>
#include <ps2.h>
#include <serial.h>
#include <types.h>

/*
 * The compositor targets 60 frames per second. PIT ticks arrive at 100 Hz, so
 * the accumulator converts tick deltas into frame time instead of assuming an
 * integer number of ticks per frame. Input events bypass the limiter so typing
 * and dragging never feel delayed.
 */
#define COMPOSITOR_HZ 60

void idle_poll(void)
{
    static bool mouse_reported;
    static uint64_t frame_tick;
    static uint64_t frame_accum;
    static uint8_t last_buttons;
    bool activity = false;
    bool due = false;
    uint64_t now;
    uint64_t delta;
    uint32_t frequency;
    uint8_t buttons;
    int before_x;
    int before_y;
    int key;

    __asm__ volatile("sti; hlt" : : : "memory");

    before_x = mouse_x();
    before_y = mouse_y();
    mouse_poll();
    if (mouse_x() != before_x || mouse_y() != before_y) {
        activity = true;
    }

    buttons = mouse_buttons();
    if (desktop_update(mouse_x(), mouse_y(), buttons)) {
        activity = true;
    }
    if (buttons != last_buttons) {
        activity = true;
    }
    last_buttons = buttons;

    if (!mouse_reported && mouse_packets() > 0) {
        kprintf("mouse: pointer active (%llu packets)\n", mouse_packets());
        mouse_reported = true;
    }

    /* Drain input before repainting the status bar so a slow RTC read can
     * never delay a keystroke or a serial byte. */
    while ((key = keyboard_pop()) >= 0) {
        console_input(key);
        activity = true;
    }
    key = serial_poll_char();
    if (key >= 0) {
        console_input(key);
        activity = true;
    }

    desktop_tick();

    now = pit_ticks();
    frequency = pit_frequency() ? pit_frequency() : 100;
    delta = now - frame_tick;
    frame_tick = now;
    if (delta > frequency) {
        delta = frequency; /* do not try to catch up after a long stall */
    }
    frame_accum += delta * COMPOSITOR_HZ;
    if (frame_accum >= frequency) {
        while (frame_accum >= frequency) {
            frame_accum -= frequency;
        }
        due = true;
    }

    if (activity || due) {
        fb_present();
    }
}
