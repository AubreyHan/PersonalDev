#include "driver/PowerDriver.h"
#include <esp_sleep.h>

void PowerDriver::init() {
    // Configure default backlight
    setBacklight(180);
    _lastVbus = M5.Power.getVBUSVoltage() > 4000;
}

void PowerDriver::setBacklight(uint8_t brightness) {
    M5.Display.setBrightness(brightness);
}

void PowerDriver::vibrate(uint16_t durationMs) {
    M5.Power.setVibration(200);
    delay(durationMs);
    M5.Power.setVibration(0);
}

void PowerDriver::update() {
    unsigned long now = millis();
    if (now - _lastCheckTime < 200) return;
    _lastCheckTime = now;

    // Check VBUS (USB external 5V): require both voltage drop and AXP192 VBUS status bit false
    int16_t vbusMv = M5.Power.getVBUSVoltage();
    bool currentVbus = (vbusMv > 4000) || M5.Power.Axp192.isVBUS();
    
    SystemState& state = SystemState::instance();
    state.lock();
    SystemContext& ctx = state.getContextUnsafe();
    ctx.vbusConnected = currentVbus;

    // Detect VBUS disconnect (only trigger auto-shutdown dialog if a track was actively being logged)
    if (_lastVbus && !currentVbus) {
        _countdownStartTime = now;
        if (ctx.isLogging && ctx.batteryMode == BATTERY_MODE_BASE_ATTACHED) {
            ctx.currentState = STATE_PWR_OFF_WARN;
            ctx.pwrCountdownSec = 10;
        }
    } else if (!_lastVbus && currentVbus) {
        // Reconnected external power
        if (ctx.currentState == STATE_PWR_OFF_WARN) {
            ctx.currentState = STATE_MAIN_APP;
        }
    }
    _lastVbus = currentVbus;

    // Handle 10-second shutdown countdown
    if (ctx.currentState == STATE_PWR_OFF_WARN) {
        int elapsedSec = (now - _countdownStartTime) / 1000;
        int remaining = 10 - elapsedSec;
        if (remaining <= 0) {
            ctx.pwrCountdownSec = 0;
            state.unlock();
            enterDeepSleep();
            return;
        } else {
            ctx.pwrCountdownSec = remaining;
        }
    }
    state.unlock();
}

void PowerDriver::enterDeepSleep() {
    // Safe shutdown
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setBrightness(0);
    delay(100);
    M5.Power.powerOff();
    esp_deep_sleep_start();
}
