OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

KERNEL_VMA = 0xFFFFFFFF80000000;
KERNEL_LMA = 0x00200000;

SECTIONS
{
    . = KERNEL_VMA + KERNEL_LMA;

    .text : AT(ADDR(.text) - KERNEL_VMA) {
        *(.text.entry)
        *(.text*)
    }

    . = ALIGN(16);
    .rodata : AT(ADDR(.rodata) - KERNEL_VMA) {
        *(.rodata*)
    }

    . = ALIGN(4096);
    .data : AT(ADDR(.data) - KERNEL_VMA) {
        *(.data*)
    }

    . = ALIGN(16);
    .bss : AT(ADDR(.bss) - KERNEL_VMA) {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }

    ASSERT(ADDR(.bss) + SIZEOF(.bss) <= KERNEL_VMA + 0x20000000,
           "kernel image exceeds the 512 MiB kernel mapping")
}
