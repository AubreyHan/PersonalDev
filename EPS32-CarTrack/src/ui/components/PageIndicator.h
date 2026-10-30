#pragma once
#include <M5GFX.h>
#include "model/SystemState.h"

class PageIndicator {
public:
    void notifyInteraction() {
        _lastActiveMs = millis();
        _visible      = true;
    }

    void render(M5Canvas& canvas, AppPageId currentPage) {
        if (!_visible) return;
        if (millis() - _lastActiveMs > Viewport::INDICATOR_AUTO_HIDE_MS) {
            _visible = false;
            return;
        }
        if (currentPage < PAGE_1_DASHBOARD || currentPage > PAGE_8_TOOLS) {
            return;
        }

        int activeIdx = (int)currentPage - (int)PAGE_1_DASHBOARD; // 0..7
        int centerX   = Viewport::SAFE_X + Viewport::SAFE_W / 2;
        int startX    = centerX - (8 * 24) / 2;
        int y         = Viewport::INDICATOR_OSD_Y;

        canvas.fillRoundRect(startX - 18, y - 10, 8 * 24 + 24, 22, 11, UiColors::COLOR_PANEL_BG);
        canvas.drawRoundRect(startX - 18, y - 10, 8 * 24 + 24, 22, 11, UiColors::COLOR_BORDER);

        for (int i = 0; i < 8; i++) {
            int cx = startX + i * 24;
            if (i == activeIdx) {
                canvas.fillCircle(cx, y, 6, UiColors::COLOR_PRIMARY);
            } else {
                canvas.drawCircle(cx, y, 4, UiColors::COLOR_MUTED);
            }
        }
    }

private:
    uint32_t _lastActiveMs = 0;
    bool     _visible      = true;
};
