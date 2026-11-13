#include <io.h>
#include <pci.h>
#include <types.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_COMMAND        0x04
#define PCI_COMMAND_IO     0x0001
#define PCI_COMMAND_MEMORY 0x0002
#define PCI_COMMAND_MASTER 0x0004

static uint32_t config_address(uint8_t bus, uint8_t slot, uint8_t func,
                               uint8_t off)
{
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
           ((uint32_t)func << 8) | (off & 0xFC);
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off)
{
    outl(PCI_CONFIG_ADDRESS, config_address(bus, slot, func, off));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off)
{
    uint32_t value = pci_read32(bus, slot, func, off & 0xFC);

    return (uint16_t)(value >> ((off & 2) * 8));
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off,
                 uint32_t value)
{
    outl(PCI_CONFIG_ADDRESS, config_address(bus, slot, func, off));
    outl(PCI_CONFIG_DATA, value);
}

void pci_enable_device(const struct pci_dev *dev)
{
    uint16_t command;

    if (!dev) {
        return;
    }
    command = pci_read16(dev->bus, dev->slot, dev->func, PCI_COMMAND);
    command |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
    pci_write32(dev->bus, dev->slot, dev->func, PCI_COMMAND,
                (uint32_t)command);
}

int pci_bar(const struct pci_dev *dev, int bar, uint64_t *addr,
            uint64_t *size)
{
    uint8_t off = (uint8_t)(0x10 + bar * 4);
    uint32_t original;
    uint32_t mask;
    uint64_t base;
    uint64_t length;

    if (!dev || bar < 0 || bar > 5) {
        return -1;
    }
    original = pci_read32(dev->bus, dev->slot, dev->func, off);
    if (original & 0x1) {
        *addr = original & ~0x3u;
        *size = 0;
        return 0;
    }

    pci_write32(dev->bus, dev->slot, dev->func, off, 0xFFFFFFFFu);
    mask = pci_read32(dev->bus, dev->slot, dev->func, off);
    pci_write32(dev->bus, dev->slot, dev->func, off, original);
    if (!mask) {
        return -1;
    }
    if ((original & 0x6) == 0x4) {
        uint32_t high_orig = pci_read32(dev->bus, dev->slot, dev->func,
                                        (uint8_t)(off + 4));
        uint32_t high_mask;

        pci_write32(dev->bus, dev->slot, dev->func, off, 0xFFFFFFFFu);
        pci_write32(dev->bus, dev->slot, dev->func, (uint8_t)(off + 4),
                    0xFFFFFFFFu);
        mask = pci_read32(dev->bus, dev->slot, dev->func, off);
        high_mask = pci_read32(dev->bus, dev->slot, dev->func,
                               (uint8_t)(off + 4));
        pci_write32(dev->bus, dev->slot, dev->func, off, original);
        pci_write32(dev->bus, dev->slot, dev->func, (uint8_t)(off + 4),
                    high_orig);
        base = (original & ~0xFu) | ((uint64_t)high_orig << 32);
        if (high_mask == 0xFFFFFFFFu) {
            length = ~((uint64_t)mask & ~0xFu) + 1;
        } else {
            length = GB(4);
        }
        *addr = base;
        *size = length;
        return 2;
    }

    base = original & ~0xFu;
    length = ~((uint64_t)mask & ~0xFu) + 1;
    *addr = base;
    *size = length;
    return 1;
}

int pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                   struct pci_dev *out)
{
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t slot = 0; slot < 32; slot++) {
            uint32_t id = pci_read32((uint8_t)bus, (uint8_t)slot, 0, 0);
            uint16_t vendor = (uint16_t)id;

            if (vendor == 0xFFFF) {
                continue;
            }
            for (uint32_t func = 0; func < 8; func++) {
                uint32_t reg;
                uint8_t fn_class;
                uint8_t fn_subclass;
                uint8_t fn_prog_if;

                if (func && !(pci_read32((uint8_t)bus, (uint8_t)slot, 0,
                                         0x0C) & 0x00800000u)) {
                    break;
                }
                reg = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func,
                                 0x08);
                fn_class = (uint8_t)(reg >> 24);
                fn_subclass = (uint8_t)(reg >> 16);
                fn_prog_if = (uint8_t)(reg >> 8);
                if (fn_class != class_code || fn_subclass != subclass) {
                    continue;
                }
                if (prog_if != 0xFF && fn_prog_if != prog_if) {
                    continue;
                }
                if (out) {
                    out->bus = (uint8_t)bus;
                    out->slot = (uint8_t)slot;
                    out->func = (uint8_t)func;
                    out->vendor = vendor;
                    out->device = (uint16_t)(id >> 16);
                    out->class_code = fn_class;
                    out->subclass = fn_subclass;
                    out->prog_if = fn_prog_if;
                }
                return 0;
            }
        }
    }
    return -1;
}
