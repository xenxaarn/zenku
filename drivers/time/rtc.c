#include <io.h>
#include <rtc.h>
#include <types.h>

#define CMOS_INDEX 0x70
#define CMOS_DATA  0x71

#define RTC_SECONDS 0x00
#define RTC_MINUTES 0x02
#define RTC_HOURS   0x04
#define RTC_DAY     0x07
#define RTC_MONTH   0x08
#define RTC_YEAR    0x09
#define RTC_CENTURY 0x32

#define RTC_STATUS_A 0x0A
#define RTC_STATUS_B 0x0B
#define RTC_UIP      0x80

static uint8_t cmos_read(uint8_t reg)
{
    outb(CMOS_INDEX, (uint8_t)(0x80 | reg)); /* keep NMI disabled */
    return inb(CMOS_DATA);
}

static bool update_in_progress(void)
{
    for (unsigned i = 0; i < 512; i++) {
        if (!(cmos_read(RTC_STATUS_A) & RTC_UIP)) {
            return false;
        }
    }
    return true;
}

static int bcd_to_bin(uint8_t value)
{
    return (value & 0x0F) + ((value >> 4) * 10);
}

static void read_raw(uint8_t *raw)
{
    raw[0] = cmos_read(RTC_SECONDS);
    raw[1] = cmos_read(RTC_MINUTES);
    raw[2] = cmos_read(RTC_HOURS);
    raw[3] = cmos_read(RTC_DAY);
    raw[4] = cmos_read(RTC_MONTH);
    raw[5] = cmos_read(RTC_YEAR);
    raw[6] = cmos_read(RTC_CENTURY);
    raw[7] = cmos_read(RTC_STATUS_B);
}

int rtc_read(struct rtc_time *out)
{
    uint8_t first[8];
    uint8_t second[8];
    bool binary;
    bool hour_24;
    int hour;
    int year;
    int century;
    int tries;

    if (!out) {
        return -1;
    }

    /* Sampling can straddle an update tick. Read twice and only accept a
     * stable snapshot. */
    for (tries = 0; tries < 8; tries++) {
        if (update_in_progress()) {
            continue;
        }
        read_raw(first);
        if (update_in_progress()) {
            continue;
        }
        read_raw(second);
        if (first[0] == second[0] && first[1] == second[1] &&
            first[2] == second[2] && first[3] == second[3] &&
            first[4] == second[4] && first[5] == second[5]) {
            break;
        }
    }
    if (tries == 8) {
        return -1;
    }

    binary = (first[7] & 0x04) != 0;
    hour_24 = (first[7] & 0x02) != 0;

    out->second = binary ? first[0] : bcd_to_bin(first[0]);
    out->minute = binary ? first[1] : bcd_to_bin(first[1]);
    hour = binary ? first[2] : bcd_to_bin(first[2]);
    if (!hour_24 && (first[2] & 0x80)) {
        hour = (hour & 0x7F) + 12;
    }
    out->hour = hour % 24;
    out->day = binary ? first[3] : bcd_to_bin(first[3]);
    out->month = binary ? first[4] : bcd_to_bin(first[4]);
    year = binary ? first[5] : bcd_to_bin(first[5]);
    century = binary ? first[6] : bcd_to_bin(first[6]);

    if (century >= 19 && century <= 99) {
        out->year = century * 100 + year;
    } else {
        out->year = 2000 + year;
    }

    /* Re-enable NMI after using the indexed access window. */
    outb(CMOS_INDEX, 0x00);
    return 0;
}

void rtc_format(const struct rtc_time *time, char out[9])
{
    out[0] = (char)('0' + (time->hour / 10) % 10);
    out[1] = (char)('0' + time->hour % 10);
    out[2] = ':';
    out[3] = (char)('0' + (time->minute / 10) % 10);
    out[4] = (char)('0' + time->minute % 10);
    out[5] = ':';
    out[6] = (char)('0' + (time->second / 10) % 10);
    out[7] = (char)('0' + time->second % 10);
    out[8] = '\0';
}
