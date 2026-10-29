#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include "config.h"

class BaroDriver {
public:
    static BaroDriver& instance() {
        static BaroDriver inst;
        return inst;
    }

    bool init();
    void update();

    bool isAvailable() const { return _isAvailable; }
    float getPressureHpa() const { return _lastPressureHpa; }
    float getTemperatureC() const { return _lastTempC; }
    float getRawAltitudeM() const { return _lastRawAltitudeM; }

private:
    BaroDriver() : _i2cAddr(0x76), _isAvailable(false), _lastPressureHpa(1013.25f), _lastTempC(25.0f), _lastRawAltitudeM(0.0f), _lastSampleTime(0), _tFine(0) {}
    uint8_t         _i2cAddr;
    bool            _isAvailable;
    float           _lastPressureHpa;
    float           _lastTempC;
    float           _lastRawAltitudeM;
    unsigned long   _lastSampleTime;
    int32_t         _tFine;

    // Bosch BMP280 factory calibration trimming parameters
    uint16_t _dig_T1;
    int16_t  _dig_T2;
    int16_t  _dig_T3;
    uint16_t _dig_P1;
    int16_t  _dig_P2;
    int16_t  _dig_P3;
    int16_t  _dig_P4;
    int16_t  _dig_P5;
    int16_t  _dig_P6;
    int16_t  _dig_P7;
    int16_t  _dig_P8;
    int16_t  _dig_P9;
};
