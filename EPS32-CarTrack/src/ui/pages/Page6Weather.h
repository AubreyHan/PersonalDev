#pragma once
#include "ui/pages/BasePage.h"

class Page6Weather : public BasePage {
public:
    Page6Weather() {}
    void draw(M5Canvas& canvas, const SystemContext& ctx) override;
    void handleSwipeUp() override;
    void handleSwipeDown() override;
};
