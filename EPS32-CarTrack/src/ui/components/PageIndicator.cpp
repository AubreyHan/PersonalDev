#include "ui/components/PageIndicator.h"
#include "config.h"

void PageIndicator::draw(M5Canvas& canvas, uint8_t currentPage, uint8_t totalPages) {
    unsigned long now = millis();
    // Auto-hide after 3 seconds of no activity
    if (now - _lastActiveTime > 3000) return;

    if (totalPages == 0) return;

    int spacing = 10;
    int totalW = (totalPages - 1) * spacing;
    int startX = (320 - totalW) / 2;
    int y = 232;

    for (uint8_t i = 0; i < totalPages; i++) {
        int x = startX + i * spacing;
        if (i == currentPage) {
            canvas.fillCircle(x, y, 3, UiColors::COLOR_PRIMARY);
        } else {
            canvas.drawCircle(x, y, 2, UiColors::COLOR_MUTED);
        }
    }
}
