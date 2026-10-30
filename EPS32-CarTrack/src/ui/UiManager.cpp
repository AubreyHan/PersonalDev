#include "ui/UiManager.h"
#include "driver/HapticDriver.h"
#include "service/NetworkManager.h"
#include <math.h>

void UiManager::init() {
    M5.Display.setRotation(1); // 1280 x 720 Standard Widescreen Landscape
    M5.Display.setBrightness(200);
    M5.Display.fillScreen(UiColors::COLOR_BG);

    // Allocate 1280x720 16-bit double-buffer canvas in 32MB PSRAM (~1.84MB)
    _canvas = new M5Canvas(&M5.Display);
    _canvas->setPsram(true);
    _canvas->setColorDepth(16);
    if (!_canvas->createSprite(Viewport::SCREEN_W, Viewport::SCREEN_H)) {
        Serial.println("[UI] Warning: Full 1280x720 16-bit sprite fallback to 8-bit color depth.");
        _canvas->setColorDepth(8);
        _canvas->createSprite(Viewport::SCREEN_W, Viewport::SCREEN_H);
    }
    _indicator.notifyInteraction();
}

BasePage* UiManager::getActivePage(AppPageId id) {
    switch (id) {
        case PAGE_INIT:          return &_pageInit;
        case PAGE_WIFI_CONFIG:   return &_pageWifiConfig;
        case PAGE_1_DASHBOARD:   return &_page1;
        case PAGE_2_ALTITUDE:    return &_page2;
        case PAGE_3_SPEED:       return &_page3;
        case PAGE_4_TRAIL:       return &_page4;
        case PAGE_5_DYNAMICS:    return &_page5;
        case PAGE_6_WEATHER:     return &_page6;
        case PAGE_7_WORLD_CLOCK: return &_page7;
        case SUBPAGE_EDIT_CLOCK: return &_subEditClock;
        case PAGE_8_TOOLS:       return &_page8;
        case SUBPAGE_TOOL_TF:    return &_toolTf;
        case SUBPAGE_TOOL_WIFI:  return &_toolWifi;
        case SUBPAGE_TOOL_GNSS:  return &_toolGnss;
        case SUBPAGE_TOOL_SYS:   return &_toolSys;
        default:                 return &_page1;
    }
}

void UiManager::applyDockAction(DockTouchAction act, SystemContext& ctx) {
    if (act == DOCK_ACTION_NONE) return;
    AppNetworkManager::instance().stopPortals();
    switch (act) {
        case DOCK_ACTION_HOME:    ctx.currentPage = PAGE_1_DASHBOARD;   break;
        case DOCK_ACTION_TIME:    ctx.currentPage = PAGE_7_WORLD_CLOCK; break;
        case DOCK_ACTION_GNSS:    ctx.currentPage = SUBPAGE_TOOL_GNSS;  break;
        case DOCK_ACTION_WIFI:    ctx.currentPage = SUBPAGE_TOOL_WIFI;  break;
        case DOCK_ACTION_BATTERY: ctx.currentPage = SUBPAGE_TOOL_SYS;   break;
        case DOCK_ACTION_WEATHER: ctx.currentPage = PAGE_6_WEATHER;     break;
        case DOCK_ACTION_TOOLS:   ctx.currentPage = PAGE_8_TOOLS;       break;
        default: break;
    }
}

void UiManager::handleTouchAndGestures() {
    M5.update();
    HapticDriver::instance().update();

    auto count = M5.Touch.getCount();
    if (count > 0) {
        auto det = M5.Touch.getDetail(0);
        int16_t tx = det.x;
        int16_t ty = det.y;

        if (!_touchDown) {
            _touchDown   = true;
            _touchStartX = tx;
            _touchStartY = ty;
            _touchLastX  = tx;
            _touchLastY  = ty;
            _touchDownMs = millis();
            _indicator.notifyInteraction();
        } else {
            _touchLastX = tx;
            _touchLastY = ty;
            uint32_t holdDuration = millis() - _touchDownMs;

            SystemState& state = SystemState::instance();
            state.lock();
            SystemContext& ctx = state.getContextUnsafe();
            BasePage* page = getActivePage(ctx.currentPage);
            state.unlock();

            if (page) {
                page->onTouchHold(tx, ty, holdDuration, state.getContextUnsafe());
            }
        }
    } else if (_touchDown) {
        _touchDown = false;
        int16_t dx = _touchLastX - _touchStartX;
        int16_t dy = _touchLastY - _touchStartY;
        uint32_t duration = millis() - _touchDownMs;
        _indicator.notifyInteraction();

        SystemState& state = SystemState::instance();
        state.lock();
        SystemContext& ctx = state.getContextUnsafe();

        // Check horizontal swipe gesture between Main Pages (Pages 1 .. 8)
        if (abs(dx) >= GestureConfig::SWIPE_HORIZ_THRESHOLD_PX &&
            abs(dx) > abs(dy) * GestureConfig::DIRECTION_RATIO &&
            ctx.currentPage >= PAGE_1_DASHBOARD && ctx.currentPage <= PAGE_8_TOOLS) {
            int p = (int)ctx.currentPage;
            if (dx < 0) {
                // Swipe left -> next page
                p++;
                if (p > (int)PAGE_8_TOOLS) p = (int)PAGE_1_DASHBOARD;
            } else {
                // Swipe right -> previous page
                p--;
                if (p < (int)PAGE_1_DASHBOARD) p = (int)PAGE_8_TOOLS;
            }
            ctx.currentPage = (AppPageId)p;
            state.unlock();
            return;
        }

        // Short tap (< 1200ms)
        if (duration < 1200 && abs(dx) < 35 && abs(dy) < 35) {
            if (ctx.currentPage != PAGE_INIT && ctx.currentPage != PAGE_WIFI_CONFIG && _touchLastX < Viewport::DOCK_W) {
                DockTouchAction act = _dock.handleTouchTap(_touchLastX, _touchLastY);
                applyDockAction(act, ctx);
                state.unlock();
                return;
            }

            BasePage* page = getActivePage(ctx.currentPage);
            if (page) {
                page->onTouchTap(_touchLastX, _touchLastY, ctx);
            }
        }
        state.unlock();
    }
}

void UiManager::update() {
    handleTouchAndGestures();
    if (!_canvas) return;

    SystemContext snap = SystemState::instance().getSnapshot();

    BasePage* page = getActivePage(snap.currentPage);
    if (page) {
        page->render(*_canvas, snap);
    }

    // Render resident 160px Left Dock and floating OSD Indicator on all pages except full-screen Boot/QR Setup
    if (snap.currentPage != PAGE_INIT && snap.currentPage != PAGE_WIFI_CONFIG) {
        _dock.render(*_canvas, snap);
        _indicator.render(*_canvas, snap.currentPage);
    }

    _canvas->pushSprite(0, 0);
}
