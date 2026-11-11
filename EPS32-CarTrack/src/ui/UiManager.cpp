#include "UiManager.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// =============================================================================
// Zero-Coupling Auto-Discovery for ui-pages-2 (10 Page Classes)
// Compiles 100% cleanly on ui-pages-1 standalone branch, and automatically
// mounts all 10 peer pages once ui-pages-2 is merged into the workspace!
// =============================================================================
#if __has_include("ui/pages/Page5Dynamics.h")
    #include "ui/pages/Page5Dynamics.h"
    #define HAS_PAGE5_DYNAMICS 1
#elif __has_include("pages/Page5Dynamics.h")
    #include "pages/Page5Dynamics.h"
    #define HAS_PAGE5_DYNAMICS 1
#else
    #define HAS_PAGE5_DYNAMICS 0
#endif

#if __has_include("ui/pages/Page6Weather.h")
    #include "ui/pages/Page6Weather.h"
    #define HAS_PAGE6_WEATHER 1
#elif __has_include("pages/Page6Weather.h")
    #include "pages/Page6Weather.h"
    #define HAS_PAGE6_WEATHER 1
#else
    #define HAS_PAGE6_WEATHER 0
#endif

#if __has_include("ui/pages/Page7WorldClock.h")
    #include "ui/pages/Page7WorldClock.h"
    #define HAS_PAGE7_WORLD_CLOCK 1
#elif __has_include("pages/Page7WorldClock.h")
    #include "pages/Page7WorldClock.h"
    #define HAS_PAGE7_WORLD_CLOCK 1
#else
    #define HAS_PAGE7_WORLD_CLOCK 0
#endif

#if __has_include("ui/pages/SubPageEditClock.h")
    #include "ui/pages/SubPageEditClock.h"
    #define HAS_SUBPAGE_EDIT_CLOCK 1
#elif __has_include("pages/SubPageEditClock.h")
    #include "pages/SubPageEditClock.h"
    #define HAS_SUBPAGE_EDIT_CLOCK 1
#else
    #define HAS_SUBPAGE_EDIT_CLOCK 0
#endif

#if __has_include("ui/pages/PageToolsGrid.h")
    #include "ui/pages/PageToolsGrid.h"
    #define HAS_PAGE_TOOLS_GRID 1
#elif __has_include("pages/PageToolsGrid.h")
    #include "pages/PageToolsGrid.h"
    #define HAS_PAGE_TOOLS_GRID 1
#else
    #define HAS_PAGE_TOOLS_GRID 0
#endif

#if __has_include("ui/pages/tools/ToolTfManager.h")
    #include "ui/pages/tools/ToolTfManager.h"
    #define HAS_TOOL_TF_MANAGER 1
#elif __has_include("pages/tools/ToolTfManager.h")
    #include "pages/tools/ToolTfManager.h"
    #define HAS_TOOL_TF_MANAGER 1
#else
    #define HAS_TOOL_TF_MANAGER 0
#endif

#if __has_include("ui/pages/tools/ToolWifi.h")
    #include "ui/pages/tools/ToolWifi.h"
    #define HAS_TOOL_WIFI 1
#elif __has_include("pages/tools/ToolWifi.h")
    #include "pages/tools/ToolWifi.h"
    #define HAS_TOOL_WIFI 1
#else
    #define HAS_TOOL_WIFI 0
#endif

#if __has_include("ui/pages/tools/ToolGnss.h")
    #include "ui/pages/tools/ToolGnss.h"
    #define HAS_TOOL_GNSS 1
#elif __has_include("pages/tools/ToolGnss.h")
    #include "pages/tools/ToolGnss.h"
    #define HAS_TOOL_GNSS 1
#else
    #define HAS_TOOL_GNSS 0
#endif

#if __has_include("ui/pages/tools/ToolOneDrive.h")
    #include "ui/pages/tools/ToolOneDrive.h"
    #define HAS_TOOL_ONEDRIVE 1
#elif __has_include("pages/tools/ToolOneDrive.h")
    #include "pages/tools/ToolOneDrive.h"
    #define HAS_TOOL_ONEDRIVE 1
#else
    #define HAS_TOOL_ONEDRIVE 0
#endif

#if __has_include("ui/pages/tools/ToolSettings.h")
    #include "ui/pages/tools/ToolSettings.h"
    #define HAS_TOOL_SETTINGS 1
