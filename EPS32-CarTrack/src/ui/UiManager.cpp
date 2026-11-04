#include "ui/UiManager.h"
#include "driver/HapticDriver.h"
#include "driver/I2cBusManager.h"
#include "service/NetworkManager.h"
#include "service/PowerMonitorService.h"
#include <math.h>

void UiManager::init() {
    M5.Display.setRotation(3); // 1280 x 720 Standard Widescreen Landscape (180 deg flipped)
    M5.Display.setBrightness(200);
    M5.Display.fillScreen(UiColors::COLOR_BG);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(UiColors::COLOR_PRIMARY);
    M5.Display.setTextSize(2);
    M5.Display.drawString("M5Track-Tab5 Initializing...", Viewport::SCREEN_W / 2, Viewport::SCREEN_H / 2);

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
        case SUBPAGE_TOOL_ONEDRIVE: return &_toolOneDrive;
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
    I2cBusManager::instance().lockBus();
    M5.update(); // M5Unified hardware touch update with I2C bus lock protection
    I2cBusManager::instance().unlockBus();

    auto detail = M5.Touch.getDetail();
    size_t touchCount = M5.Touch.getCount();
    bool isDown = (touchCount > 0);

    uint32_t now = millis();

    if (isDown) {
        if (!_touchDown) {
            _touchDown        = true;
            _gestureTriggered = false;
            _touchStartX      = detail.x;
            _touchStartY      = detail.y;
            _touchLastX       = detail.x;
            _touchLastY       = detail.y;
            _touchDownMs      = now;
            _indicator.notifyInteraction();
            if (Serial) {
                Serial.printf("[TOUCH] Down at (%d, %d)\n", detail.x, detail.y);
            }
        } else {
            _touchLastX = detail.x;
            _touchLastY = detail.y;
            int16_t curDx = _touchLastX - _touchStartX;
            int16_t curDy = _touchLastY - _touchStartY;

            // 1. REAL-TIME SWIPE: Trigger immediately when displacement exceeds 16px
            if (!_gestureTriggered && abs(curDx) >= 16 && abs(curDx) > (abs(curDy) * 3 / 4)) {
                _gestureTriggered = true;
                _indicator.notifyInteraction();
                HapticDriver::instance().triggerShortPulse(35);

                SystemState& state = SystemState::instance();
                state.lock();
                SystemContext& ctx = state.getContextUnsafe();

                if (ctx.currentPage == PAGE_INIT || ctx.currentPage == PAGE_WIFI_CONFIG) {
                    ctx.currentPage = PAGE_1_DASHBOARD;
                    if (Serial) Serial.println("[TOUCH] Active Swipe -> PAGE_1_DASHBOARD");
                } else if (ctx.currentPage >= PAGE_1_DASHBOARD && ctx.currentPage <= PAGE_8_TOOLS) {
                    int p = (int)ctx.currentPage;
                    if (curDx < 0) {
                        // Swipe Left -> Next Page (1 -> 2 -> ... -> 8 -> 1)
                        p = (p >= (int)PAGE_8_TOOLS) ? (int)PAGE_1_DASHBOARD : (p + 1);
                    } else {
                        // Swipe Right -> Prev Page (1 -> 8 -> ... -> 2 -> 1)
                        p = (p <= (int)PAGE_1_DASHBOARD) ? (int)PAGE_8_TOOLS : (p - 1);
                    }
                    ctx.currentPage = (AppPageId)p;
                    if (Serial) Serial.printf("[TOUCH] Active Swipe Page -> %d\n", p);
                } else if (ctx.currentPage > PAGE_8_TOOLS && curDx > 0) {
                    // Subpage Swipe Right -> return to parent page
                    if (ctx.currentPage == SUBPAGE_EDIT_CLOCK) {
                        ctx.currentPage = PAGE_7_WORLD_CLOCK;
                    } else {
                        ctx.currentPage = PAGE_8_TOOLS;
                    }
                    if (Serial) Serial.printf("[TOUCH] Active Subpage Return -> %d\n", (int)ctx.currentPage);
                }
                state.unlock();
            }

            // 2. Continuous Hold (e.g. long press TRIP reset on Dashboard)
            if (!_gestureTriggered) {
                uint32_t holdDuration = now - _touchDownMs;
                SystemState& state = SystemState::instance();
                state.lock();
                SystemContext& ctx = state.getContextUnsafe();
                BasePage* page = getActivePage(ctx.currentPage);
                if (page) {
                    page->onTouchHold(detail.x, detail.y, holdDuration, ctx);
                }
                state.unlock();
            }
        }
    } else {
        // Finger lifted from screen
        if (_touchDown) {
            _touchDown = false;
            int16_t endX = _touchLastX;
            int16_t endY = _touchLastY;
            int16_t startX = _touchStartX;
            int16_t startY = _touchStartY;
            uint32_t duration = (now >= _touchDownMs && _touchDownMs > 0) ? (now - _touchDownMs) : 50;
            int16_t finalDx = endX - startX;
            int16_t finalDy = endY - startY;

            _touchStartX = 0;
            _touchStartY = 0;
            _touchLastX  = 0;
            _touchLastY  = 0;
            _touchDownMs = 0;

            if (Serial) {
                Serial.printf("[TOUCH] Release at (%d,%d) start=(%d,%d) dx=%d dy=%d dur=%ums trig=%d\n",
                              endX, endY, startX, startY, finalDx, finalDy, duration, (int)_gestureTriggered);
            }

            // If gesture was NOT already triggered during active drag:
            if (!_gestureTriggered) {
                // Check if quick flick release qualifies as a swipe
                if (abs(finalDx) >= 14 && abs(finalDx) > (abs(finalDy) * 3 / 4)) {
                    _gestureTriggered = true;
                    _indicator.notifyInteraction();
                    HapticDriver::instance().triggerShortPulse(35);

                    SystemState& state = SystemState::instance();
                    state.lock();
                    SystemContext& ctx = state.getContextUnsafe();

                    if (ctx.currentPage == PAGE_INIT || ctx.currentPage == PAGE_WIFI_CONFIG) {
                        ctx.currentPage = PAGE_1_DASHBOARD;
                    } else if (ctx.currentPage >= PAGE_1_DASHBOARD && ctx.currentPage <= PAGE_8_TOOLS) {
                        int p = (int)ctx.currentPage;
                        if (finalDx < 0) {
                            p = (p >= (int)PAGE_8_TOOLS) ? (int)PAGE_1_DASHBOARD : (p + 1);
                        } else {
                            p = (p <= (int)PAGE_1_DASHBOARD) ? (int)PAGE_8_TOOLS : (p - 1);
                        }
                        ctx.currentPage = (AppPageId)p;
                        if (Serial) Serial.printf("[TOUCH] Release Swipe Page -> %d\n", p);
                    } else if (ctx.currentPage > PAGE_8_TOOLS && finalDx > 0) {
                        if (ctx.currentPage == SUBPAGE_EDIT_CLOCK) {
                            ctx.currentPage = PAGE_7_WORLD_CLOCK;
                        } else {
                            ctx.currentPage = PAGE_8_TOOLS;
                        }
                    }
                    state.unlock();
                } else if (duration < 1000 && abs(finalDx) < 35 && abs(finalDy) < 35) {
                    // Tap Event!
                    _indicator.notifyInteraction();
                    HapticDriver::instance().triggerShortPulse(35);

                    SystemState& state = SystemState::instance();
                    state.lock();
                    SystemContext& ctx = state.getContextUnsafe();

                    // Bottom Page Indicator Dots direct jump:
                    if (endY >= 660 && endX >= 590 && endX <= 850 &&
                        ctx.currentPage >= PAGE_1_DASHBOARD && ctx.currentPage <= PAGE_8_TOOLS) {
                        int dotIdx = (endX - 610) / 24;
                        if (dotIdx >= 0 && dotIdx < 8) {
                            ctx.currentPage = (AppPageId)(PAGE_1_DASHBOARD + dotIdx);
                            if (Serial) Serial.printf("[TOUCH] Dot Direct Jump -> %d\n", (int)ctx.currentPage);
                            state.unlock();
                            _gestureTriggered = false;
                            return;
                        }
                    }

                    // Left Dock Tap (X: 0..160)
                    if (ctx.currentPage != PAGE_INIT && ctx.currentPage != PAGE_WIFI_CONFIG && endX < Viewport::DOCK_W) {
                        DockTouchAction act = _dock.handleTouchTap(endX, endY);
                        if (Serial) Serial.printf("[TOUCH] Dock Action -> %d\n", (int)act);
                        applyDockAction(act, ctx);
                        state.unlock();
                        _gestureTriggered = false;
                        return;
                    }

                    // Active Page Tap
                    BasePage* page = getActivePage(ctx.currentPage);
                    if (page) {
                        bool handled = page->onTouchTap(endX, endY, ctx);
                        if (Serial) {
                            Serial.printf("[TOUCH] Page %d TouchTap(%d, %d) -> handled=%d, current=%d\n",
                                          (int)ctx.currentPage, endX, endY, handled, (int)ctx.currentPage);
                        }
                    }
                    state.unlock();
                }
            }
            _gestureTriggered = false;
        }
    }
}

