#include <acpi.h>
#include <io.h>
#include <kio.h>
#include <string.h>
#include <types.h>

#define PHYS_MAP_BASE    0xFFFF800000000000ull
#define ACPI_MAP_LIMIT   GB(4)

#define ACPI_SLP_TYP_S5  5u
#define ACPI_SLP_EN      (1u << 13)
#define ACPI_SLP_TYP_MASK (7u << 10)

#define FADT_RESET_REG_SUP (1u << 10)

struct acpi_gas {
    uint8_t address_space_id;
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t access_size;
    uint64_t address;
} PACKED;

struct acpi_table_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} PACKED;

static struct {
    bool have_pm1;
    bool have_pm1b;
    bool have_x_pm1;
    bool have_reset;
    uint16_t pm1a_cnt;
    uint16_t pm1b_cnt;
    struct acpi_gas x_pm1a_cnt;
    struct acpi_gas x_pm1b_cnt;
    struct acpi_gas reset_reg;
    uint8_t reset_value;
    uint8_t revision;
} g_acpi;

static inline const void *phys_ptr(uint64_t phys)
{
    return (const void *)(uintptr_t)(PHYS_MAP_BASE + phys);
}

static inline volatile void *phys_ptr_rw(uint64_t phys)
{
    return (volatile void *)(uintptr_t)(PHYS_MAP_BASE + phys);
}

static uint16_t le16(const void *p)
{
    uint16_t value;

    memcpy(&value, p, sizeof(value));
    return value;
}

static uint32_t le32(const void *p)
{
    uint32_t value;

    memcpy(&value, p, sizeof(value));
    return value;
}

static uint64_t le64(const void *p)
{
    uint64_t value;

    memcpy(&value, p, sizeof(value));
    return value;
}

static bool table_checksum(const uint8_t *base, uint32_t length)
{
    uint8_t sum = 0;

    if (!base || length < sizeof(struct acpi_table_header) || length > 4096) {
        return false;
    }
    for (uint32_t i = 0; i < length; i++) {
        sum = (uint8_t)(sum + base[i]);
    }
    return sum == 0;
}

static bool rsdp_valid(uint64_t phys, uint32_t *revision_out,
                       uint64_t *xsdt_out, uint32_t *rsdt_out)
{
    const uint8_t *rsdp;
    uint32_t length;

    if (phys == 0 || phys + 36 > ACPI_MAP_LIMIT) {
        return false;
    }
    rsdp = phys_ptr(phys);
    if (memcmp(rsdp, "RSD PTR ", 8) != 0) {
        return false;
    }

    /* The first 20 bytes always carry a checksum. */
    {
        uint8_t sum = 0;

        for (unsigned i = 0; i < 20; i++) {
            sum = (uint8_t)(sum + rsdp[i]);
        }
        if (sum != 0) {
            return false;
        }
    }

    /* Revision lives at offset 15; offsets 9..14 are the OEM ID. */
    *revision_out = rsdp[15];
    *rsdt_out = le32(rsdp + 16);
    *xsdt_out = 0;

    if (rsdp[15] >= 2) {
        length = le32(rsdp + 20);
        if (length < 36 || length > 4096 || phys + length > ACPI_MAP_LIMIT) {
            return false;
        }
        if (!table_checksum(rsdp, length)) {
            return false;
        }
        *xsdt_out = le64(rsdp + 24);
    }
    return true;
}

static void parse_fadt(uint64_t fadt_phys)
{
    const uint8_t *fadt;
    uint32_t length;
    uint32_t flags;
    uint8_t pm1_len;

    if (fadt_phys == 0 || fadt_phys + 140 > ACPI_MAP_LIMIT) {
        return;
    }
    fadt = phys_ptr(fadt_phys);
    if (memcmp(fadt, "FACP", 4) != 0) {
        return;
    }
    length = le32(fadt + 4);
    if (length < 116 || length > 4096 ||
        fadt_phys + length > ACPI_MAP_LIMIT ||
        !table_checksum(fadt, length)) {
        return;
    }

    memset(&g_acpi, 0, sizeof(g_acpi));
    g_acpi.pm1a_cnt = le16(fadt + 64);
    g_acpi.pm1b_cnt = le16(fadt + 68);
    pm1_len = fadt[89];
    flags = le32(fadt + 112);

    if (g_acpi.pm1a_cnt) {
        g_acpi.have_pm1 = true;
    }
    if (g_acpi.pm1b_cnt) {
        g_acpi.have_pm1b = true;
    }

    if (length >= 196) {
        memcpy(&g_acpi.x_pm1a_cnt, fadt + 172, sizeof(struct acpi_gas));
        memcpy(&g_acpi.x_pm1b_cnt, fadt + 184, sizeof(struct acpi_gas));
        if (g_acpi.x_pm1a_cnt.address) {
            g_acpi.have_x_pm1 = true;
        }
    }

    if ((flags & FADT_RESET_REG_SUP) && length >= 129) {
        memcpy(&g_acpi.reset_reg, fadt + 116, sizeof(struct acpi_gas));
        g_acpi.reset_value = fadt[128];
        g_acpi.have_reset = true;
    }

    kprintf("acpi: FADT rev %u, PM1a 0x%x len %u, PM1b 0x%x, "
            "reset %s, S5 SLP_TYP %u\n",
            fadt[8], g_acpi.pm1a_cnt, pm1_len, g_acpi.pm1b_cnt,
            g_acpi.have_reset ? "yes" : "no", ACPI_SLP_TYP_S5);
}

