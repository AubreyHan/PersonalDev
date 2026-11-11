#pragma once

#include "BasePage.h"

class Page4Trail : public BasePage {
public:
    Page4Trail() = default;
    ~Page4Trail() override;

    PageId pageId() const override { return PAGE_4_TRAIL; }
    const char* pageTitle() const override { return "VECTOR TRAIL MAP"; }

    void onEnter(const SystemStateSnapshot& state) override;
    void render(M5Canvas& canvas, const SystemStateSnapshot& state) override;
    bool onTouch(const TouchEvent& event, const SystemStateSnapshot& state) override;

private:
    TrailPoint*      _points            = nullptr;
    size_t           _pointCount        = 0;
    TrailPreviewMeta _cachedMeta        = {};
    TripCategory     _cachedCategory    = TRIP_CATEGORY_COUNT;
    uint32_t         _cachedMetaVersion = 0;
    uint32_t         _lastResetVersion  = 0;

    bool             _followMode        = false; // false = Auto-Fit Overview, true = Vehicle-Centered Follow
    uint8_t          _followZoomIdx     = 1;     // 0=250m, 1=600m, 2=1500m half-width in follow mode

    void ensureTrailBuffer();
    void syncTrailCache(const SystemStateSnapshot& state, TripCategory cat);
    static TripCategory effectiveCategory(const SystemStateSnapshot& state);

    static void drawDashedSegment(M5Canvas& canvas, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                                  float width, uint32_t color);
    static void drawStartFlag(M5Canvas& canvas, int16_t x, int16_t y);
    static void drawVehicleChevron(M5Canvas& canvas, int16_t cx, int16_t cy, float headingDeg, uint32_t color);

    void drawTacticalGrid(M5Canvas& canvas);
    void drawCornerHuds(M5Canvas& canvas, const SystemStateSnapshot& state, TripCategory cat, float metersPerPx);
};
