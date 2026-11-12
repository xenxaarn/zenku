#ifndef ZENKU_LOADER_H
#define ZENKU_LOADER_H

#include <types.h>

/* Load an ELF64 executable into the current address space. Returns 0 on
 * success and stores the entry point in `entry_out`. User stack setup and
 * process creation are handled by the scheduler. */
int elf_load(const char *path, uint64_t *entry_out);

#endif /* ZENKU_LOADER_H */
