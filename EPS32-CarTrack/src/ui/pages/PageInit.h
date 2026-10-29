#pragma once
#include "ui/pages/BasePage.h"

class PageInit : public BasePage {
public:
    PageInit() : _radarAngle(0) {}
    void init() override;
    void draw(M5Canvas& canvas, const SystemContext& ctx) override;
    void handleTap(int16_t x, int16_t y) override;

private:
    float _radarAngle;
};
