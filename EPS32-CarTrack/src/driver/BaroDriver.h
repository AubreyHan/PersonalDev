#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "../config.h"

// =============================================================================
// BaroDriver - M5Stack Module GNSS (SKU: M135) Onboard Bosch BMP280 Driver
// Bus: Tab5 SYS_I2C (M5.In_I2C, SDA=GPIO31, SCL=GPIO32, 400kHz)
// I2C Address: 0x76 (CHIP_ID 0xD0 == 0x58)
// Config: ctrl_meas=0x57 (osrs_t=x2, osrs_p=x16, Normal), config=0x10 (filter=16)
// Output: 10Hz compensated ambient pressure (hPa) & temperature (C)
// =============================================================================
class BaroDriver {
public:
    static BaroDriver& instance();

    // Probe BMP280 at 0x76, read 24-byte factory calibration (0x88..0x9F),
    // configure Normal x16 oversampling + IIR x16 filter, and publish initial state.
    bool begin();

    // Non-blocking 10Hz (100ms) update tick; updates SystemState::baro pressure/temp.
    bool update(uint32_t nowMs = millis());

    bool     isOnline() const { return _online; }
    float    pressureHpa() const { return _pressureHpa; }
    float    temperatureC() const { return _temperatureC; }
    uint32_t lastSampleMs() const { return _lastSampleMs; }

private:
    BaroDriver() = default;
    ~BaroDriver() = default;
    BaroDriver(const BaroDriver&) = delete;
    BaroDriver& operator=(const BaroDriver&) = delete;

    struct CalibrationData {
        uint16_t dig_T1 = 0;
        int16_t  dig_T2 = 0;
        int16_t  dig_T3 = 0;
        uint16_t dig_P1 = 0;
        int16_t  dig_P2 = 0;
        int16_t  dig_P3 = 0;
        int16_t  dig_P4 = 0;
        int16_t  dig_P5 = 0;
        int16_t  dig_P6 = 0;
        int16_t  dig_P7 = 0;
        int16_t  dig_P8 = 0;
        int16_t  dig_P9 = 0;
    };

    bool readCalibration();
    bool readCompensatedSample(float& outTempC, float& outPressHpa);

    CalibrationData _calib         = {};
    int32_t         _tFine         = 0;
    bool            _online        = false;
    bool            _calibLoaded   = false;
    float           _pressureHpa   = BaroFusionConfig::DEFAULT_SEA_LEVEL_HPA;
    float           _temperatureC  = 25.0f;
    uint32_t        _lastSampleMs  = 0;
    uint32_t        _lastRetryMs   = 0;
    uint8_t         _failStreak    = 0;
};