extern PowerMonitorService g_powerService;

void UiManager::handleTouch() {
    // If system is in low-power standby with display sleeping, check for screen touch to wake
    if (g_powerService.isSleeping()) {
        I2cBusManager::instance().lockBus();
        M5.update();
        I2cBusManager::instance().unlockBus();
        if (M5.Touch.getCount() > 0) {
            g_powerService.wakeFromStandby("Screen touch detected");
        }
        return;
    }

    handleTouchAndGestures();
}

void UiManager::render() {
    if (!_canvas) return;

    SystemState::instance().copySnapshotTo(_snapBuf);

    BasePage* page = getActivePage(_snapBuf.currentPage);
    if (page) {
        page->render(*_canvas, _snapBuf);
    }

    // Render resident 160px Left Dock and floating OSD Indicator on all pages except full-screen Boot/QR Setup
    if (_snapBuf.currentPage != PAGE_INIT && _snapBuf.currentPage != PAGE_WIFI_CONFIG) {
        _dock.render(*_canvas, _snapBuf);
        _indicator.render(*_canvas, _snapBuf.currentPage);
    }

    // Real-Time Touch Visualizer & Diagnostic Capsule (Aids hardware touch verification)
    if (_touchDown || (millis() - _touchVisualMs < 1000)) {
        int16_t curX = _touchLastX;
        int16_t curY = _touchLastY;
        if (curX > 0 && curY > 0) {
            _touchVisualMs = millis();
            _canvas->fillCircle(curX, curY, 14, UiColors::COLOR_PRIMARY);
            _canvas->drawCircle(curX, curY, 22, UiColors::COLOR_WHITE);
        }
    }

    // On-screen touch diagnostic status at top-right
    _canvas->setTextDatum(top_right);
    _canvas->setTextSize(1);
    _canvas->setTextColor(UiColors::COLOR_MUTED);
    char dbgBuf[64];
    snprintf(dbgBuf, sizeof(dbgBuf), "T: cnt=%u down=%d x=%d y=%d P=%d",
             (unsigned)M5.Touch.getCount(), (int)_touchDown, (int)_touchLastX, (int)_touchLastY, (int)_snapBuf.currentPage);
    _canvas->drawString(dbgBuf, 1260, 8);

    _canvas->pushSprite(0, 0);
}

void UiManager::update() {
    handleTouch();
    render();
}
