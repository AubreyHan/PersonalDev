#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include "../config.h"
#include "../model/SystemState.h"
#include "pages/BasePage.h"
#include "components/LeftDock.h"
#include "components/PageIndicator.h"

// Owned pages implemented in ui-pages-1
#include "pages/PageInit.h"
#include "pages/PageWifiConfig.h"
#include "pages/Page1Dashboard.h"
#include "pages/Page2Altitude.h"
#include "pages/Page3Speed.h"
#include "pages/Page4Trail.h"

class UiManager {
public:
    static UiManager& instance();

    UiManager() = default;
    ~UiManager();

    // Initialize 1280x720 rotation, backlight, PSRAM double-buffered canvases, and all pages
    bool begin();
    bool init() { return begin(); }

    // Per-frame 60fps UI tick: snapshot fetch, touch normalization, routing, rendering & sprite push
    void update(uint32_t nowMs = millis());
    void tick(uint32_t nowMs = millis()) { update(nowMs); }

    // Control whether UiManager::update() invokes M5.update() internally (default: true)
    void setAutoM5Update(bool enabled) { _autoM5Update = enabled; }

    // Register or override any route slot (supports external injection & __has_include auto-mount)
    void registerPage(PageId id, BasePage* page, bool takeOwnership = false);

    // Explicit route transition
    void navigateTo(PageId targetPage, bool playTone = false);

    PageId activePageId() const { return _activePageId; }
    BasePage* activePage() const;
    const SystemStateSnapshot& lastSnapshot() const { return _snap; }

private:
    bool                _initialized           = false;
    bool                _autoM5Update          = true;

    // PSRAM 16-bit double-buffered off-screen canvases
    M5Canvas            _dockCanvas;
    M5Canvas            _mainCanvas;
    bool                _dockCanvasReady       = false;
    bool                _mainCanvasReady       = false;

    // Persistent snapshot buffer (~4.6 KB, avoids stack allocation in FreeRTOS tasks)
    SystemStateSnapshot _snap                  = {};

    // Permanent chrome components
    LeftDock            _leftDock;
    PageIndicator       _pageIndicator;

    // Built-in ui-pages-1 page instances
    PageInit            _pageInit;
    PageWifiConfig      _pageWifiConfig;
    Page1Dashboard      _page1Dashboard;
    Page2Altitude       _page2Altitude;
    Page3Speed          _page3Speed;
    Page4Trail          _page4Trail;

    // Unified 16-slot routing table
    BasePage*           _pages[PAGE_COUNT]     = {};
    bool                _owned[PAGE_COUNT]     = {};
    PageId              _activePageId          = PAGE_INIT;
    PageId              _queuedPageId          = PAGE_NONE;

    // Backlight & standby controller state
    uint8_t             _appliedBrightness     = 0;
    uint8_t             _lastSpeakerVol        = 0xFF;
    bool                _displaySleeping       = false;

    // Normalized touch state machine
    bool                _wasPressed            = false;
    bool                _touchStartedInDock    = false;
    bool                _widgetConsumedGesture = false;
    int16_t             _startScreenX          = 0;
    int16_t             _startScreenY          = 0;
    int16_t             _lastScreenX           = 0;
    int16_t             _lastScreenY           = 0;
    uint32_t            _pressStartMs          = 0;

    void registerOptionalPeerPages();
    void applyPendingNavigation();
    void updateBacklightAndAudio(uint32_t nowMs);
    void processTouchAndGestures(uint32_t nowMs);
    void handleDockTap(DockTouchAction action, uint32_t nowMs);
    void renderMissingPagePlaceholder(M5Canvas& canvas, PageId id);
    void renderPreSleepOverlay(M5Canvas& canvas, uint32_t nowMs);
    void emitFeedbackTone(uint16_t freqHz, uint32_t durationMs);
};
