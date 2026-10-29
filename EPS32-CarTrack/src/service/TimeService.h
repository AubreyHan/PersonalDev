#pragma once
#include <Arduino.h>
#include <time.h>
#include "model/SystemState.h"
#include "driver/RtcDriver.h"
#include "driver/GnssDriver.h"

class TimeService {
public:
    static TimeService& instance() {
        static TimeService inst;
        return inst;
    }

    void init();
    void update(bool wifiConnected, bool gnssValid);

    void getFormattedLocalTime(char* buffer, size_t maxLen, int8_t utcOffsetHours = 8);
    void getSlotTime(uint8_t slotIdx, char* timeBuf, size_t timeLen, char* dateBuf, size_t dateLen, bool& isDaytime);

private:
    TimeService() : _lastNtpSync(0), _lastGpsSync(0) {}

    unsigned long _lastNtpSync;
    unsigned long _lastGpsSync;

    void syncWithNtp();
};
