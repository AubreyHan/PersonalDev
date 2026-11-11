#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include <functional>
#include "../../config.h"
#include "../../model/SystemState.h"

// =============================================================================
// M5Track-Tab5 Unified UI Page Routing, Dock Actions & BasePage Contract
// Viewport Split:
//   - Left Dock     : X = 0..159,   Y = 0..719 (160 x 720, permanent sidebar)
//   - Safe Viewport : X = 160..1279, Y = 0..719 (1120 x 720, BasePage M5Canvas)
// =============================================================================

// -----------------------------------------------------------------------------
// 1. Unified Page Routing Enumeration (Covers PRD §3.1 ~ §3.18 Completely)
// -----------------------------------------------------------------------------
enum PageId : uint8_t {
    // Boot & Provisioning Pages
    PAGE_INIT               = 0,  // Boot hardware self-test, radar sat search & FAT32 prompt
    PAGE_WIFI_CONFIG        = 1,  // SoftAP Captive Portal (M5Track-Setup) + Dual QR codes

    // 8 Primary Horizontal-Swipe Carousel Pages (Right 1120x720 Viewport)
    PAGE_1_DASHBOARD        = 2,  // 360px Compass + 120pt Speed + Alt + TRIP 2s Reset + [TRIP/DAY/SUB]
    PAGE_2_ALTITUDE         = 3,  // Fused Altitude + Climb + 1050px 1h Chart + Max/Min Extremes
    PAGE_3_SPEED            = 4,  // Speed + MAX/AVG + 1050px 1h Chart + Highway/Urban/Slow %
    PAGE_4_TRAIL            = 5,  // Full-Bleed Auto-Fitting Vector Trail + 4-Corner HUD + Follow Mode
    PAGE_5_DYNAMICS         = 6,  // 400px G-Force Circle (0.5s trail) + Slope % + Pitch/Roll Level
    PAGE_6_WEATHER          = 7,  // Current Weather + 93px/col 12h Front/Back Toggle + Hazard Alert
    PAGE_7_WORLD_CLOCK      = 8,  // 2x2 530x320 Aviation Multi-Timezone World Clock Cards
    PAGE_TOOLS_GRID         = 9,  // Page 8: 2x2 Expandable System Toolbox Grid (>4 auto-paginates)

    // Secondary Detail & Configuration Subpages
    SUBPAGE_EDIT_CLOCK      = 10, // Edit World Clock Card (GPS Longitude Auto-TZ / Region Picker)
    SUBPAGE_TOOL_TF_MANAGER = 11, // Tool 1: FAT32 Format, Tri-Folder GPX Browser & M5Track-Share Export
    SUBPAGE_TOOL_WIFI       = 12, // Tool 2: Saved Wi-Fi Slots (0..4), Delete & Launch Setup Portal
    SUBPAGE_TOOL_GNSS       = 13, // Tool 3: 4-Constellation Skyplot, C/N0 Bars & RF/Antenna Status
    SUBPAGE_TOOL_ONEDRIVE   = 14, // Tool 4: OneDrive Device Code QR/Code, Sync Progress & Unbind
    SUBPAGE_TOOL_SETTINGS   = 15, // Tool 5: Backlight Slider, Beep Toggle, INA226 Gauge & System Info

    PAGE_COUNT              = 16,
    PAGE_NONE               = 0xFF
};

// Convenient alias so both PAGE_TOOLS_GRID and PAGE_8_TOOLS_GRID compile identically
constexpr PageId PAGE_8_TOOLS_GRID = PAGE_TOOLS_GRID;

// Helpers for the 8-page main horizontal swipe carousel (PAGE_1_DASHBOARD .. PAGE_TOOLS_GRID)
inline constexpr bool isCarouselMainPage(PageId id) {
    return (id >= PAGE_1_DASHBOARD && id <= PAGE_TOOLS_GRID);
}

inline constexpr uint8_t getCarouselPageIndex(PageId id) {
    return isCarouselMainPage(id) ? static_cast<uint8_t>(id - PAGE_1_DASHBOARD) : 0U;
}

