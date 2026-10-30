#pragma once
#include <M5Unified.h>
#include <M5GFX.h>
#include "model/SystemState.h"
#include "ui/components/LeftDock.h"
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
#include "ui/pages/SubPageEditClock.h"
#include "ui/pages/PageToolsGrid.h"
#include "ui/pages/tools/ToolTfManager.h"
#include "ui/pages/tools/ToolWifi.h"
#include "ui/pages/tools/ToolGnss.h"
#include "ui/pages/tools/ToolSettings.h"

class UiManager {
public:
    void init();
    void update();

private:
    BasePage* getActivePage(AppPageId id);
    void handleTouchAndGestures();
    void applyDockAction(DockTouchAction act, SystemContext& ctx);

    M5Canvas*        _canvas         = nullptr;
    SystemContext    _snapBuf        = {};
    LeftDock         _dock;
    PageIndicator    _indicator;

    PageInit         _pageInit;
    PageWifiConfig   _pageWifiConfig;
    Page1Dashboard   _page1;
    Page2Altitude    _page2;
    Page3Speed       _page3;
    Page4Trail       _page4;
    Page5Dynamics    _page5;
    Page6Weather     _page6;
    Page7WorldClock  _page7;
    SubPageEditClock _subEditClock;
    PageToolsGrid    _page8;
    ToolTfManager    _toolTf;
    ToolWifi         _toolWifi;
    ToolGnss         _toolGnss;
    ToolSettings     _toolSys;

    bool     _touchDown     = false;
    int16_t  _touchStartX   = 0;
    int16_t  _touchStartY   = 0;
    int16_t  _touchLastX    = 0;
    int16_t  _touchLastY    = 0;
    uint32_t _touchDownMs   = 0;
};
