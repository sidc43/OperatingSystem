/*
  rtc.cpp - real-time clock using the pl031 hardware rtc at 0x09010000
  on boot it reads the actual host wall-clock time from the rtc dr register
  then uses timer ticks to advance it from there so seconds keep ticking
  falls back to compile-time __DATE__/__TIME__ if the hardware reads garbage
  also has a small timezone table you can configure from the control panel
*/
#include "kernel/core/rtc.hpp"
#include "arch/aarch64/regs.hpp"
#include <stdint.h>

static constexpr uintptr_t PL031_BASE = 0x09010000u;
static constexpr uintptr_t PL031_DR   = PL031_BASE + 0x00u;
static constexpr uintptr_t PL031_CR   = PL031_BASE + 0x0Cu;

static constexpr uint32_t UNIX_2020 = 1577836800u;

static inline uint32_t pl031_read() {

    volatile uint32_t* cr = reinterpret_cast<volatile uint32_t*>(PL031_CR);
    if (!(*cr & 1u)) *cr = 1u;
    dsb_sy();
    volatile uint32_t* dr = reinterpret_cast<volatile uint32_t*>(PL031_DR);
    uint32_t v = *dr;
    dsb_sy();
    return v;
}

namespace rtc {

const TZEntry tz_table[TZ_COUNT] = {
    { -8, "PST"  },
    { -7, "MST"  },
    { -6, "CST"  },
    { -5, "EST"  },
    { -4, "AST"  },
    {  0, "UTC"  },
    {  1, "CET"  },
    {  2, "EET"  },
    {  5, "PKT"  },
    {  8, "CST+8"},
};

static int8_t   g_tz_offset  =  0;
static uint32_t g_base_unix  = 0;
static uint64_t g_base_ticks = 0;

static bool is_leap(uint16_t y) {
    return (y % 4 == 0) && ((y % 100 != 0) || (y % 400 == 0));
}

static uint8_t days_in_month(uint8_t m, uint16_t y) {
    static const uint8_t dim[13] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && is_leap(y)) return 29;
    return dim[m];
}

static uint8_t parse_month(const char* d) {

    static const char* names[12] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    for (uint8_t i = 0; i < 12; ++i) {
        if (d[0] == names[i][0] && d[1] == names[i][1] && d[2] == names[i][2])
            return (uint8_t)(i + 1);
    }
    return 1;
}

static uint8_t parse_day(const char* d) {

    uint8_t tens = (d[4] == ' ') ? 0u : (uint8_t)(d[4] - '0');
    uint8_t ones = (uint8_t)(d[5] - '0');
    return (uint8_t)(tens * 10 + ones);
}

static uint16_t parse_year(const char* d) {
    return (uint16_t)(
        (d[7]-'0')*1000 + (d[8]-'0')*100 + (d[9]-'0')*10 + (d[10]-'0'));
}

static uint8_t parse_time_h(const char* t) { return (uint8_t)((t[0]-'0')*10 + (t[1]-'0')); }
static uint8_t parse_time_m(const char* t) { return (uint8_t)((t[3]-'0')*10 + (t[4]-'0')); }
static uint8_t parse_time_s(const char* t) { return (uint8_t)((t[6]-'0')*10 + (t[7]-'0')); }

static uint32_t to_unix(uint16_t year, uint8_t month, uint8_t day,
                        uint8_t hour, uint8_t min, uint8_t sec) {
    uint32_t days = 0;
    for (uint16_t y = 1970; y < year; ++y)
        days += is_leap(y) ? 366u : 365u;
    for (uint8_t m = 1; m < month; ++m)
        days += days_in_month(m, year);
    days += (uint32_t)(day - 1);
    return days * 86400u
         + (uint32_t)hour * 3600u
         + (uint32_t)min  *   60u
         + (uint32_t)sec;
}

static void from_unix(uint32_t ts,
                      uint16_t& year, uint8_t& month, uint8_t& day,
                      uint8_t& hour,  uint8_t& min,   uint8_t& sec,
                      uint8_t& wday) {
    sec   = (uint8_t)(ts % 60); ts /= 60;
    min   = (uint8_t)(ts % 60); ts /= 60;
    hour  = (uint8_t)(ts % 24); ts /= 24;

    wday  = (uint8_t)((ts + 4) % 7);
    year  = 1970;
    while (true) {
        uint32_t dy = is_leap(year) ? 366u : 365u;
        if (ts < dy) break;
        ts -= dy; ++year;
    }
    month = 1;
    while (true) {
        uint32_t dm = days_in_month(month, year);
        if (ts < dm) break;
        ts -= dm; ++month;
    }
    day = (uint8_t)(ts + 1);
}

void init(uint64_t baseline_ticks) {
    g_base_ticks = baseline_ticks;

    uint32_t hw = pl031_read();
    if (hw >= UNIX_2020) {

        g_base_unix = hw;
        return;
    }

    const char* dt = __DATE__;
    const char* tm = __TIME__;
    uint16_t year  = parse_year(dt);
    uint8_t  month = parse_month(dt);
    uint8_t  day   = parse_day(dt);
    uint8_t  hour  = parse_time_h(tm);
    uint8_t  mins  = parse_time_m(tm);
    uint8_t  secs  = parse_time_s(tm);
    g_base_unix = to_unix(year, month, day, hour, mins, secs);
}

DateTime now(uint64_t current_ticks) {
    uint64_t elapsed_secs = (current_ticks - g_base_ticks) / 100u;
    uint32_t ts_utc = g_base_unix + (uint32_t)elapsed_secs;

    int32_t off = (int32_t)g_tz_offset * 3600;
    uint32_t ts_local;
    if (off < 0 && (uint32_t)(-off) > ts_utc)
        ts_local = 0;
    else
        ts_local = (uint32_t)((int32_t)ts_utc + off);
    DateTime dt{};
    from_unix(ts_local, dt.year, dt.month, dt.day,
              dt.hour,  dt.min,  dt.sec,   dt.wday);
    return dt;
}

void set_timezone(int8_t offset) { g_tz_offset = offset; }
int8_t get_timezone() { return g_tz_offset; }

const char* tz_name() {
    for (int i = 0; i < TZ_COUNT; ++i)
        if (tz_table[i].offset_h == g_tz_offset)
            return tz_table[i].name;

    static char buf[8];
    int8_t a = g_tz_offset < 0 ? (int8_t)(-g_tz_offset) : g_tz_offset;
    buf[0] = 'U'; buf[1] = 'T'; buf[2] = 'C';
    buf[3] = g_tz_offset >= 0 ? '+' : '-';
    buf[4] = (char)('0' + a / 10);
    buf[5] = (char)('0' + a % 10);
    buf[6] = '\0';
    return buf;
}

}
