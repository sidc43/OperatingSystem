/*
  rtc.hpp - real-time clock interface
  reads the pl031 hardware rtc (qemu exposes real host time)
  returns a DateTime struct with year/month/day/hour/min/sec
  supports multiple timezones via a small offset table
*/
#pragma once
#include <stdint.h>

namespace rtc {

struct DateTime {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  min;
    uint8_t  sec;
    uint8_t  wday;
};

void init(uint64_t baseline_ticks_100hz);

DateTime now(uint64_t current_ticks_100hz);

struct TZEntry {
    int8_t      offset_h;
    const char* name;
};

static constexpr int TZ_COUNT = 10;
extern const TZEntry tz_table[TZ_COUNT];

void   set_timezone(int8_t utc_offset_hours);
int8_t get_timezone();

const char* tz_name();

}
