#ifndef ZENKU_USERCOPY_H
#define ZENKU_USERCOPY_H

#include <types.h>

/*
 * Validate that [addr, addr+len) lies entirely in the lower canonical half
 * and that every page has a user mapping. A write range additionally needs a
 * writable (or copy-on-write) PTE, so a syscall can never write into read-only
 * executable text.
 */
bool user_range_ok(uint64_t addr, uint64_t len, bool write);

/* Copy to/from a validated user buffer. They return 0 or -EFAULT. */
int copy_from_user(void *dst, uint64_t src, size_t len);
int copy_to_user(uint64_t dst, const void *src, size_t len);

/* Copy a NUL-terminated user string into a kernel buffer. Returns 0, -EFAULT,
 * or -ENAMETOOLONG when the string does not fit (including the NUL). */
int copy_user_string(char *dst, uint64_t src, size_t cap);

#endif /* ZENKU_USERCOPY_H */
