#pragma once
#include <Arduino.h>
#include "driver/BaroDriver.h"
#include "model/SystemState.h"

class AltitudeFusion {
public:
    void init(BaroDriver* baro);
    void update();
    bool isGpsCalibrated() const { return _gpsCalibrated; }
    float getSeaLevelPressure() const { return _seaLevelPressureHpa; }

private:
    BaroDriver* _baro                 = nullptr;
    float       _seaLevelPressureHpa  = 1013.25f;
    float       _fusedAltitudeM       = 0.0f;
    float       _lastAltitudeM        = 0.0f;
    float       _lastBaroAltitudeM    = 0.0f;
    double      _lastSlopeLat         = 0.0;
    double      _lastSlopeLon         = 0.0;
    float       _lastSlopeAltM        = 0.0f;
    uint32_t    _lastVerticalSampleMs = 0;
    uint32_t    _lastHistoryPushMs    = 0;
    float       _lastSavedP0          = 0.0f;
    bool        _initialized          = false;
    bool        _gpsCalibrated        = false;
};
