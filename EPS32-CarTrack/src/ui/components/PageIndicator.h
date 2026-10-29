#pragma once
#include <Arduino.h>
#include <M5Unified.h>

class PageIndicator {
public:
    static PageIndicator& instance() {
        static PageIndicator inst;
        return inst;
    }

    void wake() { _lastActiveTime = millis(); }
    void draw(M5Canvas& canvas, uint8_t currentPage, uint8_t totalPages);

    PageIndicator() : _lastActiveTime(0) {}
private:
    unsigned long _lastActiveTime;
};
