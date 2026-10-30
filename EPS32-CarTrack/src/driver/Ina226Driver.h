#pragma once
#include <Arduino.h>
#include "config.h"

class Ina226Driver {
public:
    bool init();
    bool readMeasurements(float& busVoltV, float& shuntVoltMv, float& currentMa, float& powerMw);
    bool isAvailable() const { return _isAvailable; }

private:
    bool    _isAvailable = false;
    uint8_t _i2cAddr     = Ina226Config::I2C_ADDR;
};
