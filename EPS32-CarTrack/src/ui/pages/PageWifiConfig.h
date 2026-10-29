#pragma once
#include "ui/pages/BasePage.h"

class PageWifiConfig : public BasePage {
public:
    PageWifiConfig() {}
    void draw(M5Canvas& canvas, const SystemContext& ctx) override;
    void handleTap(int16_t x, int16_t y) override;
};