#elif __has_include("pages/tools/ToolSettings.h")
    #include "pages/tools/ToolSettings.h"
    #define HAS_TOOL_SETTINGS 1
#else
    #define HAS_TOOL_SETTINGS 0
#endif

UiManager& UiManager::instance() {
    static UiManager s_instance;
    return s_instance;
}

UiManager::~UiManager() {
    for (size_t i = 0; i < PAGE_COUNT; ++i) {
        if (_owned[i] && _pages[i] != nullptr) {
            delete _pages[i];
            _pages[i] = nullptr;
            _owned[i] = false;
        }
    }
    if (_dockCanvasReady) {
        _dockCanvas.deleteSprite();
        _dockCanvasReady = false;
    }
    if (_mainCanvasReady) {
        _mainCanvas.deleteSprite();
        _mainCanvasReady = false;
    }
}

bool UiManager::begin() {
    if (_initialized) {
        return (_dockCanvasReady && _mainCanvasReady);
    }

    SystemState::instance().init();
    SystemState::instance().getSnapshot(_snap);

    // 1. Configure Tab5 5-inch 1280x720 Landscape Display & Pure Black Background
    M5.Display.setRotation(Viewport::LANDSCAPE_ROTATION);
    M5.Display.setColorDepth(16);
    M5.Display.fillScreen(TFT_BLACK);

    const uint8_t initBright = (_snap.settings.screenBrightness >= 10U)
                                   ? _snap.settings.screenBrightness
                                   : 180U;
    M5.Display.setBrightness(initBright);
    _appliedBrightness = initBright;

    if (M5.Speaker.isEnabled()) {
        M5.Speaker.setVolume(_snap.settings.speakerVolume);
        _lastSpeakerVol = _snap.settings.speakerVolume;
    }

    // 2. Allocate PSRAM Double-Buffered Sprites (setPsram(true) MUST precede createSprite!)
    _dockCanvas.setPsram(true);
    _dockCanvas.setColorDepth(16);
    _dockCanvasReady = (_dockCanvas.createSprite(Viewport::DOCK_W, Viewport::DOCK_H) != nullptr);
    if (_dockCanvasReady) {
        _dockCanvas.fillScreen(TFT_BLACK);
    }

    _mainCanvas.setPsram(true);
    _mainCanvas.setColorDepth(16);
    _mainCanvasReady = (_mainCanvas.createSprite(Viewport::SAFE_W, Viewport::SAFE_H) != nullptr);
    if (_mainCanvasReady) {
        _mainCanvas.fillScreen(TFT_BLACK);
    }

    // 3. Register ui-pages-1 Built-in Pages
    registerPage(PAGE_INIT,        &_pageInit,        false);
    registerPage(PAGE_WIFI_CONFIG, &_pageWifiConfig,  false);
    registerPage(PAGE_1_DASHBOARD, &_page1Dashboard,  false);
    registerPage(PAGE_2_ALTITUDE,  &_page2Altitude,   false);
    registerPage(PAGE_3_SPEED,     &_page3Speed,      false);
    registerPage(PAGE_4_TRAIL,     &_page4Trail,      false);

    // 4. Auto-Mount All Available ui-pages-2 Pages
    registerOptionalPeerPages();

    // 5. Enter Initial Boot Page (PAGE_INIT)
    _activePageId = _snap.boot.autoEnteredDashboard ? PAGE_1_DASHBOARD : PAGE_INIT;
    _queuedPageId = PAGE_NONE;
    if (BasePage* p = activePage()) {
        p->onEnter(_snap);
    }

    _initialized = true;
    return (_dockCanvasReady && _mainCanvasReady);
}

