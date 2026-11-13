#ifndef ZENKU_PCI_H
#define ZENKU_PCI_H

#include <types.h>

#define PCI_CLASS_STORAGE 0x01
#define PCI_SUBCLASS_SATA 0x06
#define PCI_PROGIF_AHCI   0x01
#define PCI_SUBCLASS_IDE  0x01

struct pci_dev {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor;
    uint16_t device;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
};

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off,
                 uint32_t value);

/* Enable memory decoding and bus mastering. */
void pci_enable_device(const struct pci_dev *dev);

/* Read a base address register. Returns 0 for an I/O BAR, 1 for a 32-bit
 * memory BAR and 2 for the low half of a 64-bit memory BAR. */
int pci_bar(const struct pci_dev *dev, int bar, uint64_t *addr,
            uint64_t *size);

/* Find the first device matching a class, or -1. */
int pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                   struct pci_dev *out);

#endif /* ZENKU_PCI_H */
