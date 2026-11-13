#include <ahci.h>
#include <errno.h>
#include <io.h>
#include <kio.h>
#include <mm/pmm.h>
#include <pci.h>
#include <string.h>
#include <types.h>

#define PHYS_MAP_BASE 0xFFFF800000000000ull

#define AHCI_MAX_PORTS 4
#define AHCI_SECTOR    512

#define HBA_CAP   0x00
#define HBA_GHC   0x04
#define HBA_IS    0x08
#define HBA_PI    0x0C
#define HBA_VS    0x10
#define HBA_PORT  0x100
#define PORT_STRIDE 0x80

#define PORT_CLB  0x00
#define PORT_CLBU 0x04
#define PORT_FB   0x08
#define PORT_FBU  0x0C
#define PORT_IS   0x10
#define PORT_IE   0x14
#define PORT_CMD  0x18
#define PORT_TFD  0x20
#define PORT_SIG  0x24
#define PORT_SSTS 0x28
#define PORT_SERR 0x30
#define PORT_CI   0x38

#define GHC_AE (1u << 31)
#define CMD_ST (1u << 0)
#define CMD_FRE (1u << 4)
#define CMD_FR  (1u << 14)
#define CMD_CR  (1u << 15)
#define TFD_ERR (1u << 0)
#define TFD_DRQ (1u << 3)
#define TFD_BSY (1u << 7)

#define SATA_SIG_ATA   0x00000101u
#define SATA_SIG_ATAPI 0xEB140101u
#define SATA_SIG_SEMB  0xC33C0101u
#define SATA_SIG_PM    0x96690101u

#define ATA_CMD_READ_DMA_EXT  0x25
#define ATA_CMD_WRITE_DMA_EXT 0x35
#define ATA_CMD_FLUSH_EXT     0xEA
#define ATA_CMD_IDENTIFY      0xEC

struct ahci_port {
    volatile uint8_t *base;
    uint64_t clb_phys;
    uint64_t fis_phys;
    uint64_t ct_phys;
    uint64_t buf_phys;
    uint64_t sectors;
    bool lba48;
    struct bdev bdev;
};

static struct ahci_port g_ports[AHCI_MAX_PORTS];
static int g_port_count;

static inline void *phys_to_virt(uint64_t phys)
{
    return (void *)(uintptr_t)(PHYS_MAP_BASE + phys);
}

static inline uint32_t reg_read(volatile uint8_t *base, uint32_t off)
{
    return *(volatile uint32_t *)(void *)(base + off);
}

static inline void reg_write(volatile uint8_t *base, uint32_t off,
                             uint32_t value)
{
    *(volatile uint32_t *)(void *)(base + off) = value;
}

static void barrier(void)
{
    __asm__ volatile("mfence" : : : "memory");
}

static int wait_port_idle(volatile uint8_t *base)
{
    for (int i = 0; i < 1000000; i++) {
        if (!(reg_read(base, PORT_TFD) & (TFD_BSY | TFD_DRQ))) {
            return 0;
        }
    }
    return -EIO;
}

static int port_start(volatile uint8_t *base)
{
    uint32_t cmd = reg_read(base, PORT_CMD);

    if (cmd & CMD_ST) {
        cmd &= ~CMD_ST;
        reg_write(base, PORT_CMD, cmd);
    }
    if (cmd & CMD_FRE) {
        cmd &= ~CMD_FRE;
        reg_write(base, PORT_CMD, cmd);
    }
    for (int i = 0; i < 1000000; i++) {
        if (!(reg_read(base, PORT_CMD) & (CMD_FR | CMD_CR))) {
            break;
        }
    }
    cmd = reg_read(base, PORT_CMD);
    cmd |= CMD_FRE;
    reg_write(base, PORT_CMD, cmd);
    for (int i = 0; i < 1000000; i++) {
        if (reg_read(base, PORT_CMD) & CMD_FR) {
            break;
        }
    }
    cmd = reg_read(base, PORT_CMD);
    cmd |= CMD_ST;
    reg_write(base, PORT_CMD, cmd);
    return wait_port_idle(base);
}

