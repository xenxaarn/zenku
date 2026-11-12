#ifndef ZENKU_TTY_H
#define ZENKU_TTY_H

#include <types.h>

#define TTY_LINE_MAX 128
#define TTY_HISTORY  16

/*
 * Line discipline: editing state and command history, independent of where
 * the characters are drawn. The console renders the buffer and cursor.
 */
struct tty {
    char buffer[TTY_LINE_MAX];
    int length;
    int cursor;
    int limit;                          /* max editable length */
    char history[TTY_HISTORY][TTY_LINE_MAX];
    int history_count;
    int history_index;
};

void tty_init(struct tty *tty);
void tty_reset_line(struct tty *tty);

/* Apply an editing key. Returns true when the buffer or cursor changed. */
bool tty_edit(struct tty *tty, int key);

/* Recall an older (-1) or newer (+1) command. Returns true when changed. */
bool tty_history(struct tty *tty, int direction);

/* Store the current line in history and clear the edit buffer. */
void tty_commit(struct tty *tty);

#endif /* ZENKU_TTY_H */
