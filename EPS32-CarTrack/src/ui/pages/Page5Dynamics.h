#pragma once
#include "ui/pages/BasePage.h"

class Page5Dynamics : public BasePage {
public:
    void render(M5Canvas& canvas, const SystemContext& ctx) override;

private:
    float _trailGx[15] = {0};
    float _trailGy[15] = {0};
    uint8_t _trailIdx  = 0;
};
