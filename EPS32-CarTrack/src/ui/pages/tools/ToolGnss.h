#pragma once

#include "../BasePage.h"

class ToolGnss : public BasePage {
public:
    ToolGnss() = default;
    ~ToolGnss() override = default;

    PageId pageId() const override { return SUBPAGE_TOOL_GNSS; }
    const char* pageTitle() const override { return "GPS 四星星空图与射频诊断"; }
    bool allowsHorizontalSwipe() const override { return false; }

    void onEnter(const SystemStateSnapshot& state) override;
    void render(M5Canvas& canvas, const SystemStateSnapshot& state) override;
    bool onTouch(const TouchEvent& event, const SystemStateSnapshot& state) override;

private:
    static constexpr int16_t BTN_BACK_X = 18;
    static constexpr int16_t BTN_BACK_Y = 12;
    static constexpr int16_t BTN_BACK_W = 184;
    static constexpr int16_t BTN_BACK_H = 54;

    static constexpr int16_t BTN_AGPS_X = 880;
    static constexpr int16_t BTN_AGPS_Y = 618;
    static constexpr int16_t BTN_AGPS_W = 206;
    static constexpr int16_t BTN_AGPS_H = 68;

    bool _agpsPressed = false;

    void drawHeaderBar(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawSkyplotPanel(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawCnoBarChart(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawDopAndFilterCard(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawMonRfAndAgpsCard(M5Canvas& canvas, const SystemStateSnapshot& state) const;
};
