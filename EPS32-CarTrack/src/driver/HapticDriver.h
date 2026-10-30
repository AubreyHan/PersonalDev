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
        M5.Power.setVibration(200);
        if (M5.Speaker.isEnabled()) {
            M5.Speaker.tone(1800, durationMs);
        }
        _pulseActive = true;
        _pulseEndMs  = millis() + durationMs;
    }

    void triggerAlertBeep() {
        if (M5.Speaker.isEnabled()) {
            M5.Speaker.tone(2400, 120);
        }
    }

    void update() {
        if (_pulseActive && millis() >= _pulseEndMs) {
            M5.Power.setVibration(0);
            _pulseActive = false;
        }
    }

private:
    HapticDriver() = default;
    bool     _pulseActive = false;
    uint32_t _pulseEndMs  = 0;
};
