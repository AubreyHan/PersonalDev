#pragma once
#include <M5GFX.h>
#include "model/SystemState.h"

class LeftDock {
public:
    void render(M5Canvas& canvas, const SystemContext& ctx);
    DockTouchAction handleTouchTap(int16_t x, int16_t y);
};