static int issue(struct ahci_port *p, const uint8_t *fis, bool write,
                 uint32_t bytes)
{
    volatile uint8_t *base = p->base;
    uint8_t *cmd = phys_to_virt(p->clb_phys);
    uint32_t *hdr = (uint32_t *)(void *)cmd;
    uint8_t *ct = phys_to_virt(p->ct_phys);
    uint32_t *prdt = (uint32_t *)(void *)(ct + 0x80);
    uint32_t prdtl = bytes ? 1 : 0;

    if (wait_port_idle(base) != 0) {
        return -EIO;
    }
    memset(cmd, 0, 1024);
    memset(ct, 0, 256);
    memcpy(ct, fis, 20);

    hdr[0] = (5u) | (write ? (1u << 6) : 0) | (prdtl << 16);
    hdr[1] = 0;
    hdr[2] = (uint32_t)p->ct_phys;
    hdr[3] = 0;

    if (bytes) {
        prdt[0] = (uint32_t)p->buf_phys;
        prdt[1] = (uint32_t)(p->buf_phys >> 32);
        prdt[2] = 0;
        prdt[3] = bytes - 1;
    }

    reg_write(base, PORT_IS, 0xFFFFFFFFu);
    barrier();
    reg_write(base, PORT_CI, 1);
    barrier();

    for (int i = 0; i < 2000000; i++) {
        uint32_t ci = reg_read(base, PORT_CI);

        if (!(ci & 1)) {
            if (reg_read(base, PORT_TFD) & TFD_ERR) {
                reg_write(base, PORT_SERR, reg_read(base, PORT_SERR));
                return -EIO;
            }
            return 0;
        }
        if (reg_read(base, PORT_IS) & 0x40000000u) {
            return -EIO;
        }
    }
    return -EIO;
}

static void build_lba_fis(uint8_t *fis, uint8_t command, uint64_t lba,
                          uint32_t count, bool lba48)
{
    memset(fis, 0, 20);
    fis[0] = 0x27;
    fis[1] = 0x80;
    fis[2] = command;
    fis[4] = (uint8_t)lba;
    fis[5] = (uint8_t)(lba >> 8);
    fis[6] = (uint8_t)(lba >> 16);
    fis[7] = 0x40;
    fis[12] = (uint8_t)count;
    if (lba48) {
        fis[8] = (uint8_t)(lba >> 24);
        fis[9] = (uint8_t)(lba >> 32);
        fis[10] = (uint8_t)(lba >> 40);
        fis[13] = (uint8_t)(count >> 8);
    }
}

static int identify(struct ahci_port *p, uint16_t *words)
{
    uint8_t fis[20];
    uint8_t *buf = phys_to_virt(p->buf_phys);
    int rc;

    memset(fis, 0, sizeof(fis));
    fis[0] = 0x27;
    fis[1] = 0x80;
    fis[2] = ATA_CMD_IDENTIFY;
    rc = issue(p, fis, false, AHCI_SECTOR);
    if (rc < 0) {
        return rc;
    }
    memcpy(words, buf, AHCI_SECTOR);
    return 0;
}

static int port_read(struct bdev *dev, uint64_t block, void *buf,
                     uint32_t count)
{
    struct ahci_port *p = dev->ctx;
    uint8_t *bounce = phys_to_virt(p->buf_phys);
    uint8_t *dst = buf;

    for (uint32_t i = 0; i < count; i++) {
        uint8_t fis[20];

        build_lba_fis(fis, ATA_CMD_READ_DMA_EXT, block + i, 1, p->lba48);
        if (issue(p, fis, false, AHCI_SECTOR) != 0) {
            return -EIO;
        }
        memcpy(dst + i * AHCI_SECTOR, bounce, AHCI_SECTOR);
    }
    return 0;
}

static int port_write(struct bdev *dev, uint64_t block, const void *buf,
                      uint32_t count)
{
    struct ahci_port *p = dev->ctx;
    uint8_t *bounce = phys_to_virt(p->buf_phys);
    const uint8_t *src = buf;

    for (uint32_t i = 0; i < count; i++) {
        uint8_t fis[20];

        memcpy(bounce, src + i * AHCI_SECTOR, AHCI_SECTOR);
        build_lba_fis(fis, ATA_CMD_WRITE_DMA_EXT, block + i, 1, p->lba48);
        if (issue(p, fis, true, AHCI_SECTOR) != 0) {
            return -EIO;
        }
    }
    return 0;
}

static int port_flush(struct bdev *dev)
{
    struct ahci_port *p = dev->ctx;
    uint8_t fis[20];

    memset(fis, 0, sizeof(fis));
    fis[0] = 0x27;
    fis[1] = 0x80;
    fis[2] = ATA_CMD_FLUSH_EXT;
    fis[7] = 0x40;
    return issue(p, fis, true, 0);
}

static int setup_port(volatile uint8_t *hba, uint32_t index,
                      struct ahci_port *p)
{
    volatile uint8_t *base = hba + HBA_PORT + index * PORT_STRIDE;
    uint32_t cmd;
    uint16_t words[256];
    uint64_t sectors;

    memset(p, 0, sizeof(*p));
    p->base = base;

    if ((reg_read(base, PORT_SIG) & 0xFFFFFFFFu) == SATA_SIG_ATAPI ||
        (reg_read(base, PORT_SIG) & 0xFFFFFFFFu) == SATA_SIG_SEMB ||
        (reg_read(base, PORT_SIG) & 0xFFFFFFFFu) == SATA_SIG_PM) {
        return -ENODEV;
    }
    if ((reg_read(base, PORT_SSTS) & 0xF) != 3) {
        return -ENODEV;
    }

