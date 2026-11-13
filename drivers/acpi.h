#ifndef ZENKU_ACPI_H
#define ZENKU_ACPI_H

#include <compiler.h>
#include <types.h>

/* Parse the RSDP handed over by the bootloader. Safe to call with 0. */
void acpi_init(uint64_t rsdp_phys);

/* Enter S5 via the FADT PM1 control registers, then reset/halt if the
 * platform ignores it. */
void acpi_poweroff(void) NORETURN;

/* Reset through the FADT reset register when present, then the 8042 and the
 * PCI reset control register. */
void acpi_reboot(void) NORETURN;

#endif /* ZENKU_ACPI_H */
