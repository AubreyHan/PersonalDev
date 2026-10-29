#include "ui/UiManager.h"
#include "driver/PowerDriver.h"
#include "config.h"

void UiManager::init() {
    M5.Display.setRotation(1); // Standard Landscape 320x240, Type-C on left
    _canvas.setColorDepth(16);
    _canvas.createSprite(320, 240);
    _canvas.setFont(&fonts::efontCN_10);
    _pageIndicator.wake();
    _lastRenderMs = 0;
}

BasePage* UiManager::getActivePage(uint8_t idx) {
    switch (idx) {
        case 0: return &_page1Dash;
        case 1: return &_page2Alt;
        case 2: return &_page3Speed;
        case 3: return &_page4Trail;
        case 4: return &_page5Dyn;
        case 5: return &_page6Weather;
        case 6: return &_page7Clock;
        case 7: return &_page8Tools;
        default: return &_page1Dash;
    }
}

bool UiManager::handleTouchEvents() {
    TouchEvent evt = ButtonDriver::instance().update();
    if (evt.type == TOUCH_NONE) return false;

    _pageIndicator.wake();

    SystemState& state = SystemState::instance();
    state.lock();
    SystemContext& ctx = state.getContextUnsafe();
    AppState appState = ctx.currentState;
    uint8_t curPage = ctx.mainPageIdx;
    uint8_t totalPages = ctx.totalMainPages;
    state.unlock();

    // Global Hardware Buttons (BtnA = Home, BtnC = Tools) work from ANY state
    if (evt.type == TOUCH_BTN_HOME) {
        PowerDriver::instance().vibrate(30);
        _page8Tools.resetToRoot();
        state.lock();
        ctx.mainPageIdx = 0;
        if (ctx.currentState == STATE_BOOT_INIT) {
            ctx.currentState = STATE_MAIN_APP;
        }
        state.unlock();
        return true;
    }

    if (evt.type == TOUCH_BTN_TOOLS) {
        PowerDriver::instance().vibrate(30);
        _page8Tools.resetToRoot();
        state.lock();
        ctx.mainPageIdx = 7; // Page 8 (Index 7)
        if (ctx.currentState == STATE_BOOT_INIT) {
            ctx.currentState = STATE_MAIN_APP;
        }
        state.unlock();
        return true;
    }

    if (appState == STATE_BOOT_INIT) {
        if (evt.type == TOUCH_TAP) {
            _pageInit.handleTap(evt.x, evt.y);
        } else if (evt.type == TOUCH_SWIPE_LEFT || evt.type == TOUCH_SWIPE_RIGHT || evt.type == TOUCH_SWIPE_UP) {
            // Allow swiping directly out of boot screen into main dashboard
            state.lock();
            ctx.currentState = STATE_MAIN_APP;
            ctx.mainPageIdx = 0;
            state.unlock();
        }
        return true;
    }

    if (appState == STATE_WIFI_CONFIG) {
        if (evt.type == TOUCH_TAP) _pageWifi.handleTap(evt.x, evt.y);
        return true;
    }

    // In STATE_MAIN_APP
    BasePage* page = getActivePage(curPage);

    switch (evt.type) {
        case TOUCH_SWIPE_LEFT:
            state.lock();
            ctx.mainPageIdx = (ctx.mainPageIdx + 1) % totalPages;
            state.unlock();
            break;

        case TOUCH_SWIPE_RIGHT:
            state.lock();
            ctx.mainPageIdx = (ctx.mainPageIdx + totalPages - 1) % totalPages;
            state.unlock();
            break;

        case TOUCH_SWIPE_UP:
            if (page) page->handleSwipeUp();
            break;

        case TOUCH_SWIPE_DOWN:
            if (page) page->handleSwipeDown();
            break;

        case TOUCH_TAP: {
            uint8_t targetPage = 0;
            if (StatusBar::instance().checkTap(evt.x, evt.y, targetPage)) {
                state.lock();
                ctx.mainPageIdx = targetPage;
                state.unlock();
            } else {
                if (page) page->handleTap(evt.x, evt.y);
            }
            break;
        }

        case TOUCH_LONG_PRESS_2S:
            if (page) page->handleLongPress(evt.x, evt.y);
            break;

        case TOUCH_BTN_TRIP_RESET:
            // Button 2 (Middle / BtnB): 1.5s Long Press -> Full Trip Sync Reset
            PowerDriver::instance().vibrate(100);
            state.lock();
            ctx.trip.reset(ctx.gnss.altitudeMeters, ctx.gnss.speedKmh, millis() / 1000);
            ctx.trail.reset(ctx.gnss.latE7, ctx.gnss.lonE7, (int16_t)ctx.gnss.altitudeMeters);
            ctx.dynamics.maxLateralG = 0.0f;
            ctx.dynamics.maxLongG = 0.0f;
            state.unlock();
            _page1Dash.handleLongPress(200, 200); // Trigger feedback popup
            break;

        default:
            break;
    }
    return true;
}

void UiManager::update() {
    bool touched = handleTouchEvents();
    unsigned long now = millis();
    if (!touched && (now - _lastRenderMs < 50)) {
        return; // Poll touch at ~125Hz, render display at 20 FPS or immediately on touch
    }
    _lastRenderMs = now;

    SystemState& state = SystemState::instance();
    state.lock();
    const SystemContext& ctx = state.getContextUnsafe();

    _canvas.fillScreen(UiColors::COLOR_BG);

    if (ctx.currentState == STATE_BOOT_INIT) {
        _pageInit.draw(_canvas, ctx);
    } else if (ctx.currentState == STATE_WIFI_CONFIG) {
        _pageWifi.draw(_canvas, ctx);
    } else if (ctx.currentState == STATE_PWR_OFF_WARN) {
        // Power-off warning dialog
        _canvas.fillScreen(UiColors::COLOR_BG);
        _canvas.fillRoundRect(30, 60, 260, 120, 8, UiColors::COLOR_PANEL_BG);
        _canvas.drawRoundRect(30, 60, 260, 120, 8, UiColors::COLOR_WARN);

        _canvas.setTextSize(2);
        _canvas.setTextColor(UiColors::COLOR_WARN, UiColors::COLOR_PANEL_BG);
        _canvas.drawCenterString("外部供电已断开", 160, 75);

        char cdBuf[32];
        snprintf(cdBuf, sizeof(cdBuf), "%d 秒后安全关机", ctx.pwrCountdownSec);
        _canvas.setTextSize(2);
        _canvas.setTextColor(UiColors::COLOR_DANGER, UiColors::COLOR_PANEL_BG);
        _canvas.drawCenterString(cdBuf, 160, 108);

        _canvas.setTextSize(1);
        _canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
        _canvas.drawCenterString("正在安全保存轨迹文件...", 160, 142);
    } else {
        // STATE_MAIN_APP
        // 1. Draw Active Page into 320x216 safe content area
        BasePage* page = getActivePage(ctx.mainPageIdx);
        if (page) {
            page->draw(_canvas, ctx);
        }

        // 2. Draw Top Status Bar (Always on top Y: 0~23)
        _statusBar.draw(_canvas, ctx);

        // 3. Draw Floating Page Indicator (Y: 228~235, auto-hides after 3s)
        _pageIndicator.draw(_canvas, ctx.mainPageIdx, ctx.totalMainPages);
    }

    state.unlock();

    _canvas.pushSprite(0, 0);
}
