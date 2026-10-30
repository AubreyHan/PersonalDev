#include "ui/components/LeftDock.h"
#include "service/TimeService.h"
#include <math.h>

extern TimeService g_timeService;

void LeftDock::render(M5Canvas& canvas, const SystemContext& ctx) {
    // Clear Left Dock region (X: 0..159, Y: 0..719)
    canvas.fillRect(Viewport::DOCK_X, Viewport::DOCK_Y, Viewport::DOCK_W, Viewport::DOCK_H, UiColors::COLOR_PANEL_BG);
    canvas.drawFastVLine(Viewport::DOCK_W - 1, 0, Viewport::DOCK_H, UiColors::COLOR_BORDER);

    canvas.setTextDatum(top_center);

    // 1. HOME (Y: 0..89, H: 90)
    bool isHome = (ctx.currentPage == PAGE_1_DASHBOARD);
    if (isHome) {
        canvas.fillRoundRect(8, Viewport::DOCK_HOME_Y + 6, 142, Viewport::DOCK_HOME_H - 12, 8, 0x0208);
        canvas.drawRoundRect(8, Viewport::DOCK_HOME_Y + 6, 142, Viewport::DOCK_HOME_H - 12, 8, UiColors::COLOR_PRIMARY);
    }
    canvas.setTextColor(isHome ? UiColors::COLOR_PRIMARY : UiColors::COLOR_WHITE);
    canvas.setTextSize(2);
    canvas.drawString("HOME", 80, Viewport::DOCK_HOME_Y + 18);
    canvas.setTextSize(1);
    canvas.setTextColor(isHome ? UiColors::COLOR_PRIMARY : UiColors::COLOR_MUTED);
    canvas.drawString("DASHBOARD", 80, Viewport::DOCK_HOME_Y + 52);
    canvas.drawFastHLine(10, Viewport::DOCK_TIME_Y, 140, UiColors::COLOR_BORDER);

    // 2. CLOCK (Y: 90..199, H: 110) -> Tap jumps to Page 7 World Clock
    uint16_t y = 2026; uint8_t m = 10, d = 30, hh = 14, mm = 35, ss = 28, wd = 5;
    g_timeService.getLocalTimeParts(ctx.clocks[0].tzOffsetMinutes, ctx.clocks[0].autoDst, y, m, d, hh, mm, ss, wd);
    bool isClock = (ctx.currentPage == PAGE_7_WORLD_CLOCK || ctx.currentPage == SUBPAGE_EDIT_CLOCK);
    char timeBuf[16], secBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u", hh, mm);
    snprintf(secBuf,  sizeof(secBuf),  ":%02u SEC", ss);
    canvas.setTextColor(isClock ? UiColors::COLOR_PRIMARY : UiColors::COLOR_WHITE);
    canvas.setTextSize(3);
    canvas.drawString(timeBuf, 80, Viewport::DOCK_TIME_Y + 22);
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString(secBuf, 80, Viewport::DOCK_TIME_Y + 68);
    canvas.drawFastHLine(10, Viewport::DOCK_GNSS_Y, 140, UiColors::COLOR_BORDER);

    // 3. GNSS (Y: 200..304, H: 105) -> Tap jumps to ToolGnss
    bool isGnss = (ctx.currentPage == SUBPAGE_TOOL_GNSS);
    uint16_t gnssCol = ctx.gnss.isValid ? UiColors::COLOR_PRIMARY : (ctx.gnss.isDeadReckoning ? UiColors::COLOR_WARN : UiColors::COLOR_MUTED);
    canvas.setTextColor(isGnss ? UiColors::COLOR_SECONDARY : gnssCol);
    canvas.setTextSize(2);
    char satBuf[24];
    snprintf(satBuf, sizeof(satBuf), "SAT %u", ctx.gnss.numSats);
    canvas.drawString(satBuf, 80, Viewport::DOCK_GNSS_Y + 20);
    canvas.setTextSize(1);
    const char* fixStr = ctx.gnss.isValid ? "3D FIX" : (ctx.gnss.isDeadReckoning ? "IMU DR" : "SEARCH");
    canvas.drawString(fixStr, 80, Viewport::DOCK_GNSS_Y + 56);
    if (ctx.storage.isRecording && ((millis() / 500) % 2 == 0)) {
        canvas.fillCircle(26, Viewport::DOCK_GNSS_Y + 60, 5, UiColors::COLOR_DANGER);
    }
    canvas.drawFastHLine(10, Viewport::DOCK_WIFI_Y, 140, UiColors::COLOR_BORDER);

    // 4. WIFI (Y: 305..409, H: 105) -> Tap jumps to ToolWifi
    uint16_t wifiCol = ctx.wifi.isConnected ? UiColors::COLOR_PRIMARY : (ctx.wifi.apSetupActive || ctx.wifi.apShareActive ? UiColors::COLOR_SECONDARY : UiColors::COLOR_MUTED);
    canvas.setTextColor(wifiCol);
    canvas.setTextSize(2);
    canvas.drawString(ctx.wifi.isConnected ? "WIFI ON" : (ctx.wifi.apShareActive ? "AP SHARE" : (ctx.wifi.apSetupActive ? "AP SETUP" : "WIFI OFF")), 80, Viewport::DOCK_WIFI_Y + 20);
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString(ctx.wifi.isConnected ? ctx.wifi.ssid : "Tap to Config", 80, Viewport::DOCK_WIFI_Y + 56);
    canvas.drawFastHLine(10, Viewport::DOCK_BATTERY_Y, 140, UiColors::COLOR_BORDER);

    // 5. BATTERY & INA226 COULOMB GAUGE (Y: 410..514, H: 105)
    // Dual-Color Spec: External Power -> Green (#00FFAA), Battery Only -> Red (#FF3B30)
    uint16_t batColor = ctx.power.hasExternalPower ? PowerColorConfig::COLOR_EXTERNAL_POWER_GREEN
                                                   : PowerColorConfig::COLOR_BATTERY_ONLY_RED;
    canvas.setTextColor(batColor);
    canvas.setTextSize(2);
    char batBuf[24];
    snprintf(batBuf, sizeof(batBuf), "%s%d%%", ctx.power.hasExternalPower ? "CHG " : "BAT ", (int)roundf(ctx.power.batterySocPercent));
    canvas.drawString(batBuf, 80, Viewport::DOCK_BATTERY_Y + 20);
    canvas.setTextSize(1);
    char vBuf[24];
    snprintf(vBuf, sizeof(vBuf), "%.2fV %+.0fmA", ctx.power.busVoltageV, ctx.power.currentMa);
    canvas.drawString(vBuf, 80, Viewport::DOCK_BATTERY_Y + 56);
    canvas.drawFastHLine(10, Viewport::DOCK_WEATHER_Y, 140, UiColors::COLOR_BORDER);

    // 6. WEATHER (Y: 515..619, H: 105) -> Tap jumps to Page 6 Weather
    bool isWeather = (ctx.currentPage == PAGE_6_WEATHER);
    canvas.setTextColor(isWeather ? UiColors::COLOR_PRIMARY : UiColors::COLOR_SECONDARY);
    canvas.setTextSize(2);
    char tempBuf[20];
    snprintf(tempBuf, sizeof(tempBuf), "%.1f C", ctx.weather.currentTempC);
    canvas.drawString(tempBuf, 80, Viewport::DOCK_WEATHER_Y + 20);
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString("WEATHER 12H", 80, Viewport::DOCK_WEATHER_Y + 56);
    canvas.drawFastHLine(10, Viewport::DOCK_TOOLS_Y, 140, UiColors::COLOR_BORDER);

    // 7. TOOLS (Y: 620..719, H: 100) -> Tap jumps to Page 8 Tools Grid
    bool isTools = (ctx.currentPage >= PAGE_8_TOOLS);
    if (isTools) {
        canvas.fillRoundRect(8, Viewport::DOCK_TOOLS_Y + 6, 142, Viewport::DOCK_TOOLS_H - 12, 8, 0x0208);
        canvas.drawRoundRect(8, Viewport::DOCK_TOOLS_Y + 6, 142, Viewport::DOCK_TOOLS_H - 12, 8, UiColors::COLOR_PRIMARY);
    }
    canvas.setTextColor(isTools ? UiColors::COLOR_PRIMARY : UiColors::COLOR_WHITE);
    canvas.setTextSize(2);
    canvas.drawString("TOOLS", 80, Viewport::DOCK_TOOLS_Y + 22);
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString("SYSTEM BOX", 80, Viewport::DOCK_TOOLS_Y + 56);
}

DockTouchAction LeftDock::handleTouchTap(int16_t x, int16_t y) {
    if (x < 0 || x >= Viewport::DOCK_W || y < 0 || y >= Viewport::DOCK_H) {
        return DOCK_ACTION_NONE;
    }
    if (y < Viewport::DOCK_TIME_Y)    return DOCK_ACTION_HOME;
    if (y < Viewport::DOCK_GNSS_Y)    return DOCK_ACTION_TIME;
    if (y < Viewport::DOCK_WIFI_Y)    return DOCK_ACTION_GNSS;
    if (y < Viewport::DOCK_BATTERY_Y) return DOCK_ACTION_WIFI;
    if (y < Viewport::DOCK_WEATHER_Y) return DOCK_ACTION_BATTERY;
    if (y < Viewport::DOCK_TOOLS_Y)   return DOCK_ACTION_WEATHER;
    return DOCK_ACTION_TOOLS;
}