void UiManager::registerOptionalPeerPages() {
#if HAS_PAGE5_DYNAMICS
    if (_pages[PAGE_5_DYNAMICS] == nullptr) {
        registerPage(PAGE_5_DYNAMICS, new Page5Dynamics(), true);
    }
#endif
#if HAS_PAGE6_WEATHER
    if (_pages[PAGE_6_WEATHER] == nullptr) {
        registerPage(PAGE_6_WEATHER, new Page6Weather(), true);
    }
#endif
#if HAS_PAGE7_WORLD_CLOCK
    if (_pages[PAGE_7_WORLD_CLOCK] == nullptr) {
        registerPage(PAGE_7_WORLD_CLOCK, new Page7WorldClock(), true);
    }
#endif
#if HAS_SUBPAGE_EDIT_CLOCK
    if (_pages[SUBPAGE_EDIT_CLOCK] == nullptr) {
        registerPage(SUBPAGE_EDIT_CLOCK, new SubPageEditClock(), true);
    }
#endif
#if HAS_PAGE_TOOLS_GRID
    if (_pages[PAGE_TOOLS_GRID] == nullptr) {
        registerPage(PAGE_TOOLS_GRID, new PageToolsGrid(), true);
    }
#endif
#if HAS_TOOL_TF_MANAGER
    if (_pages[SUBPAGE_TOOL_TF_MANAGER] == nullptr) {
        registerPage(SUBPAGE_TOOL_TF_MANAGER, new ToolTfManager(), true);
    }
#endif
#if HAS_TOOL_WIFI
    if (_pages[SUBPAGE_TOOL_WIFI] == nullptr) {
        registerPage(SUBPAGE_TOOL_WIFI, new ToolWifi(), true);
    }
#endif
#if HAS_TOOL_GNSS
    if (_pages[SUBPAGE_TOOL_GNSS] == nullptr) {
        registerPage(SUBPAGE_TOOL_GNSS, new ToolGnss(), true);
    }
#endif
#if HAS_TOOL_ONEDRIVE
    if (_pages[SUBPAGE_TOOL_ONEDRIVE] == nullptr) {
        registerPage(SUBPAGE_TOOL_ONEDRIVE, new ToolOneDrive(), true);
    }
#endif
#if HAS_TOOL_SETTINGS
    if (_pages[SUBPAGE_TOOL_SETTINGS] == nullptr) {
        registerPage(SUBPAGE_TOOL_SETTINGS, new ToolSettings(), true);
    }
#endif
}

void UiManager::registerPage(PageId id, BasePage* page, bool takeOwnership) {
    if (id >= PAGE_COUNT) {
        return;
    }
    if (_owned[id] && _pages[id] != nullptr && _pages[id] != page) {
        delete _pages[id];
    }
    _pages[id] = page;
    _owned[id] = takeOwnership;

    if (page != nullptr) {
        page->setNavigateCallback([this](PageId target) {
            if (target < PAGE_COUNT) {
                _queuedPageId = target;
            }
        });
        page->setToneCallback([this](uint16_t freqHz, uint32_t durationMs) {
            emitFeedbackTone(freqHz, durationMs);
        });
    }
}

BasePage* UiManager::activePage() const {
    if (_activePageId >= PAGE_COUNT) {
        return nullptr;
    }
    return _pages[_activePageId];
}

void UiManager::emitFeedbackTone(uint16_t freqHz, uint32_t durationMs) {
    if (!_snap.settings.speakerBeepEnabled) {
        return;
    }
    if (M5.Speaker.isEnabled()) {
        M5.Speaker.tone(freqHz, durationMs);
    }
}

void UiManager::navigateTo(PageId targetPage, bool playTone) {
    if (targetPage >= PAGE_COUNT || targetPage == _activePageId) {
        return;
    }

    if (playTone) {
        emitFeedbackTone(HapticConfig::TONE_TAP_HZ, HapticConfig::TONE_TAP_MS);
    }

    if (BasePage* oldPage = activePage()) {
        oldPage->onExit();
    }

    _activePageId = targetPage;
    _queuedPageId = PAGE_NONE;

    if (BasePage* newPage = activePage()) {
        newPage->onEnter(_snap);
    }

    if (isCarouselMainPage(_activePageId)) {
        _pageIndicator.notifyInteraction(millis());
    } else {
        _pageIndicator.hideImmediately();
    }
}

void UiManager::applyPendingNavigation() {
    if (BasePage* cur = activePage()) {
        PageId polled = PAGE_NONE;
        if (cur->consumePendingNavigation(polled) && polled < PAGE_COUNT) {
            _queuedPageId = polled;
        }
    }
    if (_queuedPageId != PAGE_NONE && _queuedPageId < PAGE_COUNT) {
        const PageId dst = _queuedPageId;
        _queuedPageId    = PAGE_NONE;
        navigateTo(dst, false);
    }
}

