#pragma once

#include "BasePage.h"

class PageToolsGrid : public BasePage {
public:
    PageToolsGrid() = default;
    ~PageToolsGrid() override = default;

    PageId pageId() const override { return PAGE_TOOLS_GRID; }
    const char* pageTitle() const override { return "系统工具箱"; }

    void onEnter(const SystemStateSnapshot& state) override;
    void render(M5Canvas& canvas, const SystemStateSnapshot& state) override;
    bool onTouch(const TouchEvent& event, const SystemStateSnapshot& state) override;

private:
    static constexpr uint8_t TOTAL_TOOLS    = 5;
    static constexpr uint8_t CARDS_PER_PAGE = 4; // Strict PRD 2x2 grid rule (<=4 per page)
    static constexpr uint8_t TOTAL_PAGES    = (TOTAL_TOOLS + CARDS_PER_PAGE - 1) / CARDS_PER_PAGE; // 2 pages

    static constexpr int16_t BTN_PREV_X     = 642;
    static constexpr int16_t BTN_PREV_Y     = 14;
    static constexpr int16_t BTN_PREV_W     = 216;
    static constexpr int16_t BTN_PREV_H     = 58;

    static constexpr int16_t BTN_NEXT_X     = 874;
    static constexpr int16_t BTN_NEXT_Y     = 14;
    static constexpr int16_t BTN_NEXT_W     = 228;
    static constexpr int16_t BTN_NEXT_H     = 58;

    static constexpr int16_t CARD_W         = 532;
    static constexpr int16_t CARD_H         = 292;
    static constexpr int16_t COL_0_X        = 18;
    static constexpr int16_t COL_1_X        = 570;
    static constexpr int16_t ROW_0_Y        = 86;
    static constexpr int16_t ROW_1_Y        = 392;

    uint8_t _gridPage    = 0; // 0 = Tools 1..4, 1 = Tool 5 + expansion slots
    int8_t  _pressedSlot = -1;
    bool    _prevPressed = false;
    bool    _nextPressed = false;

    static void getSlotRect(uint8_t slotOnPage, int16_t& x, int16_t& y);
    void drawHeaderAndPager(M5Canvas& canvas) const;
    void drawToolCard(M5Canvas& canvas, uint8_t toolIndex, uint8_t slotOnPage, const SystemStateSnapshot& state) const;
    void drawExpansionSlot(M5Canvas& canvas, uint8_t slotOnPage, uint8_t virtualIndex) const;
    static void drawToolVectorIcon(M5Canvas& canvas, uint8_t toolIndex, int16_t cx, int16_t cy, uint32_t accent);
};
