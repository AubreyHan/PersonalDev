#include "ui/components/StatusBar.h"
#include "service/TimeService.h"
#include "config.h"

void StatusBar::draw(M5Canvas& canvas, const SystemContext& ctx) {
    // 1. Top bar background
    canvas.fillRect(0, 0, 320, 24, UiColors::COLOR_PANEL_BG);
    canvas.drawFastHLine(0, 23, 320, UiColors::COLOR_BORDER);

    // 2. Zone 1: Left Time (X: 0~95, tap jumps to Page 7)
    char timeStr[16];
    TimeService::instance().getFormattedLocalTime(timeStr, sizeof(timeStr), 8);
    canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_PANEL_BG);
    canvas.setTextSize(1);
    canvas.drawString(timeStr, 8, 6);

    // 3. Zone 2: Center Status Badges (X: 96~225)
    // 3.1 GNSS Badge
    if (ctx.gnss.fixType >= FIX_3D) {
        canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_PANEL_BG);
        canvas.printf("  3D:%2d", ctx.gnss.numSats);
    } else if (ctx.gnss.isDeadReckoning) {
        canvas.setTextColor(UiColors::COLOR_WARN, UiColors::COLOR_PANEL_BG);
        canvas.drawString("  [DR]", 98, 6);
    } else {
        canvas.setTextColor(UiColors::COLOR_WARN, UiColors::COLOR_PANEL_BG);
        canvas.drawString("  GPS..", 98, 6);
    }

    // 3.2 Wi-Fi Icon
    int wifiX = 148;
    if (ctx.isWifiConnected) {
        canvas.setTextColor(UiColors::COLOR_BLUE, UiColors::COLOR_PANEL_BG);
        canvas.drawString("WiFi", wifiX, 6);
    } else {
        canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
        canvas.drawString("--", wifiX, 6);
    }

    // 3.3 Battery Status (Adaptive)
    int batX = 184;
    canvas.drawRect(batX, 5, 20, 12, UiColors::COLOR_MUTED);
    canvas.fillRect(batX + 20, 8, 2, 6, UiColors::COLOR_MUTED); // Battery terminal

    if (ctx.batteryMode == BATTERY_MODE_BASE_ATTACHED) {
        // Real battery level
        int fillW = (ctx.batteryPercent * 16) / 100;
        uint16_t batColor = (ctx.batteryPercent < 20) ? UiColors::COLOR_DANGER : UiColors::COLOR_PRIMARY;
        canvas.fillRect(batX + 2, 7, fillW, 8, batColor);

        char batTxt[8];
        snprintf(batTxt, sizeof(batTxt), "%d%%", ctx.batteryPercent);
        canvas.setTextColor(ctx.isCharging ? UiColors::COLOR_SECONDARY : UiColors::COLOR_WHITE, UiColors::COLOR_PANEL_BG);
        canvas.drawString(batTxt, batX + 25, 6);
    } else {
        // USB Direct Power (No base) -> Empty frame
        canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
        canvas.drawString("USB", batX + 25, 6);
    }

    // 4. Zone 3: Right Temp & Humidity (X: 226~319, tap jumps to Page 6)
    char envStr[16];
    if (ctx.weather.hasData) {
        snprintf(envStr, sizeof(envStr), "%.1f C %d%%", ctx.weather.temperatureC, ctx.weather.relativeHumidity);
    } else {
        snprintf(envStr, sizeof(envStr), "-- C --%%");
    }
    canvas.setTextColor(UiColors::COLOR_WHITE, UiColors::COLOR_PANEL_BG);
    canvas.drawRightString(envStr, 312, 6);
}

bool StatusBar::checkTap(int16_t x, int16_t y, uint8_t& targetPage) {
    if (y > Viewport::TOP_BAR_HIT_H) return false;

    if (x <= Viewport::HITBOX_TIME_W) {
        // Tap on time -> Jump to Page 7 (World Clock)
        targetPage = 6; // index 6 is Page 7
        return true;
    }
    if (x >= Viewport::HITBOX_WEATHER_X) {
        // Tap on weather -> Jump to Page 6 (Weather Forecast)
        targetPage = 5; // index 5 is Page 6
        return true;
    }
    return false;
}
