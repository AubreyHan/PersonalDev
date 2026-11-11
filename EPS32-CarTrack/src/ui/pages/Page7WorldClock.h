#pragma once

#include "BasePage.h"

class Page7WorldClock : public BasePage {
public:
    Page7WorldClock() = default;
    ~Page7WorldClock() override = default;

    PageId pageId() const override { return PAGE_7_WORLD_CLOCK; }
    const char* pageTitle() const override { return "多时区世界时钟"; }

    void onEnter(const SystemStateSnapshot& state) override;
    void render(M5Canvas& canvas, const SystemStateSnapshot& state) override;
    bool onTouch(const TouchEvent& event, const SystemStateSnapshot& state) override;

private:
    static constexpr int16_t CARD_W      = 530;
    static constexpr int16_t CARD_H      = 320;
    static constexpr int16_t COL_0_X     = 18;
    static constexpr int16_t COL_1_X     = 572;
    static constexpr int16_t ROW_0_Y     = 38;
    static constexpr int16_t ROW_1_Y     = 368;

    int8_t _pressedSlot = -1;

    static void getCardRect(uint8_t slot, int16_t& x, int16_t& y);
    void drawTopSyncBar(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawClockCard(M5Canvas& canvas, uint8_t slot, const SystemStateSnapshot& state) const;
    static void drawDayNightIcon(M5Canvas& canvas, int16_t cx, int16_t cy, bool isDaytime);
};
