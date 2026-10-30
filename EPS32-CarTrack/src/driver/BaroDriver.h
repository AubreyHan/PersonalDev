#pragma once
#include <Arduino.h>
#include "config.h"

class BaroDriver {
public:
    bool init();
    void update();
    bool isAvailable() const { return _isAvailable; }
    float getPressureHpa() const { return _lastPressureHpa; }
    float getTemperatureC() const { return _lastTempC; }
    float getRawAltitudeM(float seaLevelHpa = 1013.25f) const;

private:
    bool          _isAvailable      = false;
    uint8_t       _i2cAddr          = I2cAddrConfig::ADDR_BMP280_BARO;
    unsigned long _lastSampleTimeMs = 0;

    uint16_t _dig_T1 = 0;
    int16_t  _dig_T2 = 0, _dig_T3 = 0;
    uint16_t _dig_P1 = 0;
    int16_t  _dig_P2 = 0, _dig_P3 = 0, _dig_P4 = 0, _dig_P5 = 0;
    int16_t  _dig_P6 = 0, _dig_P7 = 0, _dig_P8 = 0, _dig_P9 = 0;
    int32_t  _tFine  = 0;

    float _lastPressureHpa = 1013.25f;
    float _lastTempC       = 24.5f;
};
