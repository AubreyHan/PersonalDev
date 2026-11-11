#pragma once

#include "BasePage.h"

class Page5Dynamics : public BasePage {
public:
    Page5Dynamics() = default;
    ~Page5Dynamics() override = default;

    PageId pageId() const override { return PAGE_5_DYNAMICS; }
    const char* pageTitle() const override { return "车辆动态与越野姿态"; }

    void onEnter(const SystemStateSnapshot& state) override;
    void render(M5Canvas& canvas, const SystemStateSnapshot& state) override;
    bool onTouch(const TouchEvent& event, const SystemStateSnapshot& state) override;

private:
    static constexpr int16_t BTN_CALIB_X = 532;
    static constexpr int16_t BTN_CALIB_Y = 598;
    static constexpr int16_t BTN_CALIB_W = 572;
    static constexpr int16_t BTN_CALIB_H = 80;

    bool     _calibPressed  = false;
    uint32_t _lastCalibMs   = 0;

    void drawGForceTarget(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawSlopeCard(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawAttitudeHorizonCard(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawDualImuStatusStrip(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawCalibrateButton(M5Canvas& canvas, const SystemStateSnapshot& state) const;
};
