#pragma once
#include "ui/pages/BasePage.h"

class Page1Dashboard : public BasePage {
public:
    void render(M5Canvas& canvas, const SystemContext& ctx) override;
    bool onTouchTap(int16_t x, int16_t y, SystemContext& ctx) override;
    void onTouchHold(int16_t x, int16_t y, uint32_t holdMs, SystemContext& ctx) override;

private:
    bool _resetTriggeredThisHold = false;
};
