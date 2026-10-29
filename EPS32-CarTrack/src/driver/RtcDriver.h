#pragma once
#include <Arduino.h>
#include <M5Unified.h>

struct RtcTimeData {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
};

class RtcDriver {
public:
    static RtcDriver& instance() {
        static RtcDriver inst;
        return inst;
    }

    void init();
    RtcTimeData getTime();
    void setTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second);
    uint32_t getEpoch();

private:
    RtcDriver() {}
};
