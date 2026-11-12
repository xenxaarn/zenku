#include <errno.h>
#include <pipe.h>
#include <string.h>
#include <types.h>

struct pipe {
    bool used;
    unsigned refs;
    unsigned readers;
    unsigned writers;
    size_t head;
    size_t tail;
    size_t count;
    uint8_t buf[PIPE_CAPACITY];
};

static struct pipe g_pipes[PIPE_MAX];

struct pipe *pipe_create(void)
{
    for (unsigned i = 0; i < PIPE_MAX; i++) {
        if (!g_pipes[i].used) {
            memset(&g_pipes[i], 0, sizeof(g_pipes[i]));
            g_pipes[i].used = true;
            return &g_pipes[i];
        }
    }
    return NULL;
}

void pipe_add_end(struct pipe *pipe, bool write_end)
{
    if (!pipe) {
        return;
    }
    pipe->refs++;
    if (write_end) {
        pipe->writers++;
    } else {
        pipe->readers++;
    }
}

void pipe_drop_end(struct pipe *pipe, bool write_end)
{
    if (!pipe || !pipe->used) {
        return;
    }
    if (write_end && pipe->writers) {
        pipe->writers--;
    } else if (!write_end && pipe->readers) {
        pipe->readers--;
    }
    if (pipe->refs) {
        pipe->refs--;
    }
    if (pipe->refs == 0) {
        pipe->used = false;
    }
}

int64_t pipe_read(struct pipe *pipe, void *buf, size_t len)
{
    size_t n = 0;

    if (!pipe || !pipe->used || !buf) {
        return -EBADF;
    }
    if (pipe->count == 0) {
        return pipe->writers ? -EAGAIN : 0;
    }

    while (n < len && pipe->count > 0) {
        ((uint8_t *)buf)[n++] = pipe->buf[pipe->head];
        pipe->head = (pipe->head + 1) % PIPE_CAPACITY;
        pipe->count--;
    }
    return (int64_t)n;
}

int64_t pipe_write(struct pipe *pipe, const void *buf, size_t len)
{
    size_t n = 0;

    if (!pipe || !pipe->used || !buf) {
        return -EBADF;
    }
    if (pipe->readers == 0) {
        return -EPIPE;
    }
    if (pipe->count == PIPE_CAPACITY) {
        return -EAGAIN;
    }

    while (n < len && pipe->count < PIPE_CAPACITY) {
        pipe->buf[pipe->tail] = ((const uint8_t *)buf)[n++];
        pipe->tail = (pipe->tail + 1) % PIPE_CAPACITY;
        pipe->count++;
    }
    return (int64_t)n;
}
