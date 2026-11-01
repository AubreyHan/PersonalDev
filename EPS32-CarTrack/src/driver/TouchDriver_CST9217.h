#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <TouchDrvCSTXXX.hpp>
#include "config_s3_amoled.h"

class TouchDriver_CST9217 {
public:
    static TouchDriver_CST9217& instance() {
        static TouchDriver_CST9217 inst;
        return inst;
    }

    bool init();
    void update();

    bool isOnline() const { return _isOnline; }

private:
    TouchDriver_CST9217() = default;
    bool readRawCoordinates(int16_t& outX, int16_t& outY);

    TouchDrvCST92xx _touch;
    bool _isOnline = false;
    bool _useSensorLib = false;
    bool _wasPressed = false;
    int16_t _lastX = 0;
    int16_t _lastY = 0;
    uint32_t _lastPollMs = 0;
};
