#pragma once
#include "ui/pages/BasePage.h"

class Page2Altitude : public BasePage {
public:
    void render(M5Canvas& canvas, const SystemContext& ctx) override;
};
