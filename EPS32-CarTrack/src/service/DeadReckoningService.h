#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include "model/SystemState.h"

class DeadReckoningService {
public:
    static DeadReckoningService& instance() {
        static DeadReckoningService inst;
        return inst;
    }

    void init();
    void update(bool gnssValid, float speedKmh, float headingDeg, int32_t latE7, int32_t lonE7);

    bool isInDrMode() const { return _inDrMode; }
    uint32_t getDrDurationSec() const { return _drDurationSec; }
    float getDrHeading() const { return _drHeadingDeg; }
    float getDrSpeed() const { return _drSpeedKmh; }

private:
    DeadReckoningService() : _inDrMode(false), _drDurationSec(0), _drHeadingDeg(0.0f), _drSpeedKmh(0.0f), _lastDrEpoch(0), _lastGnssValidTime(0), _lastImuUpdateMs(0) {}

    bool          _inDrMode;
    uint32_t      _drDurationSec;
    float         _drHeadingDeg;
    float         _drSpeedKmh;
    unsigned long _lastDrEpoch;
    unsigned long _lastGnssValidTime;
    unsigned long _lastImuUpdateMs;
};
