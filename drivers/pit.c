#include <arch/x86_64/irq.h>
#include <io.h>
#include <pit.h>
#include <types.h>

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43

#define PIT_CMD_CHANNEL0 0x00
#define PIT_CMD_LOHI     0x30
#define PIT_CMD_MODE3    0x06

static volatile uint64_t g_ticks;
static uint32_t g_frequency;

static void pit_tick(struct regs *r)
{
    (void)r;
    g_ticks++;
}

void pit_init(uint32_t hz)
{
    uint32_t divisor;

    if (hz == 0) {
        hz = 100;
    }
    divisor = PIT_BASE_FREQUENCY / hz;
    if (divisor == 0) {
        divisor = 1;
    }
    if (divisor > 0xFFFF) {
        divisor = 0xFFFF;
    }

    g_ticks = 0;
    g_frequency = PIT_BASE_FREQUENCY / divisor;

    outb(PIT_COMMAND, PIT_CMD_CHANNEL0 | PIT_CMD_LOHI | PIT_CMD_MODE3);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    irq_register(0, pit_tick);
}

uint64_t pit_ticks(void)
{
    return g_ticks;
}

uint32_t pit_frequency(void)
{
    return g_frequency;
}

void pit_wait_ticks(uint64_t ticks)
{
    uint64_t target = g_ticks + ticks;

    while (g_ticks < target) {
        __asm__ volatile("sti; hlt");
    }
}
