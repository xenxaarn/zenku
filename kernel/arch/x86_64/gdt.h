#ifndef ZENKU_GDT_H
#define ZENKU_GDT_H

#include <compiler.h>
#include <types.h>

/* Selector values for the kernel/user segments we install. */
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE   0x18
#define GDT_USER_DATA   0x20
#define GDT_TSS         0x28

/* 64-bit Task State Segment (104 bytes). */
struct tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} PACKED;

void gdt_init(uint64_t kernel_stack_top);
void tss_set_rsp0(uint64_t rsp);
uint64_t tss_get_rsp0(void);

#endif /* ZENKU_GDT_H */
