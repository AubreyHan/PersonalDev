#pragma once
#include "ui/pages/BasePage.h"

class SubPageEditClock : public BasePage {
public:
    SubPageEditClock() : _isOpen(false) {}
    void open(uint8_t slotIdx) { _isOpen = true; }
    void close() { _isOpen = false; }
    bool isOpen() const { return _isOpen; }

    void draw(M5Canvas& canvas, const SystemContext& ctx) override;
    void handleTap(int16_t x, int16_t y) override;
    void handleSwipeUp() override;

private:
    bool _isOpen;
};
