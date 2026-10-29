#pragma once
#include <Arduino.h>
#include "model/SystemState.h"
#include "driver/BaroDriver.h"

class AltitudeFusion {
public:
    static AltitudeFusion& instance() {
        static AltitudeFusion inst;
        return inst;
    }

    void init();
    void update(float gnssAltM, bool gnssValid, float hdop, float speedKmh);

    float getFusedAltitude() const { return _fusedAltM; }
    float getRoadSlopePercent() const { return _roadSlopePercent; }
    float getP0Hpa() const { return _p0Hpa; }

private:
    AltitudeFusion() : _p0Hpa(1013.25f), _fusedAltM(0.0f), _roadSlopePercent(0.0f), _lastUpdateMs(0), _prevAltM(0.0f), _prevDistanceM(0.0f) {}

    float         _p0Hpa;
    float         _fusedAltM;
    float         _roadSlopePercent;
    unsigned long _lastUpdateMs;
    float         _prevAltM;
    float         _prevDistanceM;
};
