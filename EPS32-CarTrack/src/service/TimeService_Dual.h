#pragma once
#include <Arduino.h>
#include <time.h>
#include "driver/RtcDriver_PCF85063.h"
#include "config_s3_amoled.h"

enum TimeSourceType : uint8_t {
    TIME_SOURCE_NONE = 0,
    TIME_SOURCE_RTC  = 1,
    TIME_SOURCE_NTP  = 2,
    TIME_SOURCE_GPS  = 3
};

class TimeService_Dual {
public:
    static TimeService_Dual& instance() {
        static TimeService_Dual inst;
        return inst;
    }

    void init();
    void update();
    void syncNtpAsync();

    TimeSourceType getSource() const { return _currentSource; }
    void getTimeString(char* outStr, size_t maxLen); // "HH:MM"
    void getDateTimeString(char* outStr, size_t maxLen); // "YYYY-MM-DD HH:MM:SS"

private:
    TimeService_Dual() = default;

    void updateSystemTime(uint16_t year, uint8_t month, uint8_t day,
                          uint8_t hour, uint8_t minute, uint8_t second,
                          TimeSourceType source);

    TimeSourceType _currentSource   = TIME_SOURCE_NONE;
    uint32_t       _lastNtpSyncMs   = 0;
    uint32_t       _lastGpsSyncMs   = 0;
    bool           _ntpConfigured   = false;
};
