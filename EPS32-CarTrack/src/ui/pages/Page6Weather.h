#pragma once
#include "ui/pages/BasePage.h"

class Page6Weather : public BasePage {
public:
    void render(M5Canvas& canvas, const SystemContext& ctx) override;
    bool onTouchTap(int16_t x, int16_t y, SystemContext& ctx) override;

private:
    bool _showSecond12Hours = false;
};
