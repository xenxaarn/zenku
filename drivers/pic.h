#ifndef ZENKU_PIC_H
#define ZENKU_PIC_H

#include <types.h>

#define PIC_IRQ_COUNT 16

void pic_remap(uint8_t master_offset, uint8_t slave_offset);
void pic_disable(void);
void pic_mask(uint8_t irq);
void pic_unmask(uint8_t irq);
void pic_send_eoi(uint8_t irq);
uint16_t pic_get_isr(void);
bool pic_is_spurious(uint8_t irq);

#endif /* ZENKU_PIC_H */
