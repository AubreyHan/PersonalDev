#pragma once

#include "../BasePage.h"

class ToolSettings : public BasePage {
public:
    ToolSettings() = default;
    ~ToolSettings() override = default;

    PageId pageId() const override { return SUBPAGE_TOOL_SETTINGS; }
    const char* pageTitle() const override { return "系统偏好与 INA226 库仑计监控"; }
    bool allowsHorizontalSwipe() const override { return false; }

    void onEnter(const SystemStateSnapshot& state) override;
    void render(M5Canvas& canvas, const SystemStateSnapshot& state) override;
    bool onTouch(const TouchEvent& event, const SystemStateSnapshot& state) override;

private:
    static constexpr int16_t BTN_BACK_X        = 18;
    static constexpr int16_t BTN_BACK_Y        = 12;
    static constexpr int16_t BTN_BACK_W        = 190;
    static constexpr int16_t BTN_BACK_H        = 56;

    // Left Column Controls (X = 18, W = 526)
    static constexpr int16_t SLIDER_HIT_X      = 38;
    static constexpr int16_t SLIDER_HIT_Y      = 136;
    static constexpr int16_t SLIDER_HIT_W      = 486;
    static constexpr int16_t SLIDER_HIT_H      = 68;

    static constexpr int16_t BTN_AUTOBRIGHT_X  = 38;
    static constexpr int16_t BTN_AUTOBRIGHT_Y  = 220;
    static constexpr int16_t BTN_AUTOBRIGHT_W  = 486;
    static constexpr int16_t BTN_AUTOBRIGHT_H  = 62;

    static constexpr int16_t BTN_BEEP_X        = 38;
    static constexpr int16_t BTN_BEEP_Y        = 296;
    static constexpr int16_t BTN_BEEP_W        = 486;
    static constexpr int16_t BTN_BEEP_H        = 62;

    // Manual SUB Trip Quick Controls
    static constexpr int16_t BTN_SUB_ACT_X     = 38;
    static constexpr int16_t BTN_SUB_ACT_Y     = 478;
    static constexpr int16_t BTN_SUB_ACT_W     = 236;
    static constexpr int16_t BTN_SUB_ACT_H     = 76;

    static constexpr int16_t BTN_SUB_RESET_X   = 288;
    static constexpr int16_t BTN_SUB_RESET_Y   = 478;
    static constexpr int16_t BTN_SUB_RESET_W   = 236;
    static constexpr int16_t BTN_SUB_RESET_H   = 76;

    bool     _draggingSlider  = false;
    bool     _armedSubReset   = false;
    uint32_t _armedSubResetMs = 0;

    void updateBrightnessFromTouchX(int16_t localX, bool persistNvs);
    void drawTopHeader(M5Canvas& canvas) const;
    void drawDisplayAndAudioCard(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawSubTripQuickCard(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawIna226DashboardCard(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawFirmwareSysInfoCard(M5Canvas& canvas, const SystemStateSnapshot& state) const;
};
