#include "ui/pages/PageInit.h"
#include "service/NetworkManager.h"
#include "service/TrackLogger.h"
#include "driver/GnssDriver.h"
#include <math.h>

extern GnssDriver g_gnssDriver;

void PageInit::render(M5Canvas& canvas, const SystemContext& ctx) {
    beginViewport(canvas, true); // Full 1280x720 boot diagnostic screen

    // Top Status Banner
    canvas.fillRect(0, 0, 1280, 56, UiColors::COLOR_PANEL_BG);
    canvas.drawFastHLine(0, 56, 1280, UiColors::COLOR_BORDER);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.setTextSize(2);
    canvas.drawString("  M5Track-Tab5 | System Hardware Self-Test & GNSS Acquisition", 16, 28);

    canvas.setTextDatum(middle_right);
    uint16_t batCol = ctx.power.hasExternalPower ? PowerColorConfig::COLOR_EXTERNAL_POWER_GREEN : PowerColorConfig::COLOR_BATTERY_ONLY_RED;
    canvas.setTextColor(batCol);
    char pwrStr[48];
    snprintf(pwrStr, sizeof(pwrStr), "%s %.0f%% (%.2fV)  ", ctx.power.hasExternalPower ? "EXT PWR" : "NP-F550", ctx.power.batterySocPercent, ctx.power.busVoltageV);
    canvas.drawString(pwrStr, 1264, 28);

    // Left Column: Hardware Peripherals Self-Test (X: 24..410, Y: 80..620)
    canvas.fillRoundRect(24, 80, 386, 540, 10, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(24, 80, 386, 540, 10, UiColors::COLOR_BORDER);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.setTextSize(2);
    canvas.drawString("1. Hardware Self-Test", 44, 100);

    auto drawCheckRow = [&](int y, const char* label, bool ok, const char* okText, const char* failText) {
        canvas.setTextSize(2);
        canvas.setTextColor(UiColors::COLOR_WHITE);
        canvas.drawString(label, 44, y);
        canvas.setTextColor(ok ? UiColors::COLOR_PRIMARY : UiColors::COLOR_WARN);
        canvas.drawString(ok ? okText : failText, 220, y);
    };

    drawCheckRow(155, "TF Card:",   ctx.storage.isMounted, "MOUNTED (OK)", "STANDBY");
    drawCheckRow(205, "NEO-M9N:",   ctx.gnss.moduleOnline, "UART READY",   "STANDBY");
    drawCheckRow(255, "Dual IMU:",  ctx.imu.masterOnline || ctx.imu.slaveOnline,
                 ctx.imu.isDualFused ? "0x68+0x69 FUSED" : "ONLINE", "STANDBY");
    drawCheckRow(305, "BMP280:",    ctx.baro.isAvailable,  "0x76 READY",   "STANDBY");
    drawCheckRow(355, "RX8130 RTC:",ctx.rtcOnline,         "0x32 READY",   "INTERNAL");
    drawCheckRow(405, "INA226:",    ctx.power.isAvailable, "0x40 READY",   "ESTIMATED");

    // Hot-Start Re-Acquisition Button (Solves indoor -> outdoor slow lock without reboot)
    canvas.fillRoundRect(44, 465, 346, 58, 8, 0x0208);
    canvas.drawRoundRect(44, 465, 346, 58, 8, UiColors::COLOR_PRIMARY);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.setTextSize(2);
    canvas.drawString("Hot-Restart GNSS Lock", 217, 494);

    // Format TF Button or Enter Dashboard Demo Button
    canvas.fillRoundRect(44, 540, 346, 58, 8, UiColors::COLOR_CARD_BG);
    canvas.drawRoundRect(44, 540, 346, 58, 8, UiColors::COLOR_SECONDARY);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.drawString("Enter Dashboard >>", 217, 569);

    // Center Column: Dynamic Radar Satellite Acquisition (X: 430..850, Y: 80..620)
    canvas.fillRoundRect(430, 80, 420, 540, 10, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(430, 80, 420, 540, 10, UiColors::COLOR_BORDER);
    int cx = 640, cy = 300, r = 150;
    canvas.drawCircle(cx, cy, r, UiColors::COLOR_PRIMARY);
    canvas.drawCircle(cx, cy, r * 2 / 3, UiColors::COLOR_BORDER);
    canvas.drawCircle(cx, cy, r / 3, UiColors::COLOR_BORDER);
    canvas.drawFastHLine(cx - r, cy, r * 2, UiColors::COLOR_BORDER);
    canvas.drawFastVLine(cx, cy - r, r * 2, UiColors::COLOR_BORDER);

    // Rotating radar sweep line
    float angleRad = ((millis() / 20) % 360) * 0.0174532925f;
    int sx = cx + (int)(cosf(angleRad) * r);
    int sy = cy + (int)(sinf(angleRad) * r);
    canvas.drawLine(cx, cy, sx, sy, UiColors::COLOR_PRIMARY);

    // Render active satellite constellation positions on radar
    for (uint8_t i = 0; i < ctx.gnss.activeSatCount && i < 32; i++) {
        const auto& sat = ctx.gnss.sats[i];
        if (sat.elev < 0) continue;
        // Project elevation (90 at center, 0 at edge) and azimuth
        float satR = ((90.0f - (float)sat.elev) / 90.0f) * (r - 12);
        float satRad = ((float)sat.azim - 90.0f) * 0.0174532925f;
        int px = cx + (int)(cosf(satRad) * satR);
        int py = cy + (int)(sinf(satRad) * satR);

        // Constellation color: BDS = Cyan-Green, GPS = Racing Yellow, GLO = Ice Blue, GAL = Amber
        uint16_t sColor = UiColors::COLOR_PRIMARY;
        if (sat.gnssId == 0) sColor = UiColors::COLOR_SECONDARY;      // GPS
        else if (sat.gnssId == 6) sColor = UiColors::COLOR_BLUE;      // GLONASS
        else if (sat.gnssId == 2) sColor = UiColors::COLOR_WARN;      // Galileo
        else if (sat.gnssId == 3) sColor = UiColors::COLOR_PRIMARY;   // BeiDou

        if (sat.used) {
            canvas.fillCircle(px, py, 6, sColor);
            canvas.drawCircle(px, py, 8, UiColors::COLOR_WHITE);
        } else {
            canvas.drawCircle(px, py, 5, sColor);
        }
    }

    canvas.setTextDatum(top_center);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.setTextSize(2);
    canvas.drawString("GNSS SATELLITE RADAR", 640, 100);

    char satSummary[64];
    snprintf(satSummary, sizeof(satSummary), "BDS:%u | GPS:%u | GLO:%u | GAL:%u",
             ctx.gnss.numBdsSats, ctx.gnss.numGpsSats, ctx.gnss.numGloSats, ctx.gnss.numGalSats);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.drawString(satSummary, 640, 480);

    char hdopStr[64];
    snprintf(hdopStr, sizeof(hdopStr), "Tracked: %u Sats (HDOP: %.1f)", ctx.gnss.numSats, ctx.gnss.hdop);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.drawString(hdopStr, 640, 520);

    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString("Auto-Entering Dashboard on 3D Fix (>=4 Sats)", 640, 565);

    // Right Column: Wi-Fi & NTP Status (X: 870..1256, Y: 80..620)
    canvas.fillRoundRect(870, 80, 386, 540, 10, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(870, 80, 386, 540, 10, UiColors::COLOR_BORDER);
    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.drawString("2. Network & NTP Sync", 890, 100);

    canvas.setTextColor(ctx.wifi.isConnected ? UiColors::COLOR_PRIMARY : UiColors::COLOR_WARN);
    canvas.drawString(ctx.wifi.isConnected ? "Wi-Fi: CONNECTED" : "Wi-Fi: DISCONNECTED", 890, 160);

    canvas.setTextColor(UiColors::COLOR_WHITE);
    char ssidStr[48];
    snprintf(ssidStr, sizeof(ssidStr), "SSID: %s", ctx.wifi.isConnected ? ctx.wifi.ssid : "None");
    canvas.drawString(ssidStr, 890, 210);

    char ipStr[48];
    snprintf(ipStr, sizeof(ipStr), "IP: %s", ctx.wifi.isConnected ? ctx.wifi.ipAddr : "0.0.0.0");
    canvas.drawString(ipStr, 890, 260);

    canvas.setTextColor(UiColors::COLOR_BLUE);
    canvas.drawString(ctx.wifi.isConnected && !ctx.gnss.isValid ? "NTP Time Sync: ACTIVE" : "RTC Time Engine: READY", 890, 315);

    // Wi-Fi QR Provisioning Button
    canvas.fillRoundRect(890, 465, 346, 70, 8, 0x0208);
    canvas.drawRoundRect(890, 465, 346, 70, 8, UiColors::COLOR_PRIMARY);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.drawString("Wi-Fi QR Code Setup", 1063, 500);

    // Bottom Guide Footer
    canvas.setTextDatum(middle_center);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString("Drive to open sky for auto 3D Fix, or tap [Enter Dashboard >>] for indoor preview", 640, 675);

    endViewport(canvas);
}

bool PageInit::onTouchTap(int16_t x, int16_t y, SystemContext& ctx) {
    // 1. Hot-Restart GNSS Lock button (X: 44..390, Y: 465..523)
    if (x >= 44 && x <= 390 && y >= 465 && y <= 523) {
        g_gnssDriver.triggerHotStartReacquisition();
        return true;
    }
    // 2. Enter Dashboard button (X: 44..390, Y: 540..598)
    if (x >= 44 && x <= 390 && y >= 540 && y <= 598) {
        ctx.currentPage = PAGE_1_DASHBOARD;
        return true;
    }
    // 3. Wi-Fi QR Code Setup button (X: 890..1236, Y: 465..535)
    if (x >= 890 && x <= 1236 && y >= 465 && y <= 535) {
        AppNetworkManager::instance().startSetupPortal();
        ctx.currentPage = PAGE_WIFI_CONFIG;
        return true;
    }
    return false;
}
