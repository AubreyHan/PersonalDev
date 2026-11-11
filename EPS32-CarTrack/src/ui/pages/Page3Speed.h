#pragma once

#include "BasePage.h"

class Page3Speed : public BasePage {
public:
    Page3Speed() = default;
    ~Page3Speed() override = default;

    PageId pageId() const override { return PAGE_3_SPEED; }
    const char* pageTitle() const override { return "SPEED ANALYTICS"; }

    void render(M5Canvas& canvas, const SystemStateSnapshot& state) override;
    bool onTouch(const TouchEvent& event, const SystemStateSnapshot& state) override;

private:
    static TripCategory effectiveCategory(const SystemStateSnapshot& state);

    void drawHeaderAndTabs(M5Canvas& canvas, const SystemStateSnapshot& state, TripCategory cat);
    void drawSpeedHeroCards(M5Canvas& canvas, const SystemStateSnapshot& state, const TripStats& stats);
    void drawSpeedChart1050(M5Canvas& canvas, const SystemStateSnapshot& state, const TripStats& stats);
    void drawSpeedStructureBar(M5Canvas& canvas, const TripStats& stats);
};
