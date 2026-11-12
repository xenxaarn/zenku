#ifndef ZENKU_USER_H
#define ZENKU_USER_H

#include <types.h>

/* Load an ELF64 executable from the VFS and run it in ring 3. Returns 0 when
 * the program exits back to the kernel, or a negative errno on load failure. */
int user_exec(const char *path);

#endif /* ZENKU_USER_H */