void acpi_init(uint64_t rsdp_phys)
{
    uint32_t revision = 0;
    uint32_t rsdt = 0;
    uint64_t xsdt = 0;
    uint64_t table_phys = 0;
    const uint8_t *table;
    uint32_t length;

    memset(&g_acpi, 0, sizeof(g_acpi));

    if (!rsdp_valid(rsdp_phys, &revision, &xsdt, &rsdt)) {
        kprintf("acpi: no valid RSDP, poweroff will use the reset fallback\n");
        return;
    }

    table_phys = (revision >= 2 && xsdt) ? xsdt : rsdt;
    if (table_phys == 0 || table_phys + sizeof(struct acpi_table_header) >
                              ACPI_MAP_LIMIT) {
        kprintf("acpi: RSDP has no usable RSDT/XSDT\n");
        return;
    }
    table = phys_ptr(table_phys);
    length = le32(table + 4);
    if (memcmp(table, revision >= 2 && xsdt ? "XSDT" : "RSDT", 4) != 0 ||
        length < 36 || length > 4096 || table_phys + length > ACPI_MAP_LIMIT ||
        !table_checksum(table, length)) {
        kprintf("acpi: invalid RSDT/XSDT\n");
        return;
    }

    kprintf("acpi: RSDP rev %u at 0x%llx, %s %u bytes\n", revision,
            rsdp_phys, (revision >= 2 && xsdt) ? "XSDT" : "RSDT", length);

    for (uint32_t offset = 36; offset + 4 <= length; offset += 4) {
        uint64_t entry;

        if (revision >= 2 && xsdt) {
            if (offset + 8 > length) {
                break;
            }
            entry = le64(table + offset);
            offset += 4; /* the loop adds the other 4 */
        } else {
            entry = le32(table + offset);
        }
        if (entry && entry + sizeof(struct acpi_table_header) <=
                         ACPI_MAP_LIMIT &&
            memcmp(phys_ptr(entry), "FACP", 4) == 0) {
            parse_fadt(entry);
            return;
        }
    }

    kprintf("acpi: FADT not found\n");
}

static void gas_write(const struct acpi_gas *gas, uint64_t value)
{
    if (!gas || !gas->address) {
        return;
    }

    if (gas->address_space_id == 0) {
        /* System memory. */
        volatile void *p = phys_ptr_rw(gas->address);

        switch (gas->access_size) {
        case 1:
            *(volatile uint8_t *)p = (uint8_t)value;
            break;
        case 2:
            *(volatile uint16_t *)p = (uint16_t)value;
            break;
        case 4:
            *(volatile uint32_t *)p = (uint32_t)value;
            break;
        default:
            *(volatile uint8_t *)p = (uint8_t)value;
            break;
        }
    } else if (gas->address_space_id == 1) {
        /* System I/O. */
        if (gas->access_size == 4) {
            outl((uint16_t)gas->address, (uint32_t)value);
        } else if (gas->access_size == 2) {
            outw((uint16_t)gas->address, (uint16_t)value);
        } else {
            outb((uint16_t)gas->address, (uint8_t)value);
        }
    }
}

static void platform_reset(void) NORETURN;

static void platform_reset(void)
{
    if (g_acpi.have_reset) {
        gas_write(&g_acpi.reset_reg, g_acpi.reset_value);
    }

    /* 8042 keyboard-controller reset. */
    outb(0x64, 0xFE);
    io_wait();

    /* PCI reset control register: full reset. */
    outb(0xCF9, 0x06);
    io_wait();

    /* Last resort: force a triple fault. */
    {
        struct {
            uint16_t limit;
            uint64_t base;
        } PACKED null_idt = {0, 0};

        __asm__ volatile("lidt %0; int3" : : "m"(null_idt));
    }
    cpu_halt();
    __builtin_unreachable();
}

void acpi_poweroff(void)
{
    uint16_t value;

    irq_disable();
    kprintf("acpi: poweroff (S5)\n");

    if (g_acpi.have_x_pm1) {
        /* ACPI PM1 control registers are 16-bit even through a GAS. */
        gas_write(&g_acpi.x_pm1a_cnt,
                  (ACPI_SLP_TYP_S5 << 10) | ACPI_SLP_EN);
        if (g_acpi.x_pm1b_cnt.address) {
            gas_write(&g_acpi.x_pm1b_cnt,
                      (ACPI_SLP_TYP_S5 << 10) | ACPI_SLP_EN);
        }
    } else if (g_acpi.have_pm1) {
        value = inw(g_acpi.pm1a_cnt);
        value = (uint16_t)((value & ~ACPI_SLP_TYP_MASK) |
                           (ACPI_SLP_TYP_S5 << 10) | ACPI_SLP_EN);
        outw(g_acpi.pm1a_cnt, value);
        if (g_acpi.have_pm1b) {
            outw(g_acpi.pm1b_cnt, value);
        }
    }

    /* Give the platform a moment to cut power before forcing a reset. */
    for (volatile unsigned i = 0; i < 10000000u; i++) {
    }
    platform_reset();
}

void acpi_reboot(void)
{
    irq_disable();
    kprintf("acpi: reboot\n");
    platform_reset();
}