inline constexpr PageId carouselPageFromIndex(uint8_t index) {
    return (index < 8U) ? static_cast<PageId>(PAGE_1_DASHBOARD + index) : PAGE_1_DASHBOARD;
}

// -----------------------------------------------------------------------------
// 2. Permanent Left Dock (160x720) Dedicated Touch Action Enumeration
// -----------------------------------------------------------------------------
enum DockTouchAction : uint8_t {
    DOCK_ACTION_NONE    = 0,
    DOCK_ACTION_HOME    = 1, // [0..89]    Home icon   -> PAGE_1_DASHBOARD
    DOCK_ACTION_TIME    = 2, // [90..199]  Clock       -> PAGE_7_WORLD_CLOCK
    DOCK_ACTION_GNSS    = 3, // [200..304] Satellites  -> SUBPAGE_TOOL_GNSS
    DOCK_ACTION_WIFI    = 4, // [305..409] Wi-Fi       -> SUBPAGE_TOOL_WIFI (or PAGE_WIFI_CONFIG)
    DOCK_ACTION_BATTERY = 5, // [410..514] Power/SoC   -> SUBPAGE_TOOL_SETTINGS (INA226 details)
    DOCK_ACTION_WEATHER = 6, // [515..619] Weather     -> PAGE_6_WEATHER
    DOCK_ACTION_TOOLS   = 7  // [620..719] Toolbox     -> PAGE_TOOLS_GRID
};

inline constexpr DockTouchAction hitTestLeftDock(int16_t screenX, int16_t screenY) {
    if (screenX < Viewport::DOCK_X || screenX >= (Viewport::DOCK_X + Viewport::DOCK_W) ||
        screenY < Viewport::DOCK_Y || screenY >= (Viewport::DOCK_Y + Viewport::DOCK_H)) {
        return DOCK_ACTION_NONE;
    }
    if (screenY < Viewport::DOCK_TIME_Y)    return DOCK_ACTION_HOME;
    if (screenY < Viewport::DOCK_GNSS_Y)    return DOCK_ACTION_TIME;
    if (screenY < Viewport::DOCK_WIFI_Y)    return DOCK_ACTION_GNSS;
    if (screenY < Viewport::DOCK_BATTERY_Y) return DOCK_ACTION_WIFI;
    if (screenY < Viewport::DOCK_WEATHER_Y) return DOCK_ACTION_BATTERY;
    if (screenY < Viewport::DOCK_TOOLS_Y)   return DOCK_ACTION_WEATHER;
    return DOCK_ACTION_TOOLS;
}

// -----------------------------------------------------------------------------
// 3. Normalized Touch Event Passed to BasePage::onTouch()
// Provides BOTH 1120x720 canvas-local coordinates (localX, localY) matching
// M5Canvas drawing calls AND 1280x720 global screen coordinates (screenX, screenY).
// -----------------------------------------------------------------------------
enum TouchEventType : uint8_t {
    TOUCH_EVT_NONE = 0,
    TOUCH_EVT_DOWN = 1, // Finger just pressed down
    TOUCH_EVT_MOVE = 2, // Finger dragging / moving while pressed
    TOUCH_EVT_HOLD = 3, // Finger held down (check holdDurationMs, e.g. >= 2000ms for TRIP reset)
    TOUCH_EVT_TAP  = 4, // Short clean tap released within jitter threshold
    TOUCH_EVT_UP   = 5  // Finger lifted
};

struct TouchEvent {
    TouchEventType type           = TOUCH_EVT_NONE;
    int16_t        localX         = 0;     // 0 .. 1119 (relative to 1120x720 right viewport canvas)
    int16_t        localY         = 0;     // 0 .. 719
    int16_t        screenX        = 0;     // 0 .. 1279 (global Tab5 landscape touch X)
    int16_t        screenY        = 0;     // 0 .. 719  (global Tab5 landscape touch Y)
    int16_t        startLocalX    = 0;     // Initial press-down canvas-local X
    int16_t        startLocalY    = 0;     // Initial press-down canvas-local Y
    int16_t        deltaX         = 0;     // Frame-to-frame or total drag delta X
    int16_t        deltaY         = 0;     // Frame-to-frame or total drag delta Y
    uint32_t       holdDurationMs = 0;     // Elapsed press duration in ms
    bool           isPressed      = false;

