#pragma once
#include "ui/pages/BasePage.h"

class ToolSettings : public BasePage {
public:
    ToolSettings() : _isOpen(false) {}
    void open() { _isOpen = true; }
    void close() { _isOpen = false; }
    bool isOpen() const { return _isOpen; }

    void draw(M5Canvas& canvas, const SystemContext& ctx) override;
    void handleTap(int16_t x, int16_t y) override;
    void handleSwipeUp() override { close(); }

private:
    bool _isOpen;
};
