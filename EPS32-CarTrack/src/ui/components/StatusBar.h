#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include "model/SystemState.h"

class StatusBar {
public:
    static StatusBar& instance() {
        static StatusBar inst;
        return inst;
    }

    void draw(M5Canvas& canvas, const SystemContext& ctx);
    bool checkTap(int16_t x, int16_t y, uint8_t& targetPage);

private:
    StatusBar() {}
};