    // Hit-test against coordinates drawn on the 1120x720 BasePage M5Canvas
    constexpr bool hitTestLocal(int16_t x, int16_t y, int16_t w, int16_t h) const {
        return (localX >= x) && (localX < (x + w)) && (localY >= y) && (localY < (y + h));
    }

    // Hit-test against global 1280x720 screen coordinates
    constexpr bool hitTestScreen(int16_t x, int16_t y, int16_t w, int16_t h) const {
        return (screenX >= x) && (screenX < (x + w)) && (screenY >= y) && (screenY < (y + h));
    }
};

// -----------------------------------------------------------------------------
// 4. Abstract Base Class for All M5Track-Tab5 UI Pages & Subpages
// -----------------------------------------------------------------------------
class BasePage {
public:
    using NavigateCallback = std::function<void(PageId)>;
    using ToneCallback     = std::function<void(uint16_t, uint32_t)>;

    BasePage() = default;
    virtual ~BasePage() = default;

    // Unique route identifier of this page
    virtual PageId pageId() const = 0;

    // Human-readable title for logs/headers
    virtual const char* pageTitle() const { return ""; }

    // Whether UiManager should allow global left/right swipe page switching on this page
    virtual bool allowsHorizontalSwipe() const { return isCarouselMainPage(pageId()); }

    // Lifecycle hooks invoked by UiManager on route transitions
    virtual void onEnter(const SystemStateSnapshot& state) { (void)state; }
    virtual void onExit() {}

    // Render into the 1120x720 PSRAM off-screen M5Canvas (coordinates: X=0..1119, Y=0..719).
    // UiManager clears or prepares the canvas and pushes it to screen (160, 0) at 60fps.
    virtual void render(M5Canvas& canvas, const SystemStateSnapshot& state) = 0;

    // Handle touch events inside the 1120x720 safe viewport.
    // Return true if consumed by an interactive widget/button/slider (suppresses swipe).
    virtual bool onTouch(const TouchEvent& event, const SystemStateSnapshot& state) {
        (void)event;
        (void)state;
        return false;
    }

    // Wired by UiManager during registration
    void setNavigateCallback(NavigateCallback cb) { _navigateCb = std::move(cb); }
    void setToneCallback(ToneCallback cb) { _toneCb = std::move(cb); }

    // Request navigation to another page (supports both callback & polling by UiManager)
    void requestNavigate(PageId targetPage) {
        _pendingTarget = targetPage;
        if (_navigateCb) {
            _navigateCb(targetPage);
        }
    }

    // Allows UiManager to poll pending navigation if callback wasn't used
    bool consumePendingNavigation(PageId& outTarget) {
        if (_pendingTarget == PAGE_NONE) {
            return false;
        }
        outTarget      = _pendingTarget;
        _pendingTarget = PAGE_NONE;
        return true;
    }

    // Emit audio-tactile speaker click/chime (respects UserSettings::speakerBeepEnabled)
    void playFeedbackTone(const SystemStateSnapshot& state,
                          uint16_t freqHz = HapticConfig::TONE_TAP_HZ,
                          uint32_t durationMs = HapticConfig::TONE_TAP_MS) {
        if (!state.settings.speakerBeepEnabled) {
            return;
        }
        if (_toneCb) {
            _toneCb(freqHz, durationMs);
        } else if (M5.Speaker.isEnabled()) {
            M5.Speaker.tone(freqHz, durationMs);
        }
    }

    // Helper: enforce pure black background fill on the 1120x720 page canvas
    static void clearViewportCanvas(M5Canvas& canvas) {
        canvas.fillScreen(TFT_BLACK);
    }

protected:
    NavigateCallback _navigateCb    = nullptr;
    ToneCallback     _toneCb        = nullptr;
    PageId           _pendingTarget = PAGE_NONE;
};
