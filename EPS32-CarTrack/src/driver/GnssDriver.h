#pragma once
#include <Arduino.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#include "model/SystemState.h"

class GnssDriver {
public:
    bool init();
    void update();
    bool configureAutomotiveParams();
    void pushRawData(const uint8_t* buffer, size_t size);
    bool injectUtcTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second, uint32_t accuracyNs = 50000000);
    bool injectPosition(int32_t latE7, int32_t lonE7, int32_t altCm, uint32_t posAccCm = 50000);
    bool isOnline() const { return _isOnline; }

private:
    void queryMonRf();

    SFE_UBLOX_GNSS _myGNSS;
    bool           _isInitialized      = false;
    bool           _isOnline           = false;
    int            _activeRxPin        = PinConfig::GNSS_RX_PIN_PRIMARY;
    int            _activeTxPin        = PinConfig::GNSS_TX_PIN_PRIMARY;
    unsigned long  _lastMonRfTimeMs    = 0;
    double         _lastPlausibleLat   = 0.0;
    double         _lastPlausibleLon   = 0.0;
    uint32_t       _lastPlausibleEpoch = 0;
};
