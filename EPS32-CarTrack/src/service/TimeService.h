#pragma once
#include <Arduino.h>
#include "driver/RtcDriver.h"
#include "driver/GnssDriver.h"
#include "model/SystemState.h"

class TimeService {
public:
    void init(RtcDriver* rtc, GnssDriver* gnss);
    void update();
    bool syncFromNtpWhenNoGps();
    void injectWarmStartAssistance();
    void saveLastPositionToNvs(int32_t latE7, int32_t lonE7, int32_t altCm);
    void getLocalTimeParts(int16_t tzOffsetMin, bool autoDst, uint16_t& year, uint8_t& month, uint8_t& day, uint8_t& hour, uint8_t& minute, uint8_t& second, uint8_t& weekday);

private:
    RtcDriver*  _rtc                  = nullptr;
    GnssDriver* _gnss                 = nullptr;
    bool        _gpsCalibratedRtc     = false;
    bool        _ntpSynced            = false;
    uint32_t    _lastNtpAttemptMs     = 0;
    uint32_t    _lastNvsPosSaveMs     = 0;
};
