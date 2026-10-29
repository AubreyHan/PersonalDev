#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include "model/SystemState.h"
#include "config.h"

class BasePage {
public:
    virtual ~BasePage() {}
    virtual void init() {}
    virtual void draw(M5Canvas& canvas, const SystemContext& ctx) = 0;
    virtual void handleTap(int16_t x, int16_t y) {}
    virtual void handleSwipeUp() {}
    virtual void handleSwipeDown() {}
    virtual void handleLongPress(int16_t x, int16_t y) {}
};
