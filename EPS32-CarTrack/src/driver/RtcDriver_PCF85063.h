#pragma once
#include <Arduino.h>
#include "config_s3_amoled.h"

struct RtcTimeInfo {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  weekday;
};

class RtcDriver_PCF85063 {
public:
    static RtcDriver_PCF85063& instance() {
        static RtcDriver_PCF85063 inst;
        return inst;
    }

    bool init();
    bool readTime(RtcTimeInfo& outTime);
    bool writeTime(const RtcTimeInfo& inTime);
    bool isOnline() const { return _isOnline; }

private:
    RtcDriver_PCF85063() = default;

    bool _isOnline = false;
    static uint8_t bcd2dec(uint8_t val) { return (val >> 4) * 10 + (val & 0x0F); }
    static uint8_t dec2bcd(uint8_t val) { return ((val / 10) << 4) | (val % 10); }
};
