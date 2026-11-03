#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include "config.h"

class HapticDriver {
public:
    static HapticDriver& instance() {
        static HapticDriver inst;
        return inst;
    }

    void triggerShortPulse(uint32_t durationMs = TripResetConfig::VIBRATION_FEEDBACK_MS) {
        // Tab5 has no vibration motor on PMIC and M5.Speaker.tone deadlocks the FreeRTOS uiTask on ESP32-P4.
        // Keep this method completely non-blocking and safe from buffer overflows.
        _pulseActive = true;
        _pulseEndMs  = millis() + durationMs;
    }

    void triggerAlertBeep() {
    }

    void update() {
        if (_pulseActive && millis() >= _pulseEndMs) {
            _pulseActive = false;
        }
    }

private:
    HapticDriver() = default;
    bool     _pulseActive = false;
    uint32_t _pulseEndMs  = 0;
};