void UiManager::updateBacklightAndAudio(uint32_t nowMs) {
    (void)nowMs;

    // Sync speaker volume when changed in UserSettings
    if (M5.Speaker.isEnabled() && _snap.settings.speakerVolume != _lastSpeakerVol) {
        M5.Speaker.setVolume(_snap.settings.speakerVolume);
        _lastSpeakerVol = _snap.settings.speakerVolume;
    }

    // Standby phase sleep / wake backlight guard
    if (_snap.power.standbyPhase == STANDBY_PHASE_SLEEPING || _snap.power.lowBattCutoffActive) {
        if (!_displaySleeping || _appliedBrightness != 0U) {
            M5.Display.setBrightness(0);
            _appliedBrightness = 0;
            _displaySleeping   = true;
        }
        return;
    }

    _displaySleeping = false;

    // Compute target brightness (manual vs auto day/night/tunnel)
    uint8_t base = _snap.settings.screenBrightness;
    if (base < 15U) base = 15U;

    uint8_t target = base;
    if (_snap.settings.autoDayNightBacklight) {
        const uint16_t minsOfDay = static_cast<uint16_t>(_snap.time.localHour) * 60U + _snap.time.localMinute;
        const bool isDaylight    = (minsOfDay >= 390U && minsOfDay < 1110U); // 06:30 .. 18:29

        if (_snap.gnss.isInTunnel) {
            const uint16_t tunVal = (static_cast<uint16_t>(base) * 55U) / 100U;
            target = static_cast<uint8_t>(tunVal < 36U ? 36U : (tunVal > 135U ? 135U : tunVal));
        } else if (!isDaylight) {
            const uint16_t nightVal = (static_cast<uint16_t>(base) * 45U) / 100U;
            target = static_cast<uint8_t>(nightVal < 24U ? 24U : (nightVal > 115U ? 115U : nightVal));
        } else {
            target = (base < 205U) ? 205U : base;
        }
    }

    // Smooth slew rate limiting (max 8 steps per frame) to avoid harsh brightness jumps
    if (_appliedBrightness != target) {
        int16_t next = static_cast<int16_t>(_appliedBrightness);
        const int16_t diff = static_cast<int16_t>(target) - next;
        if (abs(diff) <= 8) {
            next = target;
        } else {
            next += (diff > 0) ? 8 : -8;
        }
        _appliedBrightness = static_cast<uint8_t>(next);
        M5.Display.setBrightness(_appliedBrightness);
    }
}

void UiManager::handleDockTap(DockTouchAction action, uint32_t nowMs) {
    PageId target = PAGE_NONE;
    switch (action) {
        case DOCK_ACTION_HOME:
            target = PAGE_1_DASHBOARD;
            break;
        case DOCK_ACTION_TIME:
            target = PAGE_7_WORLD_CLOCK;
            break;
        case DOCK_ACTION_GNSS:
            target = SUBPAGE_TOOL_GNSS;
            break;
        case DOCK_ACTION_WIFI:
            if (_activePageId == PAGE_INIT || _pages[SUBPAGE_TOOL_WIFI] == nullptr) {
                target = PAGE_WIFI_CONFIG;
            } else {
                target = SUBPAGE_TOOL_WIFI;
            }
            break;
        case DOCK_ACTION_BATTERY:
            target = SUBPAGE_TOOL_SETTINGS;
            break;
        case DOCK_ACTION_WEATHER:
            target = PAGE_6_WEATHER;
            break;
        case DOCK_ACTION_TOOLS:
            target = PAGE_TOOLS_GRID;
            break;
        default:
            return;
    }

    if (target == PAGE_NONE) {
        return;
    }

    // Leaving PAGE_INIT via Dock marks boot dashboard transition complete so PageInit doesn't pull back
    if (_activePageId == PAGE_INIT && target != PAGE_INIT && target != PAGE_WIFI_CONFIG) {
        StateGuard guard(SystemState::instance());
        if (guard.locked()) {
            SystemState::instance().data().boot.autoEnteredDashboard = true;
        }
    }

    if (target == _activePageId) {
        if (isCarouselMainPage(target)) {
            _pageIndicator.notifyInteraction(nowMs);
        }
        emitFeedbackTone(HapticConfig::TONE_TAP_HZ, HapticConfig::TONE_TAP_MS);
        return;
    }

    navigateTo(target, true);
}

