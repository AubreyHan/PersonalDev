#include "driver/RtcDriver.h"
#include <time.h>

void RtcDriver::init() {
    // M5Unified automatically initializes BM8563 RTC on Core2
}

RtcTimeData RtcDriver::getTime() {
    auto dt = M5.Rtc.getDateTime();
    RtcTimeData t;
    t.year = dt.date.year;
    t.month = dt.date.month;
    t.day = dt.date.date;
    t.hour = dt.time.hours;
    t.minute = dt.time.minutes;
    t.second = dt.time.seconds;
    return t;
}

void RtcDriver::setTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
    m5::rtc_datetime_t dt;
    dt.date.year = year;
    dt.date.month = month;
    dt.date.date = day;
    dt.time.hours = hour;
    dt.time.minutes = minute;
    dt.time.seconds = second;
    M5.Rtc.setDateTime(dt);

    // Also update ESP32 system posix time
    struct tm t;
    t.tm_year = year - 1900;
    t.tm_mon  = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = minute;
    t.tm_sec  = second;
    t.tm_isdst = -1;
    time_t epoch = mktime(&t);
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
}

uint32_t RtcDriver::getEpoch() {
    RtcTimeData t = getTime();
    struct tm tm_info;
    tm_info.tm_year = t.year - 1900;
    tm_info.tm_mon  = t.month - 1;
    tm_info.tm_mday = t.day;
    tm_info.tm_hour = t.hour;
    tm_info.tm_min  = t.minute;
    tm_info.tm_sec  = t.second;
    tm_info.tm_isdst = -1;
    return (uint32_t)mktime(&tm_info);
}
