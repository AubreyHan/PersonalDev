#pragma once
#include <M5GFX.h>
#include "model/SystemState.h"

class BasePage {
public:
    virtual ~BasePage() = default;

    void beginViewport(M5Canvas& canvas, bool fullWidth = false) {
        if (fullWidth) {
            canvas.setClipRect(0, 0, Viewport::SCREEN_W, Viewport::SCREEN_H);
            canvas.fillRect(0, 0, Viewport::SCREEN_W, Viewport::SCREEN_H, UiColors::COLOR_BG);
        } else {
            canvas.setClipRect(Viewport::SAFE_X, Viewport::SAFE_Y, Viewport::SAFE_W, Viewport::SAFE_H);
            canvas.fillRect(Viewport::SAFE_X, Viewport::SAFE_Y, Viewport::SAFE_W, Viewport::SAFE_H, UiColors::COLOR_BG);
        }
    }

    void endViewport(M5Canvas& canvas) {
        canvas.clearClipRect();
    }

    virtual void render(M5Canvas& canvas, const SystemContext& ctx) = 0;
    virtual bool onTouchTap(int16_t x, int16_t y, SystemContext& ctx) { return false; }
    virtual void onTouchHold(int16_t x, int16_t y, uint32_t holdMs, SystemContext& ctx) {}
};
