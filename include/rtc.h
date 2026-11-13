#ifndef ZENKU_RTC_H
#define ZENKU_RTC_H

#include <types.h>

struct rtc_time {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
};

/* Read the CMOS wall clock in 24-hour local time. Returns 0 on success and
 * leaves `out` untouched when the RTC never reports a stable value. */
int rtc_read(struct rtc_time *out);

/* Format a time as "HH:MM:SS". `out` must hold at least 9 bytes. */
void rtc_format(const struct rtc_time *time, char out[9]);

#endif /* ZENKU_RTC_H */
