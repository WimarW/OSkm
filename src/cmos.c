#include <stdbool.h>
#include "header/driver/cmos.h"
#include "header/cpu/portio.h"

#define CMOS_INDEX_PORT 0x70
#define CMOS_DATA_PORT  0x71

static uint8_t cmos_read(uint8_t reg) {
    out(CMOS_INDEX_PORT, reg);
    return in(CMOS_DATA_PORT);
}

static uint8_t bcd_to_bin(uint8_t v) {
    return (uint8_t)((v & 0x0F) + (v >> 4) * 10);
}

void cmos_read_time(struct CMOSTime *time) {
    while (cmos_read(0x0A) & 0x80);

    uint8_t second = cmos_read(0x00);
    uint8_t minute = cmos_read(0x02);
    uint8_t hour   = cmos_read(0x04);
    uint8_t status_b = cmos_read(0x0B);

    bool pm = (hour & 0x80) != 0;
    hour &= 0x7F;

    if (!(status_b & 0x04)) {
        second = bcd_to_bin(second);
        minute = bcd_to_bin(minute);
        hour   = bcd_to_bin(hour);
    }
    if (!(status_b & 0x02)) {
        hour = (uint8_t)(hour % 12);
        if (pm) hour = (uint8_t)(hour + 12);
    }

    time->hour = hour;
    time->minute = minute;
    time->second = second;
}