    p->clb_phys = pmm_alloc_frame();
    p->fis_phys = pmm_alloc_frame();
    p->ct_phys = pmm_alloc_frame();
    p->buf_phys = pmm_alloc_frame();
    if (!p->clb_phys || !p->fis_phys || !p->ct_phys || !p->buf_phys) {
        kprintf("ahci: port %u out of DMA frames\n", index);
        return -ENOMEM;
    }

    /* Stop the engine before replacing the list pointers. */
    cmd = reg_read(base, PORT_CMD);
    reg_write(base, PORT_CMD, cmd & ~CMD_ST);
    reg_write(base, PORT_CMD, cmd & ~(CMD_ST | CMD_FRE));
    for (int i = 0; i < 1000000; i++) {
        if (!(reg_read(base, PORT_CMD) & (CMD_FR | CMD_CR))) {
            break;
        }
    }

    reg_write(base, PORT_CLB, (uint32_t)p->clb_phys);
    reg_write(base, PORT_CLBU, (uint32_t)(p->clb_phys >> 32));
    reg_write(base, PORT_FB, (uint32_t)p->fis_phys);
    reg_write(base, PORT_FBU, (uint32_t)(p->fis_phys >> 32));
    reg_write(base, PORT_IS, 0xFFFFFFFFu);
    reg_write(base, PORT_IE, 0);

    if (port_start(base) != 0) {
        kprintf("ahci: port %u start failed (CMD 0x%x, TFD 0x%x)\n", index,
                reg_read(base, PORT_CMD), reg_read(base, PORT_TFD));
        return -EIO;
    }

    if (identify(p, words) != 0) {
        kprintf("ahci: port %u identify failed (TFD 0x%x, CI 0x%x)\n",
                index, reg_read(base, PORT_TFD), reg_read(base, PORT_CI));
        return -EIO;
    }
    if (words[0] == 0) {
        return -ENODEV;
    }

    sectors = (uint64_t)words[60] | ((uint64_t)words[61] << 16);
    p->lba48 = (words[83] & (1u << 10)) != 0;
    if (p->lba48) {
        uint64_t ext = (uint64_t)words[100] | ((uint64_t)words[101] << 16) |
                       ((uint64_t)words[102] << 32) |
                       ((uint64_t)words[103] << 48);

        if (ext) {
            sectors = ext;
        }
    }
    if (!sectors) {
        return -ENODEV;
    }

    p->sectors = sectors;
    p->bdev.ctx = p;
    p->bdev.block_size = AHCI_SECTOR;
    p->bdev.block_count = sectors;
    p->bdev.name = "ahci";
    p->bdev.read_only = false;
    p->bdev.read = port_read;
    p->bdev.write = port_write;
    p->bdev.flush = port_flush;
    return 0;
}

int ahci_init(struct bdev *out, int max)
{
    struct pci_dev dev;
    uint64_t abar;
    uint64_t abar_size;
    volatile uint8_t *hba;
    uint32_t pi;
    int found = 0;

    if (!out || max <= 0) {
        return -EINVAL;
    }
    if (pci_find_class(PCI_CLASS_STORAGE, PCI_SUBCLASS_SATA,
                       PCI_PROGIF_AHCI, &dev) != 0) {
        return 0;
    }
    if (pci_bar(&dev, 5, &abar, &abar_size) != 1) {
        kprintf("ahci: BAR5 is not a 32-bit memory BAR\n");
        return 0;
    }
    pci_enable_device(&dev);

    if (abar == 0 || abar + 0x1100 > GB(4)) {
        kprintf("ahci: ABAR 0x%llx is outside the direct map\n", abar);
        return 0;
    }
    hba = (volatile uint8_t *)(uintptr_t)(PHYS_MAP_BASE + abar);
    reg_write(hba, HBA_GHC, reg_read(hba, HBA_GHC) | GHC_AE);
    pi = reg_read(hba, HBA_PI);

    kprintf("ahci: controller %04x:%04x ABAR 0x%llx ports 0x%x\n",
            dev.vendor, dev.device, abar, pi);

    for (uint32_t i = 0; i < 32 && found < max; i++) {
        if (!(pi & (1u << i))) {
            continue;
        }
        if (g_port_count >= AHCI_MAX_PORTS) {
            break;
        }
        if (setup_port(hba, i, &g_ports[g_port_count]) == 0) {
            struct ahci_port *p = &g_ports[g_port_count];

            kprintf("ahci: port %u disk %llu sectors%s\n", i,
                    (unsigned long long)p->sectors,
                    p->lba48 ? " (LBA48)" : "");
            out[found++] = p->bdev;
            g_port_count++;
        }
    }
    return found;
}
