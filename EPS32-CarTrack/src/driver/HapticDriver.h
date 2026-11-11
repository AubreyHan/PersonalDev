#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "../config.h"

// =============================================================================
// HapticDriver - Lightweight Non-Blocking Audio-Tactile Feedback Driver
// - M5Stack Tab5 has NO internal vibration motor (Board #3 Correction #4).
// - Uses onboard ES8388 + NS4150B amplifier via M5.Speaker.tone(...) to provide
//   crisp button clicks, swipe ticks, 2s TRIP reset dual-tone confirmation, and
//   safety alert chimes without ever blocking FreeRTOS/UI threads.
// - Strictly respects UserSettings::speakerBeepEnabled & speakerVolume.
// =============================================================================

enum HapticPattern : uint8_t {
    HAPTIC_TAP        = 0, // Short crisp click (Dock / card tap)
    HAPTIC_SWIPE      = 1, // Subtle page transition tick
    HAPTIC_CONFIRM    = 2, // Rising dual-tone confirmation (Save / calibration OK)
    HAPTIC_TRIP_RESET = 3, // 2.0s long-press TRIP reset completion dual-tone chime
    HAPTIC_WARNING    = 4  // Double alert tone (Steep slope / RF jamming / low battery)
};

class HapticDriver {
public:
    static HapticDriver& instance();

    HapticDriver() = default;

    // Sync speaker volume & readiness (assumes M5.begin() already initialized ES8388/Speaker)
    bool begin();

    // Non-blocking step sequencer: call from UI loop or periodic task (zero delay()!)
    void update();

    // Pattern triggers (all non-blocking, auto-gated by UserSettings::speakerBeepEnabled)
    void playPattern(HapticPattern pattern);
    void playTap();
    void playSwipe();
    void playConfirm();
    void playTripReset();
    void playWarning();
    void playCustomTone(uint16_t freqHz, uint32_t durationMs);

    void stop();
    bool isPlaying() const { return _stepIndex < _stepCount; }

private:
    struct ToneStep {
        uint16_t freqHz;     // 0 = silent gap between tones
        uint16_t durationMs; // Step hold duration in ms
    };

    static constexpr size_t MAX_STEPS = 4;

    bool isBeepAllowed(uint8_t& outVolume) const;
    void enqueueSteps(const ToneStep* steps, uint8_t count, uint8_t volume);
    void triggerCurrentStep();

    bool     _initialized       = false;
    uint8_t  _lastAppliedVolume = 0xFFU;
    ToneStep _steps[MAX_STEPS]  = {};
    uint8_t  _stepCount         = 0;
    uint8_t  _stepIndex         = 0;
    uint32_t _stepStartMs       = 0;
};
