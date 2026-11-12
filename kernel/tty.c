#include <ps2.h>
#include <string.h>
#include <tty.h>
#include <types.h>

static void line_clear(struct tty *tty)
{
    tty->buffer[0] = '\0';
    tty->length = 0;
    tty->cursor = 0;
}

void tty_init(struct tty *tty)
{
    memset(tty, 0, sizeof(*tty));
    tty->limit = TTY_LINE_MAX - 1;
    tty->history_index = 0;
}

void tty_reset_line(struct tty *tty)
{
    line_clear(tty);
    tty->history_index = tty->history_count;
}

static bool insert_char(struct tty *tty, char c)
{
    int tail;

    if (tty->length >= tty->limit) {
        return false;
    }
    if (tty->cursor >= tty->limit) {
        return false;
    }

    tail = tty->length - tty->cursor;
    memmove(&tty->buffer[tty->cursor + 1], &tty->buffer[tty->cursor],
            (size_t)tail + 1);
    tty->buffer[tty->cursor] = c;
    tty->length++;
    tty->cursor++;
    return true;
}

static bool delete_prev(struct tty *tty)
{
    int tail;

    if (tty->cursor <= 0) {
        return false;
    }

    tail = tty->length - tty->cursor;
    memmove(&tty->buffer[tty->cursor - 1], &tty->buffer[tty->cursor],
            (size_t)tail + 1);
    tty->length--;
    tty->cursor--;
    return true;
}

static bool delete_at(struct tty *tty)
{
    int tail;

    if (tty->cursor >= tty->length) {
        return false;
    }

    tail = tty->length - tty->cursor - 1;
    memmove(&tty->buffer[tty->cursor], &tty->buffer[tty->cursor + 1],
            (size_t)tail + 1);
    tty->length--;
    return true;
}

bool tty_edit(struct tty *tty, int key)
{
    if (key == '\b' || key == 0x7F) {
        return delete_prev(tty);
    }
    if (key == KEY_LEFT) {
        if (tty->cursor > 0) {
            tty->cursor--;
            return true;
        }
        return false;
    }
    if (key == KEY_RIGHT) {
        if (tty->cursor < tty->length) {
            tty->cursor++;
            return true;
        }
        return false;
    }
    if (key == KEY_HOME) {
        if (tty->cursor != 0) {
            tty->cursor = 0;
            return true;
        }
        return false;
    }
    if (key == KEY_END) {
        if (tty->cursor != tty->length) {
            tty->cursor = tty->length;
            return true;
        }
        return false;
    }
    if (key == KEY_DELETE) {
        return delete_at(tty);
    }
    if (key >= 32 && key <= 126) {
        return insert_char(tty, (char)key);
    }
    return false;
}

bool tty_history(struct tty *tty, int direction)
{
    int index;

    if (tty->history_count == 0) {
        return false;
    }

    index = tty->history_index + direction;
    if (index < 0) {
        index = 0;
    }
    if (index > tty->history_count) {
        index = tty->history_count;
    }
    if (index == tty->history_index) {
        return false;
    }
    tty->history_index = index;

    if (index == tty->history_count) {
        line_clear(tty);
        return true;
    }

    strcpy(tty->buffer, tty->history[index]);
    tty->length = (int)strlen(tty->buffer);
    tty->cursor = tty->length;
    return true;
}

void tty_commit(struct tty *tty)
{
    if (tty->length > 0) {
        int index;

        if (tty->history_count < TTY_HISTORY) {
            index = tty->history_count++;
        } else {
            memmove(tty->history[0], tty->history[1],
                    sizeof(tty->history[0]) * (TTY_HISTORY - 1));
            index = TTY_HISTORY - 1;
        }
        memcpy(tty->history[index], tty->buffer, (size_t)tty->length);
        tty->history[index][tty->length] = '\0';
    }

    tty_reset_line(tty);
}
