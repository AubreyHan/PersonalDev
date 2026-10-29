#pragma once
#include "ui/pages/BasePage.h"
#include "ui/pages/SubPageEditClock.h"

class Page7WorldClock : public BasePage {
public:
    Page7WorldClock() {}
    void draw(M5Canvas& canvas, const SystemContext& ctx) override;
    void handleTap(int16_t x, int16_t y) override;
    void handleSwipeUp() override;
    void handleSwipeDown() override;

private:
    SubPageEditClock _subEdit;

    void drawClockCard(M5Canvas& canvas, int x, int y, int w, int h, uint8_t slotIdx, const WorldClockSlotConfig& slot);
};
