#pragma once

#include "BasePage.h"

class Page1Dashboard : public BasePage {
public:
    Page1Dashboard() = default;
    ~Page1Dashboard() override;

    PageId pageId() const override { return PAGE_1_DASHBOARD; }
    const char* pageTitle() const override { return "DASHBOARD"; }

    void onEnter(const SystemStateSnapshot& state) override;
    void onExit() override;
    void render(M5Canvas& canvas, const SystemStateSnapshot& state) override;
    bool onTouch(const TouchEvent& event, const SystemStateSnapshot& state) override;

private:
    M5Canvas _speedSprite;
    bool     _speedSpriteReady    = false;

    // 2.0s Long-press TRIP reset state
    bool     _tripHoldActive      = false;
    bool     _tripResetTriggered  = false;
    uint32_t _tripHoldElapsedMs   = 0;

    // Non-blocking second chime of rising dual-tone feedback
    bool     _pendingSecondTone   = false;
    uint32_t _secondToneDueMs     = 0;

    void ensureSpeedSprite();
    static TripCategory effectiveCategory(const SystemStateSnapshot& state);
    static const char* headingToCn8(float deg);
    static const char* headingToEn8(float deg);

    void drawTopAlertAndTunnelHud(M5Canvas& canvas, const SystemStateSnapshot& state, uint32_t nowMs);
    void drawCompass360(M5Canvas& canvas, const SystemStateSnapshot& state);
    void drawMiniDynamicsCard(M5Canvas& canvas, const SystemStateSnapshot& state);
    void drawHeroSpeedometer(M5Canvas& canvas, const SystemStateSnapshot& state);
    void drawAltitudeAndSlopeCard(M5Canvas& canvas, const SystemStateSnapshot& state);
    void drawTripDimensionBar(M5Canvas& canvas, const SystemStateSnapshot& state);
    void drawTripCardAndResetRing(M5Canvas& canvas, const SystemStateSnapshot& state, uint32_t nowMs);
};
