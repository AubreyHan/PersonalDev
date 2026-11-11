#pragma once

#include "../BasePage.h"

class ToolOneDrive : public BasePage {
public:
    ToolOneDrive() = default;
    ~ToolOneDrive() override = default;

    PageId pageId() const override { return SUBPAGE_TOOL_ONEDRIVE; }
    const char* pageTitle() const override { return "OneDrive 云端增量同步"; }
    bool allowsHorizontalSwipe() const override { return false; }

    void onEnter(const SystemStateSnapshot& state) override;
    void render(M5Canvas& canvas, const SystemStateSnapshot& state) override;
    bool onTouch(const TouchEvent& event, const SystemStateSnapshot& state) override;

private:
    static constexpr int16_t BTN_BACK_X           = 18;
    static constexpr int16_t BTN_BACK_Y           = 12;
    static constexpr int16_t BTN_BACK_W           = 190;
    static constexpr int16_t BTN_BACK_H           = 56;

    // Unbound state button
    static constexpr int16_t BTN_START_AUTH_X     = 260;
    static constexpr int16_t BTN_START_AUTH_Y     = 520;
    static constexpr int16_t BTN_START_AUTH_W     = 600;
    static constexpr int16_t BTN_START_AUTH_H     = 92;

    // Device code verify state cancel button
    static constexpr int16_t BTN_CANCEL_AUTH_X    = 580;
    static constexpr int16_t BTN_CANCEL_AUTH_Y    = 576;
    static constexpr int16_t BTN_CANCEL_AUTH_W    = 360;
    static constexpr int16_t BTN_CANCEL_AUTH_H    = 72;

    // Bound state action buttons
    static constexpr int16_t BTN_SYNC_NOW_X       = 18;
    static constexpr int16_t BTN_SYNC_NOW_Y       = 596;
    static constexpr int16_t BTN_SYNC_NOW_W       = 380;
    static constexpr int16_t BTN_SYNC_NOW_H       = 94;

    static constexpr int16_t BTN_AUTO_SYNC_X      = 414;
    static constexpr int16_t BTN_AUTO_SYNC_Y      = 596;
    static constexpr int16_t BTN_AUTO_SYNC_W      = 380;
    static constexpr int16_t BTN_AUTO_SYNC_H      = 94;

    static constexpr int16_t BTN_UNBIND_X         = 810;
    static constexpr int16_t BTN_UNBIND_Y         = 596;
    static constexpr int16_t BTN_UNBIND_W         = 292;
    static constexpr int16_t BTN_UNBIND_H         = 94;

    // Unbind confirmation modal buttons
    static constexpr int16_t BTN_UNBIND_CANCEL_X  = 270;
    static constexpr int16_t BTN_UNBIND_CANCEL_Y  = 430;
    static constexpr int16_t BTN_UNBIND_CANCEL_W  = 260;
    static constexpr int16_t BTN_UNBIND_CANCEL_H  = 72;

    static constexpr int16_t BTN_UNBIND_CONFIRM_X = 590;
    static constexpr int16_t BTN_UNBIND_CONFIRM_Y = 430;
    static constexpr int16_t BTN_UNBIND_CONFIRM_W = 260;
    static constexpr int16_t BTN_UNBIND_CONFIRM_H = 72;

    bool _showUnbindConfirm = false;

    void drawTopHeader(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawUnboundView(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawDeviceCodeVerifyView(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawBoundActiveView(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawUnbindConfirmModal(M5Canvas& canvas) const;
};
