#pragma once
#include "ui/pages/BasePage.h"

class Page4Trail : public BasePage {
public:
    Page4Trail() {}
    void draw(M5Canvas& canvas, const SystemContext& ctx) override;
    void handleTap(int16_t x, int16_t y) override;
};
