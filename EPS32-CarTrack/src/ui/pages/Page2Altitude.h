#pragma once
#include "ui/pages/BasePage.h"

class Page2Altitude : public BasePage {
public:
    Page2Altitude() {}
    void draw(M5Canvas& canvas, const SystemContext& ctx) override;
};