void UiManager::processTouchAndGestures(uint32_t nowMs) {
    const auto& det    = M5.Touch.getDetail(0);
    const bool pressed = det.isPressed();

    if (!pressed && !_wasPressed) {
        _leftDock.setPressedZone(DOCK_ACTION_NONE);
        return;
    }

    const int16_t curScreenX = det.x;
    const int16_t curScreenY = det.y;

    // 1. Finger Just Pressed Down -> TOUCH_EVT_DOWN
    if (pressed && !_wasPressed) {
        _wasPressed            = true;
        _startScreenX          = curScreenX;
        _startScreenY          = curScreenY;
        _lastScreenX           = curScreenX;
        _lastScreenY           = curScreenY;
        _pressStartMs          = nowMs;
        _widgetConsumedGesture = false;
        _touchStartedInDock    = (curScreenX < Viewport::SAFE_X);

        if (_touchStartedInDock) {
            _leftDock.setPressedZone(hitTestLeftDock(curScreenX, curScreenY));
            return;
        }

        _leftDock.setPressedZone(DOCK_ACTION_NONE);
        if (isCarouselMainPage(_activePageId)) {
            _pageIndicator.notifyInteraction(nowMs);
        }

        if (BasePage* page = activePage()) {
            TouchEvent ev;
            ev.type           = TOUCH_EVT_DOWN;
            ev.screenX        = curScreenX;
            ev.screenY        = curScreenY;
            ev.localX         = static_cast<int16_t>(curScreenX - Viewport::SAFE_X);
            ev.localY         = curScreenY;
            ev.startLocalX    = ev.localX;
            ev.startLocalY    = ev.localY;
            ev.deltaX         = 0;
            ev.deltaY         = 0;
            ev.holdDurationMs = 0;
            ev.isPressed      = true;
            if (page->onTouch(ev, _snap)) {
                _widgetConsumedGesture = true;
            }
        }
        applyPendingNavigation();
        return;
    }

    // 2. Finger Held / Moving -> TOUCH_EVT_MOVE & TOUCH_EVT_HOLD
    if (pressed && _wasPressed) {
        const uint32_t holdMs = nowMs - _pressStartMs;
        const int16_t  stepDx = static_cast<int16_t>(curScreenX - _lastScreenX);
        const int16_t  stepDy = static_cast<int16_t>(curScreenY - _lastScreenY);
        _lastScreenX          = curScreenX;
        _lastScreenY          = curScreenY;

        if (_touchStartedInDock) {
            const DockTouchAction curZone   = hitTestLeftDock(curScreenX, curScreenY);
            const DockTouchAction startZone = hitTestLeftDock(_startScreenX, _startScreenY);
            _leftDock.setPressedZone((curZone == startZone) ? curZone : DOCK_ACTION_NONE);
            return;
        }

        if (BasePage* page = activePage()) {
            TouchEvent ev;
            ev.screenX        = curScreenX;
            ev.screenY        = curScreenY;
            ev.localX         = static_cast<int16_t>(curScreenX - Viewport::SAFE_X);
            ev.localY         = curScreenY;
            ev.startLocalX    = static_cast<int16_t>(_startScreenX - Viewport::SAFE_X);
            ev.startLocalY    = _startScreenY;
            ev.deltaX         = stepDx;
            ev.deltaY         = stepDy;
            ev.holdDurationMs = holdMs;
            ev.isPressed      = true;

            const int16_t jitter = static_cast<int16_t>(
                abs(curScreenX - _startScreenX) + abs(curScreenY - _startScreenY)
            );
            if (stepDx != 0 || stepDy != 0) {
                ev.type = TOUCH_EVT_MOVE;
                if (page->onTouch(ev, _snap)) {
                    _widgetConsumedGesture = true;
                }
            }
            if (jitter <= (GestureConfig::TAP_MAX_JITTER_PX * 2)) {
                ev.type = TOUCH_EVT_HOLD;
                if (page->onTouch(ev, _snap)) {
                    _widgetConsumedGesture = true;
                }
            }
        }
        applyPendingNavigation();
        return;
    }

    // 3. Finger Released -> Dock Shortcut / Horizontal Carousel Swipe / TOUCH_EVT_TAP / TOUCH_EVT_UP
    if (!pressed && _wasPressed) {
        _wasPressed = false;
        _leftDock.setPressedZone(DOCK_ACTION_NONE);

        const uint32_t durationMs = nowMs - _pressStartMs;
        const int16_t  relScreenX = _lastScreenX;
        const int16_t  relScreenY = _lastScreenY;
        const int16_t  totalDx    = static_cast<int16_t>(relScreenX - _startScreenX);
        const int16_t  totalDy    = static_cast<int16_t>(relScreenY - _startScreenY);

        if (_touchStartedInDock) {
            const DockTouchAction startZone = hitTestLeftDock(_startScreenX, _startScreenY);
            const DockTouchAction endZone   = hitTestLeftDock(relScreenX, relScreenY);
            if (startZone != DOCK_ACTION_NONE &&
                startZone == endZone &&
                abs(totalDx) <= GestureConfig::TAP_MAX_JITTER_PX * 2 &&
                abs(totalDy) <= GestureConfig::TAP_MAX_JITTER_PX * 2) {
                handleDockTap(startZone, nowMs);
            }
            return;
        }

        BasePage* page = activePage();
        const bool swipeAllowed = (page == nullptr) ? isCarouselMainPage(_activePageId)
                                                    : page->allowsHorizontalSwipe();

        // Check Full-Screen Horizontal Swipe across 8 Primary Carousel Pages
        if (!_widgetConsumedGesture &&
            swipeAllowed &&
            isCarouselMainPage(_activePageId) &&
            abs(totalDx) >= GestureConfig::SWIPE_HORIZ_THRESHOLD_PX &&
            static_cast<float>(abs(totalDx)) >= static_cast<float>(abs(totalDy)) * GestureConfig::DIRECTION_RATIO) {

            const uint8_t curIdx  = getCarouselPageIndex(_activePageId);
            const uint8_t nextIdx = (totalDx < 0)
                                        ? static_cast<uint8_t>((curIdx + 1U) % 8U)
                                        : static_cast<uint8_t>((curIdx + 7U) % 8U);
            emitFeedbackTone(HapticConfig::TONE_SWIPE_HZ, HapticConfig::TONE_SWIPE_MS);
            _pageIndicator.notifyInteraction(nowMs);
            navigateTo(carouselPageFromIndex(nextIdx), false);
            return;
        }

        // Clean short tap inside 1120x720 safe viewport
        const bool isCleanTap = (durationMs <= GestureConfig::TAP_MAX_DURATION_MS) &&
                                (abs(totalDx) <= GestureConfig::TAP_MAX_JITTER_PX) &&
                                (abs(totalDy) <= GestureConfig::TAP_MAX_JITTER_PX);

        if (page != nullptr) {
            TouchEvent ev;
            ev.screenX        = relScreenX;
            ev.screenY        = relScreenY;
            ev.localX         = static_cast<int16_t>(relScreenX - Viewport::SAFE_X);
            ev.localY         = relScreenY;
            ev.startLocalX    = static_cast<int16_t>(_startScreenX - Viewport::SAFE_X);
            ev.startLocalY    = _startScreenY;
            ev.deltaX         = totalDx;
            ev.deltaY         = totalDy;
            ev.holdDurationMs = durationMs;
            ev.isPressed      = false;

            if (isCleanTap) {
                ev.type = TOUCH_EVT_TAP;
                page->onTouch(ev, _snap);
            }
            ev.type = TOUCH_EVT_UP;
            page->onTouch(ev, _snap);
        } else if (isCleanTap && _activePageId != PAGE_1_DASHBOARD) {
            // Fallback placeholder [Return to Dashboard] tap support
            if (relScreenX >= (Viewport::SAFE_X + 410) && relScreenX < (Viewport::SAFE_X + 710) &&
                relScreenY >= 420 && relScreenY < 492) {
                navigateTo(PAGE_1_DASHBOARD, true);
            }
        }

        applyPendingNavigation();
    }
}

