#include "ui/components/StatusBar.h"
#include "service/TimeService.h"
#include "config.h"

void StatusBar::draw(M5Canvas& canvas, const SystemContext& ctx) {
    // 1. Top bar background
    canvas.fillRect(0, 0, 320, 24, UiColors::COLOR_PANEL_BG);
    canvas.drawFastHLine(0, 23, 320, UiColors::COLOR_BORDER);

    // 2. Zone 1: Left Time (X: 0~95, tap jumps to Page 7)
    // Use setTextSize(2) for large, clear automotive glance
    char timeStr[16];
    TimeService::instance().getFormattedLocalTime(timeStr, sizeof(timeStr), 8);
    // Take HH:mm:ss
    canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_PANEL_BG);
    canvas.setTextSize(2);
    canvas.drawString(timeStr, 4, 4);

    // 3. Zone 2: Center Status Badges (X: 104~225)
    canvas.setTextSize(1);
    // 3.1 GNSS Badge
    if (ctx.gnss.fixType >= FIX_3D) {
        canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_PANEL_BG);
        char satStr[16];
        snprintf(satStr, sizeof(satStr), "3D:%d", ctx.gnss.numSats);
        canvas.drawString(satStr, 108, 7);
    } else if (ctx.gnss.isDeadReckoning) {
        canvas.setTextColor(UiColors::COLOR_WARN, UiColors::COLOR_PANEL_BG);
        canvas.drawString("[DR]", 108, 7);
    } else {
        canvas.setTextColor(UiColors::COLOR_WARN, UiColors::COLOR_PANEL_BG);
        canvas.drawString("GPS..", 108, 7);
    }

    // 3.2 Wi-Fi Icon
    int wifiX = 148;
    if (ctx.isWifiConnected) {
        canvas.setTextColor(UiColors::COLOR_BLUE, UiColors::COLOR_PANEL_BG);
        canvas.drawString("WiFi", wifiX, 7);
    } else {
        canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
        canvas.drawString("--", wifiX, 7);
    }

    // 3.3 Battery Status (Adaptive)
    int batX = 182;
    canvas.drawRect(batX, 5, 18, 12, UiColors::COLOR_MUTED);
    canvas.fillRect(batX + 18, 8, 2, 6, UiColors::COLOR_MUTED);

    if (ctx.batteryMode == BATTERY_MODE_BASE_ATTACHED) {
        int fillW = (ctx.batteryPercent * 14) / 100;
        uint16_t batColor = (ctx.batteryPercent < 20) ? UiColors::COLOR_DANGER : UiColors::COLOR_PRIMARY;
        canvas.fillRect(batX + 2, 7, fillW, 8, batColor);

        char batTxt[8];
        snprintf(batTxt, sizeof(batTxt), "%d%%", ctx.batteryPercent);
        canvas.setTextColor(ctx.isCharging ? UiColors::COLOR_SECONDARY : UiColors::COLOR_WHITE, UiColors::COLOR_PANEL_BG);
        canvas.drawString(batTxt, batX + 23, 7);
    } else {
        canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
        canvas.drawString("USB", batX + 23, 7);
    }

    // 4. Zone 3: Right Temp & Humidity (X: 226~319, tap jumps to Page 6)
    char envStr[16];
    if (ctx.weather.hasData) {
        snprintf(envStr, sizeof(envStr), "%.1f° %d%%", ctx.weather.temperatureC, ctx.weather.relativeHumidity);
    } else {
        snprintf(envStr, sizeof(envStr), "--° --%%");
    }
    canvas.setTextColor(UiColors::COLOR_WHITE, UiColors::COLOR_PANEL_BG);
    canvas.drawRightString(envStr, 314, 7);
}

bool StatusBar::checkTap(int16_t x, int16_t y, uint8_t& targetPage) {
    if (y > Viewport::TOP_BAR_HIT_H) return false;

    if (x <= Viewport::HITBOX_TIME_W) {
        targetPage = 6; // Page 7 World Clock
        return true;
    }
    if (x >= Viewport::HITBOX_WEATHER_X) {
        targetPage = 5; // Page 6 Weather Forecast
        return true;
    }
    return false;
}
