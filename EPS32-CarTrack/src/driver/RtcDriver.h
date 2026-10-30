#pragma once
#include <Arduino.h>
#include "config.h"

struct RtcDateTime {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
};

class RtcDriver {
public:
    bool init();
    bool readTime(RtcDateTime& outDt);
    bool writeTime(const RtcDateTime& dt);
    bool isOnline() const { return _isOnline; }

private:
    bool _isOnline = false;
    static uint8_t bcd2dec(uint8_t val) { return (val >> 4) * 10 + (val & 0x0F); }
    static uint8_t dec2bcd(uint8_t val) { return ((val / 10) << 4) | (val % 10); }
};
