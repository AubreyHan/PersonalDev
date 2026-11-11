#pragma once

#include "BasePage.h"

class PageWifiConfig : public BasePage {
public:
    PageWifiConfig() = default;
    ~PageWifiConfig() override = default;

    PageId pageId() const override { return PAGE_WIFI_CONFIG; }
    const char* pageTitle() const override { return "WIFI PROVISIONING"; }
    bool allowsHorizontalSwipe() const override { return false; }

    void onEnter(const SystemStateSnapshot& state) override;
    void onExit() override;
    void render(M5Canvas& canvas, const SystemStateSnapshot& state) override;
    bool onTouch(const TouchEvent& event, const SystemStateSnapshot& state) override;

private:
    PageId   _returnPage          = PAGE_1_DASHBOARD;
    bool     _backPressed         = false;
    bool     _autoCloseScheduled  = false;
    uint32_t _provisionedDetectMs = 0;
};
