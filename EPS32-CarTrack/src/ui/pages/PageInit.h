#pragma once

#include "BasePage.h"

class PageInit : public BasePage {
public:
    PageInit() = default;
    ~PageInit() override = default;

    PageId pageId() const override { return PAGE_INIT; }
    const char* pageTitle() const override { return "BOOT SELF-TEST"; }
    bool allowsHorizontalSwipe() const override { return false; }

    void onEnter(const SystemStateSnapshot& state) override;
    void render(M5Canvas& canvas, const SystemStateSnapshot& state) override;
    bool onTouch(const TouchEvent& event, const SystemStateSnapshot& state) override;

private:
    bool     _formatModalDismissed = false;
    bool     _autoEnterTriggered   = false;
    uint32_t _enterMs              = 0;
    uint8_t  _pressedBtn           = 0; // 0=none, 1=wifi, 2=skip, 3=fmtConfirm, 4=fmtDismiss

    bool shouldShowFormatModal(const SystemStateSnapshot& state) const;

    void drawTopHeader(M5Canvas& canvas, const SystemStateSnapshot& state);
    void drawHardwareChecklist(M5Canvas& canvas, const SystemStateSnapshot& state);
    void drawRadarSkyplot(M5Canvas& canvas, const SystemStateSnapshot& state, uint32_t nowMs);
    void drawRightControlPanel(M5Canvas& canvas, const SystemStateSnapshot& state);
    void drawFormatFat32Modal(M5Canvas& canvas, const SystemStateSnapshot& state, uint32_t nowMs);
};
