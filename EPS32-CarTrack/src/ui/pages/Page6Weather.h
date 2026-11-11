#pragma once

#include "BasePage.h"

class Page6Weather : public BasePage {
public:
    Page6Weather() = default;
    ~Page6Weather() override = default;

    PageId pageId() const override { return PAGE_6_WEATHER; }
    const char* pageTitle() const override { return "实时与12小时气象预报"; }

    void onEnter(const SystemStateSnapshot& state) override;
    void render(M5Canvas& canvas, const SystemStateSnapshot& state) override;
    bool onTouch(const TouchEvent& event, const SystemStateSnapshot& state) override;

private:
    // Toggle button hitbox for [前12小时 (0~11h) / 后12小时 (12~23h)]
    static constexpr int16_t BTN_TOGGLE_X = 724;
    static constexpr int16_t BTN_TOGGLE_W = 380;
    static constexpr int16_t BTN_TOGGLE_H = 62;

    // Refresh button hitbox
    static constexpr int16_t BTN_REFRESH_X = 568;
    static constexpr int16_t BTN_REFRESH_W = 142;
    static constexpr int16_t BTN_REFRESH_H = 62;

    bool     _showBack12h     = false; // false = Front 12h (0..11), true = Back 12h (12..23)
    uint32_t _lastToggleTapMs = 0;
    uint32_t _lastRefreshMs   = 0;

    static bool isHighRiskDrivingWmo(uint8_t wmoCode);
    static const char* wmoToZhDesc(uint8_t wmoCode);
    static const char* windDegToZhDir(uint16_t deg);
    static void drawWmoVectorIcon(M5Canvas& canvas, int16_t cx, int16_t cy, int16_t radius,
                                  uint8_t wmoCode, bool isDay);

    int16_t currentToggleY(const SystemStateSnapshot& state) const;
    void drawSevereTopAlertBar(M5Canvas& canvas, const SystemStateSnapshot& state, int16_t y, int16_t h) const;
    void drawCurrentWeatherCard(M5Canvas& canvas, const SystemStateSnapshot& state, int16_t y, int16_t h) const;
    void drawHourlyMatrix(M5Canvas& canvas, const SystemStateSnapshot& state, int16_t y, int16_t h) const;
    void drawOfflinePlaceholder(M5Canvas& canvas, const SystemStateSnapshot& state, int16_t y, int16_t h) const;
    void drawBottomTrendFooter(M5Canvas& canvas, const SystemStateSnapshot& state, int16_t y, int16_t h) const;
};
