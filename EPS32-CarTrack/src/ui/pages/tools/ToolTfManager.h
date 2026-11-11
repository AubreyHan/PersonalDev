#pragma once

#include "../BasePage.h"

class ToolTfManager : public BasePage {
public:
    ToolTfManager() = default;
    ~ToolTfManager() override = default;

    PageId pageId() const override { return SUBPAGE_TOOL_TF_MANAGER; }
    const char* pageTitle() const override { return "TF 卡与轨迹无线导出"; }
    bool allowsHorizontalSwipe() const override { return false; }

    void onEnter(const SystemStateSnapshot& state) override;
    void onExit() override;
    void render(M5Canvas& canvas, const SystemStateSnapshot& state) override;
    bool onTouch(const TouchEvent& event, const SystemStateSnapshot& state) override;

private:
    static constexpr int16_t BTN_BACK_X        = 18;
    static constexpr int16_t BTN_BACK_Y        = 12;
    static constexpr int16_t BTN_BACK_W        = 190;
    static constexpr int16_t BTN_BACK_H        = 56;

    static constexpr int16_t BTN_REFRESH_X     = 918;
    static constexpr int16_t BTN_REFRESH_Y     = 12;
    static constexpr int16_t BTN_REFRESH_W     = 184;
    static constexpr int16_t BTN_REFRESH_H     = 56;

    static constexpr int16_t BTN_AUTOREC_X     = 698;
    static constexpr int16_t BTN_AUTOREC_Y     = 12;
    static constexpr int16_t BTN_AUTOREC_W     = 206;
    static constexpr int16_t BTN_AUTOREC_H     = 56;

    static constexpr int16_t BTN_SHARE_X       = 18;
    static constexpr int16_t BTN_SHARE_Y       = 598;
    static constexpr int16_t BTN_SHARE_W       = 660;
    static constexpr int16_t BTN_SHARE_H       = 96;

    static constexpr int16_t BTN_FORMAT_X      = 696;
    static constexpr int16_t BTN_FORMAT_Y      = 598;
    static constexpr int16_t BTN_FORMAT_W      = 406;
    static constexpr int16_t BTN_FORMAT_H      = 96;

    // Dual QR Share Modal Buttons
    static constexpr int16_t BTN_QR_STOP_X     = 360;
    static constexpr int16_t BTN_QR_STOP_Y     = 596;
    static constexpr int16_t BTN_QR_STOP_W     = 400;
    static constexpr int16_t BTN_QR_STOP_H     = 68;

    // Format Confirmation Modal Buttons
    static constexpr int16_t BTN_FMT_CANCEL_X  = 250;
    static constexpr int16_t BTN_FMT_CANCEL_Y  = 456;
    static constexpr int16_t BTN_FMT_CANCEL_W  = 280;
    static constexpr int16_t BTN_FMT_CANCEL_H  = 74;

    static constexpr int16_t BTN_FMT_CONFIRM_X = 590;
    static constexpr int16_t BTN_FMT_CONFIRM_Y = 456;
    static constexpr int16_t BTN_FMT_CONFIRM_W = 280;
    static constexpr int16_t BTN_FMT_CONFIRM_H = 74;

    bool _showShareModal     = false;
    bool _showFormatConfirm  = false;
    bool _startedShareHere   = false;

    void drawTopHeader(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawCapacityAndQuotaCard(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawTriFolderCards(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawBottomActionButtons(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawShareQrModal(M5Canvas& canvas, const SystemStateSnapshot& state) const;
    void drawFormatConfirmModal(M5Canvas& canvas, const SystemStateSnapshot& state) const;
};
