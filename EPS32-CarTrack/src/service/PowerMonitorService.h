#pragma once
#include <Arduino.h>
#include "driver/Ina226Driver.h"
#include "model/SystemState.h"

class PowerMonitorService {
public:
    void init(Ina226Driver* ina);
    void update();
    static float lookupOcvSocPercent(float busVoltV);
    bool isSleeping() const { return _isStandbySleep; }
    void wakeFromStandby(const char* reason = "Screen touch detected");

private:
    void enterStandstillStandby();
    void exitStandstillStandby(const char* reason = "Wakeup event");

    Ina226Driver* _ina                    = nullptr;
    uint32_t      _lastSampleMs           = 0;
    uint32_t      _lowCurrentStartMs      = 0;
    uint32_t      _standstillStartMs      = 0;
    uint32_t      _sustainedMoveStartMs   = 0;
    float         _remainingMah           = 1900.0f;
    float         _avgCurrentMa           = 0.0f;
    bool          _ocvInitialized         = false;
    bool          _isStandbySleep         = false;
    bool          _hadExternalPowerBefore = true;
    uint8_t       _savedBrightness        = 200;
};
