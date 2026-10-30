#pragma once
#include <Arduino.h>
#include "config.h"

// Module GNSS Onboard BMM150 3-Axis Geomagnetic Compass Driver
// Explicitly bypassed per SPEC 1.4 / PRD 3.11 (MagSafe Magnetic Immunity)
class CompassDriver {
public:
    bool init() {
        _isBypassed = MotionPowerConfig::BYPASS_GEOMAGNETIC_BMM150;
        return true;
    }

    void update() {
        // Complete hardware/software bypass to prevent MagSafe NdFeB ring saturation
    }

    bool isBypassed() const { return _isBypassed; }

private:
    bool _isBypassed = true;
};
