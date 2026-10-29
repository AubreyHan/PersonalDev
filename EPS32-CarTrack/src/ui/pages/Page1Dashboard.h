#pragma once
#include "ui/pages/BasePage.h"

class Page1Dashboard : public BasePage {
public:
    Page1Dashboard() : _resetFeedbackMs(0) {}
    void draw(M5Canvas& canvas, const SystemContext& ctx) override;
    void handleTap(int16_t x, int16_t y) override;
    void handleLongPress(int16_t x, int16_t y) override;

private:
    unsigned long _resetFeedbackMs;

    void drawCompass(M5Canvas& canvas, float headingDeg);
};
