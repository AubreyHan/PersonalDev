#pragma once
#include "ui/pages/BasePage.h"

class ToolWifi : public BasePage {
public:
    void render(M5Canvas& canvas, const SystemContext& ctx) override;
    bool onTouchTap(int16_t x, int16_t y, SystemContext& ctx) override;
};