void UiManager::renderMissingPagePlaceholder(M5Canvas& canvas, PageId id) {
    BasePage::clearViewportCanvas(canvas);
    canvas.fillRoundRect(220, 200, 680, 320, 16, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(220, 200, 680, 320, 16, ThemeColors::BORDER_SUBTLE);

    char msg[64];
    snprintf(msg, sizeof(msg), "页面模块待挂载 (PageId = %u)", static_cast<unsigned>(id));
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString(msg, Viewport::SAFE_W / 2, 290);

    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("合并 ui-pages-2 分支后将通过 __has_include 自动挂载", Viewport::SAFE_W / 2, 348);

    canvas.fillRoundRect(410, 420, 300, 72, 12, ThemeColors::NEON_GREEN);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(TFT_BLACK);
    canvas.drawString("返回主仪表盘", 560, 456);
}

void UiManager::renderPreSleepOverlay(M5Canvas& canvas, uint32_t nowMs) {
    const StandbyLifecyclePhase phase = _snap.power.standbyPhase;
    if (phase != STANDBY_PHASE_SETTLING_TRIPS && phase != STANDBY_PHASE_PRE_SLEEP_SYNC) {
        return;
    }

    canvas.fillRectAlpha(180, 250, 760, 220, 220, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(180, 250, 760, 220, 16, ThemeColors::ICE_CYAN);

    const float spin = fmodf(static_cast<float>(nowMs) * 0.30f, 360.0f);
    canvas.fillArc(252, 360, 36, 26, spin, spin + 230.0f, ThemeColors::ICE_CYAN);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    if (phase == STANDBY_PHASE_SETTLING_TRIPS) {
        canvas.drawString("驻车静置满 20 分钟 — 正在安全封口轨迹文件", 316, 304);
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextColor(ThemeColors::TEXT_WHITE);
        canvas.drawString("正在写入 trips.csv 行程报表并同步 MicroSD 缓存...", 316, 356);
    } else {
        canvas.drawString("休眠前 OneDrive 云端增量同步中...", 316, 296);
        char prog[96];
        snprintf(prog, sizeof(prog), "待传 %u 个文件  |  当前进度 %u%% (%s)",
                 static_cast<unsigned>(_snap.oneDrive.pendingFiles),
                 static_cast<unsigned>(_snap.oneDrive.currentFilePercent),
                 _snap.oneDrive.currentFileName[0] ? _snap.oneDrive.currentFileName : "准备中");
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextColor(ThemeColors::TEXT_WHITE);
        canvas.drawString(prog, 316, 348);
        canvas.setFont(&fonts::efontCN_14);
        canvas.setTextColor(ThemeColors::TEXT_MUTED);
        canvas.drawString("同步完成或满 25 秒超时后将自动关屏进入超低功耗待机", 316, 390);
    }
}

void UiManager::update(uint32_t nowMs) {
    if (!_initialized) {
        begin();
    }

    if (_autoM5Update) {
        M5.update();
    }

    // 1. Atomic per-frame snapshot copy into persistent _snap
    SystemState::instance().getSnapshot(_snap);

    // 2. Backlight & standby lifecycle management
    updateBacklightAndAudio(nowMs);
    if (_displaySleeping) {
        // Still allow touch polling if needed by M5.Touch, then skip heavy sprite push while screen is off
        return;
    }

    // 3. Touch normalization, Dock 7-zone shortcuts, and 8-page horizontal swipe routing
    processTouchAndGestures(nowMs);
    applyPendingNavigation();

    // 4. Render Left Permanent 160x720 Dock & Push to (0, 0)
    if (_dockCanvasReady) {
        _leftDock.render(_dockCanvas, _snap, _activePageId);
        _dockCanvas.pushSprite(&M5.Display, Viewport::DOCK_X, Viewport::DOCK_Y);
    }

    // 5. Render Right 1120x720 Safe Viewport Page + Floating 8-Dot Indicator & Push to (160, 0)
    if (_mainCanvasReady) {
        if (BasePage* page = activePage()) {
            page->render(_mainCanvas, _snap);
        } else {
            renderMissingPagePlaceholder(_mainCanvas, _activePageId);
        }

        applyPendingNavigation();

        // Floating bottom 8-dot carousel indicator (auto-hides cleanly after 3000ms idle)
        _pageIndicator.render(_mainCanvas, _activePageId, nowMs);

        // Pre-standby settle / OneDrive cloud sync HUD overlay
        renderPreSleepOverlay(_mainCanvas, nowMs);

        _mainCanvas.pushSprite(&M5.Display, Viewport::SAFE_X, Viewport::SAFE_Y);
    }
}
