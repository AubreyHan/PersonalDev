#pragma once
#include "ui/pages/BasePage.h"

class Page3Speed : public BasePage {
public:
    Page3Speed() {}
    void draw(M5Canvas& canvas, const SystemContext& ctx) override;
};
