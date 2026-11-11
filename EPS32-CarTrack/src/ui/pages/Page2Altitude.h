#pragma once

#include "BasePage.h"

class Page2Altitude : public BasePage {
public:
    Page2Altitude() = default;
    ~Page2Altitude() override = default;

    PageId pageId() const override { return PAGE_2_ALTITUDE; }
    const char* pageTitle() const override { return "ALTITUDE PROFILE"; }

    void render(M5Canvas& canvas, const SystemStateSnapshot& state) override;
    bool onTouch(const TouchEvent& event, const SystemStateSnapshot& state) override;

private:
    static TripCategory effectiveCategory(const SystemStateSnapshot& state);

    void drawHeaderAndTabs(M5Canvas& canvas, const SystemStateSnapshot& state, TripCategory cat);
    void drawTopMetricsAndExtremes(M5Canvas& canvas, const SystemStateSnapshot& state, const TripStats& stats);
    void drawAltitudeChart1050(M5Canvas& canvas, const SystemStateSnapshot& state, const TripStats& stats);
};
