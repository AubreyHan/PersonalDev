#pragma once
#include "ui/pages/BasePage.h"

class ToolGnss : public BasePage {
public:
    ToolGnss() : _isOpen(false) {}
    void open() { _isOpen = true; }
    void close() { _isOpen = false; }
    bool isOpen() const { return _isOpen; }

    void draw(M5Canvas& canvas, const SystemContext& ctx) override;
    void handleSwipeUp() override { close(); }

private:
    bool _isOpen;
};
