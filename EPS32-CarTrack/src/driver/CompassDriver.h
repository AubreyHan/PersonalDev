#pragma once

#include <stdint.h>
#include "../config.h"

// =============================================================================
// CompassDriver - Permanent Software Bypass Stub (MagSafe Immunity & I2C Safety)
//
// HARDWARE SAFETY INVARIANT (DO NOT VIOLATE):
// 1. On M5Stack Tab5's main SYS_I2C (SDA=31, SCL=32), address 0x10 is occupied
//    by the onboard ES8388 Audio Codec! Probing or writing BMM150 registers to
//    0x10 on SYS_I2C will corrupt the audio codec registers.
// 2. Module GNSS (M135)'s BMM150 magnetometer sits behind M135's BMI270 (0x69)
//    secondary AUX I2C bus and is saturated by strong vehicle MagSafe ring
//    magnets (~1000+ uT vs ~50 uT Earth field).
// 3. Heading is computed with 100% magnetic immunity by DeadReckoningService
//    using GNSS true ground course + Dual BMI270 (0x68 + 0x69) gyro integration.
// =============================================================================
class CompassDriver {
public:
    static CompassDriver& instance() {
        static CompassDriver s_instance;
        return s_instance;
    }

    // Intentionally never touches SYS_I2C 0x10; always reports bypassed.
    bool begin() { return false; }

    void update(uint32_t /*nowMs*/ = 0) {}

    bool isOnline() const { return false; }
    bool isBypassed() const { return DualImuConfig::BYPASS_GEOMAGNETIC_BMM150; }

private:
    CompassDriver() = default;
    ~CompassDriver() = default;
    CompassDriver(const CompassDriver&) = delete;
    CompassDriver& operator=(const CompassDriver&) = delete;
};
