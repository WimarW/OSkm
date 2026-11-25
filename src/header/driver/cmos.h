#ifndef _CMOS_H
#define _CMOS_H

#include <stdint.h>

struct CMOSTime {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

void cmos_read_time(struct CMOSTime *time);

#endif