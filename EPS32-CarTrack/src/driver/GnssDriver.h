#pragma once
#include <Arduino.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#include "model/SystemState.h"
#include "config.h"

class GnssDriver {
public:
    static GnssDriver& instance() {
        static GnssDriver inst;
        return inst;
    }

    bool init();
    void update();

    // Injection helpers
    bool injectUtcTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second, uint32_t accuracyNs = 50000000);
    bool injectPosition(int32_t latE7, int32_t lonE7, int32_t altCm, uint32_t posAccCm = 50000);
    void pushRawData(const uint8_t* buffer, size_t size);

    HardwareSerial& getSerial() { return Serial2; }

private:
    GnssDriver() : _isInitialized(false), _lastMonRfTime(0), _lastPlausibleLat(0), _lastPlausibleLon(0), _lastPlausibleEpoch(0) {}
    
    bool configureAutomotiveParams();
    void queryMonRf();

    SFE_UBLOX_GNSS _myGNSS;
    bool           _isInitialized;
    unsigned long  _lastMonRfTime;
    
    // Kinematic filter state
    double         _lastPlausibleLat;
    double         _lastPlausibleLon;
    uint32_t       _lastPlausibleEpoch;
};
