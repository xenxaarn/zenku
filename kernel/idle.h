#ifndef ZENKU_IDLE_H
#define ZENKU_IDLE_H

/*
 * One pass of the kernel idle loop: halt until an interrupt, then poll the
 * mouse, desktop, keyboard, and serial port and present the framebuffer.
 *
 * The main loop and the scheduler both use this, so terminal input keeps
 * arriving while every user process is blocked waiting for it.
 */
void idle_poll(void);

#endif /* ZENKU_IDLE_H */
