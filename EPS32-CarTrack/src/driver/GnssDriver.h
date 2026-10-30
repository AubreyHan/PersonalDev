#pragma once
#include <Arduino.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#include "model/SystemState.h"

class GnssDriver {
public:
    bool init();
    void update();
    bool configureAutomotiveParams();
    void setSignalFilterStage(bool strictTrackingMode);
    void triggerHotStartReacquisition();
    void pushRawData(const uint8_t* buffer, size_t size);
    bool injectUtcTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second, uint32_t accuracyNs = 50000000);
    bool injectPosition(int32_t latE7, int32_t lonE7, int32_t altCm, uint32_t posAccCm = 50000);
    bool isOnline() const { return _isOnline; }
    void processNavSatData(UBX_NAV_SAT_data_t* satData);
    bool consumeReinjectRequest() {
        bool r = _needAssistReinject;
        _needAssistReinject = false;
        return r;
    }

private:
    void queryMonRf();
    void executeHotStartReacquisition();

    SFE_UBLOX_GNSS    _myGNSS;
    SemaphoreHandle_t _uartMutex           = nullptr;
    volatile bool     _requestHotRestart   = false;
    bool              _isInitialized       = false;
    bool              _isOnline            = false;
    bool              _strictFilterActive  = false;
    volatile bool     _needAssistReinject  = false;
    bool              _hadValidFixEver     = false;
    uint8_t           _lastObservedSats    = 0;
    int               _activeRxPin         = PinConfig::GNSS_RX_PIN_PRIMARY;
    int               _activeTxPin         = PinConfig::GNSS_TX_PIN_PRIMARY;
    unsigned long     _lastMonRfTimeMs     = 0;
    unsigned long     _lastValidFixMs      = 0;
    unsigned long     _firstValidFixMs     = 0;
    unsigned long     _lastHotRestartMs    = 0;
    double            _lastPlausibleLat    = 0.0;
    double            _lastPlausibleLon    = 0.0;
    uint32_t          _lastPlausibleEpoch  = 0;
};
