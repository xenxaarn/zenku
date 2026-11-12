#ifndef ZENKU_PIT_H
#define ZENKU_PIT_H

#include <types.h>

#define PIT_BASE_FREQUENCY 1193182u

void pit_init(uint32_t hz);
uint64_t pit_ticks(void);
uint32_t pit_frequency(void);
void pit_wait_ticks(uint64_t ticks);

#endif /* ZENKU_PIT_H */
