#include "ui/pages/Page7WorldClock.h"
#include "service/TimeService.h"
#include "config.h"

void Page7WorldClock::drawClockCard(M5Canvas& canvas, int x, int y, int w, int h, uint8_t slotIdx, const WorldClockSlotConfig& slot) {
    canvas.fillRoundRect(x, y, w, h, 6, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(x, y, w, h, 6, UiColors::COLOR_BORDER);

    // City Name
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
    canvas.drawString(slot.displayName, x + 8, y + 8);

    // Time & Date
    char timeBuf[16], dateBuf[24];
    bool isDaytime = true;
    TimeService::instance().getSlotTime(slotIdx, timeBuf, sizeof(timeBuf), dateBuf, sizeof(dateBuf), isDaytime);

    canvas.setTextColor(isDaytime ? UiColors::COLOR_PRIMARY : UiColors::COLOR_SECONDARY, UiColors::COLOR_PANEL_BG);
    canvas.setTextSize(2);
    canvas.drawString(timeBuf, x + 8, y + 26);

    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_WHITE, UiColors::COLOR_PANEL_BG);
    canvas.drawString(dateBuf, x + 8, y + 48);

    // Timezone offset badge
    char tzBuf[16];
    snprintf(tzBuf, sizeof(tzBuf), "UTC%+d", slot.utcOffsetHours);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
    canvas.drawString(tzBuf, x + 8, y + 66);

    canvas.setTextColor(UiColors::COLOR_SECONDARY, UiColors::COLOR_PANEL_BG);
    canvas.drawRightString(isDaytime ? "☀️" : "🌙", x + w - 8, y + 8);
}

void Page7WorldClock::draw(M5Canvas& canvas, const SystemContext& ctx) {
    if (_subEdit.isOpen()) {
        _subEdit.draw(canvas, ctx);
        return;
    }

    // Header hint
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
    canvas.drawString("WORLD CLOCK (点击卡片修改时区)", 8, 28);

    // 2x2 Grid (Each card: width 148, height 84)
    // Row 1: Y 44~128
    drawClockCard(canvas, 8, 44, 148, 84, 0, ctx.worldClock.slots[0]);
    drawClockCard(canvas, 164, 44, 148, 84, 1, ctx.worldClock.slots[1]);

    // Row 2: Y 134~218
    drawClockCard(canvas, 8, 134, 148, 84, 2, ctx.worldClock.slots[2]);
    drawClockCard(canvas, 164, 134, 148, 84, 3, ctx.worldClock.slots[3]);
}

void Page7WorldClock::handleTap(int16_t x, int16_t y) {
    if (_subEdit.isOpen()) {
        _subEdit.handleTap(x, y);
        return;
    }

    // Check which of the 4 cards was tapped
    uint8_t slotIdx = 255;
    if (y >= 44 && y <= 128) {
        if (x >= 8 && x <= 156) slotIdx = 0;
        else if (x >= 164 && x <= 312) slotIdx = 1;
    } else if (y >= 134 && y <= 218) {
        if (x >= 8 && x <= 156) slotIdx = 2;
        else if (x >= 164 && x <= 312) slotIdx = 3;
    }

    if (slotIdx != 255) {
        SystemState::instance().lock();
        SystemState::instance().getContextUnsafe().worldClock.activeEditSlot = slotIdx;
        SystemState::instance().unlock();
        _subEdit.open(slotIdx);
    }
}

void Page7WorldClock::handleSwipeUp() {
    if (_subEdit.isOpen()) {
        _subEdit.handleSwipeUp();
    }
}

void Page7WorldClock::handleSwipeDown() {
    // No-op
}
