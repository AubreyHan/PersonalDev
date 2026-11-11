#pragma once

#include "../BasePage.h"

class ToolWifi : public BasePage {
public:
    ToolWifi() = default;
    ~ToolWifi() override = default;

    PageId pageId() const override { return SUBPAGE_TOOL_WIFI; }
    const char* pageTitle() const override { return "Wi-Fi 6 网络与漫游管理"; }
    bool allowsHorizontalSwipe() const override { return false; }

    void onEnter(const SystemStateSnapshot& state) override;
    void render(M5Canvas& canvas, const SystemStateSnapshot& state) override;
    bool onTouch(const TouchEvent& event, const SystemStateSnapshot& state) override;

private:
    static constexpr int16_t BTN_BACK_X      = 18;
    static constexpr int16_t BTN_BACK_Y      = 12;
    static constexpr int16_t BTN_BACK_W      = 190;
    static constexpr int16_t BTN_BACK_H      = 56;

    static constexpr int16_t BTN_RECONN_X    = 654;
    static constexpr int16_t BTN_RECONN_Y    = 12;
    static constexpr int16_t BTN_RECONN_W    = 200;
    static constexpr int16_t BTN_RECONN_H    = 56;

    static constexpr int16_t BTN_ADD_QR_X    = 868;
    static constexpr int16_t BTN_ADD_QR_Y    = 12;
    static constexpr int16_t BTN_ADD_QR_W    = 234;
    static constexpr int16_t BTN_ADD_QR_H    = 56;

    static constexpr int16_t SLOT_LIST_X     = 18;
    static constexpr int16_t SLOT_LIST_Y0    = 264;
    static constexpr int16_t SLOT_ROW_W      = 1084;
    static constexpr int16_t SLOT_ROW_H      = 78;
    static constexpr int16_t SLOT_ROW_GAP    = 8;

    static constexpr int16_t DEL_BTN_W       = 164;
    static constexpr int16_t DEL_BTN_H       = 52;

    int8_t   _armedDeleteSlot = -1;
    uint32_t _armedDeleteMs   = 0;

    void drawTopBar(M5Canvas& canvas) const;
    void drawCurrentConnectionCard(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawSavedSlotsCard(M5Canvas& canvas, const SystemStateSnapshot& state);
    static void drawSignalBars(M5Canvas& canvas, int16_t x, int16_t y, int8_t rssiDbm, bool connected);
};
