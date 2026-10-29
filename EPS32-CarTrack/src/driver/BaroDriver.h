#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include "config.h"

class BaroDriver {
public:
    static BaroDriver& instance() {
        static BaroDriver inst;
        return inst;
    }

    bool init(TwoWire* wire = &Wire1);
    void update();

    bool isAvailable() const { return _isAvailable; }
    float getPressureHpa() const { return _lastPressureHpa; }
    float getTemperatureC() const { return _lastTempC; }
    float getRawAltitudeM() const { return _lastRawAltitudeM; }

private:
    BaroDriver() : _bmp(&Wire1), _isAvailable(false), _lastPressureHpa(1013.25f), _lastTempC(25.0f), _lastRawAltitudeM(0.0f), _lastSampleTime(0) {}
    Adafruit_BMP280 _bmp;
    bool            _isAvailable;
    float           _lastPressureHpa;
    float           _lastTempC;
    float           _lastRawAltitudeM;
    unsigned long   _lastSampleTime;
};
