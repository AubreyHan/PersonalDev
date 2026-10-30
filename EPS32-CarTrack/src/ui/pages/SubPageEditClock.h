#pragma once
#include "ui/pages/BasePage.h"

class SubPageEditClock : public BasePage {
public:
    void render(M5Canvas& canvas, const SystemContext& ctx) override;
    bool onTouchTap(int16_t x, int16_t y, SystemContext& ctx) override;

private:
    uint8_t _regionTab = 0; // 0: Asia/Pacific, 1: Americas, 2: Europe/ME, 3: UTC
};
