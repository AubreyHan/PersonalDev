#include "HapticDriver.h"
#include <M5Unified.h>
#include "../model/SystemState.h"

HapticDriver& HapticDriver::instance() {
    static HapticDriver s_instance;
    return s_instance;
}

bool HapticDriver::begin() {
    if (!M5.Speaker.isEnabled()) {
        _initialized = false;
        return false;
    }

    uint8_t vol = HapticConfig::SPEAKER_DEFAULT_VOL;
    {
        StateGuard guard(SystemState::instance(), pdMS_TO_TICKS(10));
        if (guard.locked()) {
            vol = SystemState::instance().data().settings.speakerVolume;
        }
    }
    M5.Speaker.setVolume(vol);
    _lastAppliedVolume = vol;
    _stepCount         = 0;
    _stepIndex         = 0;
    _initialized       = true;
    return true;
}

bool HapticDriver::isBeepAllowed(uint8_t& outVolume) const {
    if (!M5.Speaker.isEnabled()) {
        return false;
    }
    bool    enabled = true;
    uint8_t vol     = HapticConfig::SPEAKER_DEFAULT_VOL;
    {
        StateGuard guard(SystemState::instance(), pdMS_TO_TICKS(5));
        if (guard.locked()) {
            const UserSettings& cfg = SystemState::instance().data().settings;
            enabled = cfg.speakerBeepEnabled;
            vol     = cfg.speakerVolume;
        }
    }
    outVolume = vol;
    return enabled && (vol > 0U);
}

void HapticDriver::enqueueSteps(const ToneStep* steps, uint8_t count, uint8_t volume) {
    if (steps == nullptr || count == 0U) {
        return;
    }
    if (!_initialized && !begin()) {
        return;
    }
    if (volume != _lastAppliedVolume) {
        M5.Speaker.setVolume(volume);
        _lastAppliedVolume = volume;
    }

    const uint8_t capped = (count > MAX_STEPS) ? static_cast<uint8_t>(MAX_STEPS) : count;
    for (uint8_t i = 0; i < capped; ++i) {
        _steps[i] = steps[i];
    }
    _stepCount = capped;
    _stepIndex = 0;
    triggerCurrentStep();
}

void HapticDriver::triggerCurrentStep() {
    if (_stepIndex >= _stepCount) {
        return;
    }
    const ToneStep& step = _steps[_stepIndex];
    _stepStartMs = millis();
    if (step.freqHz > 0U && step.durationMs > 0U) {
        // Channel 0 with stop_current_sound = true preempts any stale click cleanly
        M5.Speaker.tone(static_cast<float>(step.freqHz),
                        static_cast<uint32_t>(step.durationMs),
                        0,
                        true);
    }
}

void HapticDriver::update() {
    if (_stepIndex >= _stepCount) {
        return;
    }
    const uint32_t nowMs   = millis();
    const uint32_t elapsed = nowMs - _stepStartMs;
    if (elapsed >= _steps[_stepIndex].durationMs) {
        _stepIndex++;
        if (_stepIndex < _stepCount) {
            triggerCurrentStep();
        } else {
            _stepCount = 0;
            _stepIndex = 0;
        }
    }
}

void HapticDriver::playPattern(HapticPattern pattern) {
    switch (pattern) {
        case HAPTIC_TAP:        playTap();       break;
        case HAPTIC_SWIPE:      playSwipe();     break;
        case HAPTIC_CONFIRM:    playConfirm();   break;
        case HAPTIC_TRIP_RESET: playTripReset(); break;
        case HAPTIC_WARNING:    playWarning();   break;
        default:                break;
    }
}

void HapticDriver::playTap() {
    uint8_t vol = 0;
    if (!isBeepAllowed(vol)) {
        return;
    }
    const ToneStep seq[1] = {
        { HapticConfig::TONE_TAP_HZ, static_cast<uint16_t>(HapticConfig::TONE_TAP_MS) }
    };
    enqueueSteps(seq, 1, vol);
}

void HapticDriver::playSwipe() {
    uint8_t vol = 0;
    if (!isBeepAllowed(vol)) {
        return;
    }
    const ToneStep seq[1] = {
        { HapticConfig::TONE_SWIPE_HZ, static_cast<uint16_t>(HapticConfig::TONE_SWIPE_MS) }
    };
    enqueueSteps(seq, 1, vol);
}

void HapticDriver::playConfirm() {
    uint8_t vol = 0;
    if (!isBeepAllowed(vol)) {
        return;
    }
    const ToneStep seq[3] = {
        { HapticConfig::TONE_CONFIRM_1_HZ, static_cast<uint16_t>(HapticConfig::TONE_CONFIRM_MS) },
        { 0U,                              18U },
        { HapticConfig::TONE_CONFIRM_2_HZ, static_cast<uint16_t>(HapticConfig::TONE_CONFIRM_MS + 15U) }
    };
    enqueueSteps(seq, 3, vol);
}

void HapticDriver::playTripReset() {
    uint8_t vol = 0;
    if (!isBeepAllowed(vol)) {
        return;
    }
    // Distinct rising two-tone confirmation chime when 2.0s TRIP hold completes
    const ToneStep seq[3] = {
        { HapticConfig::TONE_CONFIRM_1_HZ,  70U },
        { 0U,                               25U },
        { HapticConfig::TONE_TRIP_RESET_HZ, static_cast<uint16_t>(HapticConfig::TONE_TRIP_RESET_MS) }
    };
    enqueueSteps(seq, 3, vol);
}

void HapticDriver::playWarning() {
    uint8_t vol = 0;
    if (!isBeepAllowed(vol)) {
        return;
    }
    const ToneStep seq[3] = {
        { HapticConfig::TONE_WARN_HZ, static_cast<uint16_t>(HapticConfig::TONE_WARN_MS) },
        { 0U,                         65U },
        { HapticConfig::TONE_WARN_HZ, static_cast<uint16_t>(HapticConfig::TONE_WARN_MS) }
    };
    enqueueSteps(seq, 3, vol);
}

void HapticDriver::playCustomTone(uint16_t freqHz, uint32_t durationMs) {
    uint8_t vol = 0;
    if (!isBeepAllowed(vol) || freqHz == 0U || durationMs == 0UL) {
        return;
    }
    const uint16_t cappedMs = (durationMs > 5000UL) ? 5000U : static_cast<uint16_t>(durationMs);
    const ToneStep seq[1] = { { freqHz, cappedMs } };
    enqueueSteps(seq, 1, vol);
}

void HapticDriver::stop() {
    _stepCount = 0;
    _stepIndex = 0;
    if (M5.Speaker.isEnabled()) {
        M5.Speaker.stop(0);
    }
}
