#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include "../../config.h"
#include "../pages/BasePage.h"

class PageIndicator {
public:
    PageIndicator() = default;
    ~PageIndicator() = default;

    // Wake floating OSD indicator on page swipe or viewport touch
    void notifyInteraction(uint32_t nowMs) {
        _lastActiveMs = nowMs;
        _everShown    = true;
    }

    void hideImmediately() { _everShown = false; }

    void setDotCount(uint8_t dotCount) {
        _dotCount = (dotCount >= 1U && dotCount <= 12U) ? dotCount : 8U;
    }

    bool isVisible(uint32_t nowMs) const {
        if (!_everShown) {
            return false;
        }
        return (nowMs - _lastActiveMs) < Viewport::INDICATOR_AUTO_HIDE_MS;
    }

    // Render onto the right 1120x720 main viewport canvas; draws nothing if >= 3000ms idle
    void render(M5Canvas& canvas, PageId activePage, uint32_t nowMs);

private:
    uint32_t _lastActiveMs = 0;
    uint8_t  _dotCount     = 8;
    bool     _everShown    = false;
};
