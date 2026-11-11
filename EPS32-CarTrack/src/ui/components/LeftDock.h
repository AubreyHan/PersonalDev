#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include "../../config.h"
#include "../../model/SystemState.h"
#include "../pages/BasePage.h"

class LeftDock {
public:
    LeftDock() = default;
    ~LeftDock() = default;

    void setPressedZone(DockTouchAction zone) { _pressedZone = zone; }
    DockTouchAction pressedZone() const { return _pressedZone; }

    // Render permanent 160x720 sidebar onto the dedicated PSRAM dock canvas (local 0..159 x 0..719)
    void render(M5Canvas& canvas, const SystemStateSnapshot& state, PageId activePage);

private:
    DockTouchAction _pressedZone = DOCK_ACTION_NONE;

    void drawZoneChrome(M5Canvas& canvas, int16_t y, int16_t h, bool isActive, bool isPressed, uint32_t accentRgb888);

    // Crisp automotive vector HUD icons (immune to BMP-only font emoji limitations)
    void drawHomeIcon(M5Canvas& canvas, int16_t cx, int16_t cy, uint32_t color);
    void drawSatelliteIcon(M5Canvas& canvas, int16_t cx, int16_t cy, uint32_t color);
    void drawWifiIcon(M5Canvas& canvas, int16_t cx, int16_t cy, int8_t bars, uint32_t color, bool apMode);
    void drawBatteryDualColor(M5Canvas& canvas, int16_t cx, int16_t cy, float socPct, bool isExtPower, bool isCharging, uint32_t color);
    void drawWeatherIcon(M5Canvas& canvas, int16_t cx, int16_t cy, uint8_t wmoCode, bool isDay, bool valid);
    void drawGearIcon(M5Canvas& canvas, int16_t cx, int16_t cy, uint32_t color);
};
