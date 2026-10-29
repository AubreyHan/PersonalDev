#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include "model/SystemState.h"
#include "ui/components/StatusBar.h"
#include "ui/components/PageIndicator.h"
#include "ui/pages/PageInit.h"
#include "ui/pages/PageWifiConfig.h"
#include "ui/pages/Page1Dashboard.h"
#include "ui/pages/Page2Altitude.h"
#include "ui/pages/Page3Speed.h"
#include "ui/pages/Page4Trail.h"
#include "ui/pages/Page5Dynamics.h"
#include "ui/pages/Page6Weather.h"
#include "ui/pages/Page7WorldClock.h"
#include "ui/pages/PageToolsGrid.h"
#include "driver/ButtonDriver.h"

class UiManager {
public:
    static UiManager& instance() {
        static UiManager inst;
        return inst;
    }

    void init();
    void update();

private:
    UiManager() : _canvas(&M5.Display), _lastRenderMs(0) {}

    M5Canvas        _canvas;
    unsigned long   _lastRenderMs;
    StatusBar       _statusBar;
    PageIndicator   _pageIndicator;

    PageInit        _pageInit;
    PageWifiConfig  _pageWifi;
    Page1Dashboard  _page1Dash;
    Page2Altitude   _page2Alt;
    Page3Speed      _page3Speed;
    Page4Trail      _page4Trail;
    Page5Dynamics   _page5Dyn;
    Page6Weather    _page6Weather;
    Page7WorldClock _page7Clock;
    PageToolsGrid   _page8Tools;

    BasePage* getActivePage(uint8_t idx);
    bool handleTouchEvents();
};
