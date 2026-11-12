#ifndef ZENKU_PIPE_H
#define ZENKU_PIPE_H

#include <types.h>

#define PIPE_CAPACITY 4096
#define PIPE_MAX       8

struct pipe;

/* Allocate a pipe object with no ends attached yet. */
struct pipe *pipe_create(void);

/* Account for one read or write end being attached or closed. */
void pipe_add_end(struct pipe *pipe, bool write_end);
void pipe_drop_end(struct pipe *pipe, bool write_end);

/* Non-blocking byte transfer. Returns -EAGAIN when empty/full and -EPIPE
 * when writing with no readers left. */
int64_t pipe_read(struct pipe *pipe, void *buf, size_t len);
int64_t pipe_write(struct pipe *pipe, const void *buf, size_t len);

#endif /* ZENKU_PIPE_H */
