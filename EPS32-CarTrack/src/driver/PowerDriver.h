#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include "model/SystemState.h"

class PowerDriver {
public:
    static PowerDriver& instance() {
        static PowerDriver inst;
        return inst;
    }

    void init();
    void update();
    void vibrate(uint16_t durationMs = 100);
    void enterDeepSleep();
    void setBacklight(uint8_t brightness);

private:
    PowerDriver() : _lastCheckTime(0), _lastVbus(true), _countdownStartTime(0) {}
    unsigned long _lastCheckTime;
    bool          _lastVbus;
    unsigned long _countdownStartTime;
};
