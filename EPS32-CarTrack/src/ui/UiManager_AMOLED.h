#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <qrcode.h>
#include "config_s3_amoled.h"

enum UiPage : uint8_t {
    PAGE_ALTITUDE = 0,
    PAGE_SPEED    = 1,
    PAGE_COMPASS  = 2,
    PAGE_WIFI     = 3,
    PAGE_COUNT    = 4
};

class UiManager_AMOLED {
public:
    static UiManager_AMOLED& instance() {
        static UiManager_AMOLED inst;
        return inst;
    }

    bool init();
    void update();

    // Page navigation
    void nextPage();
    void prevPage();
    void setPage(UiPage page);
    UiPage getCurrentPage() const { return _currentPage; }

    // Touch event input
    void handleTouch(int16_t x, int16_t y, bool isPressed);

private:
    UiManager_AMOLED() = default;

    void renderStatusBar();
    void renderPageAltitude();
    void renderPageSpeed();
    void renderPageCompass();
    void renderPageWiFi();

    // Sub-renderers
    void drawLineChart(int16_t x, int16_t y, int16_t w, int16_t h,
                       const float* data, uint16_t count,
                       float minVal, float maxVal, uint16_t lineColor, uint16_t fillColor);
    void drawQrCode(int16_t cx, int16_t cy, int16_t size, const char* text);

    Arduino_DataBus* _bus = nullptr;
    Arduino_GFX*     _gfx = nullptr;

    UiPage _currentPage = PAGE_ALTITUDE;
    uint32_t _lastRenderMs = 0;

    // Touch state tracking
    bool    _touchDown = false;
    int16_t _touchStartX = 0;
    int16_t _touchStartY = 0;
};
