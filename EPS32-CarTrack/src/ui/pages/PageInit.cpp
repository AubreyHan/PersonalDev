#include "ui/pages/PageInit.h"
#include "service/NetworkManager.h"
#include "service/TrackLogger.h"
#include <math.h>

void PageInit::init() {
    _radarAngle = 0;
}

void PageInit::draw(M5Canvas& canvas, const SystemContext& ctx) {
    canvas.fillScreen(UiColors::COLOR_BG);

    // Title
    canvas.setTextColor(UiColors::COLOR_SECONDARY, UiColors::COLOR_BG);
    canvas.setTextSize(2);
    canvas.drawCenterString("M5Track-M9N", 160, 16);

    // Subtitle
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_WHITE, UiColors::COLOR_BG);
    canvas.drawCenterString("Vehicle High-Precision Navigator", 160, 38);

    // Draw Radar Circle
    int centerX = 160;
    int centerY = 110;
    int radius = 45;
    canvas.drawCircle(centerX, centerY, radius, UiColors::COLOR_BORDER);
    canvas.drawCircle(centerX, centerY, radius / 2, UiColors::COLOR_BORDER);
    canvas.drawFastHLine(centerX - radius, centerY, radius * 2, UiColors::COLOR_BORDER);
    canvas.drawFastVLine(centerX, centerY - radius, radius * 2, UiColors::COLOR_BORDER);

    // Rotating Radar Beam
    _radarAngle += 0.08f;
    if (_radarAngle >= 6.28318f) _radarAngle = 0;
    int beamX = centerX + (int)(radius * cos(_radarAngle));
    int beamY = centerY + (int)(radius * sin(_radarAngle));
    canvas.drawLine(centerX, centerY, beamX, beamY, UiColors::COLOR_PRIMARY);

    // Satellites Status
    char satBuf[32];
    snprintf(satBuf, sizeof(satBuf), "SATS: %2d  %s", 
             ctx.gnss.numSats, 
             (ctx.gnss.fixType >= 3) ? "3D FIX" : "SEARCH");
    canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_BG);
    canvas.setTextSize(2);
    canvas.drawCenterString(satBuf, 160, 160);

    // Hardware Icons Check
    canvas.setTextSize(1);
    char hwBuf[64];
    snprintf(hwBuf, sizeof(hwBuf), "HDOP: %.1f | SD: %s | IMU: OK", 
             ctx.gnss.hdop,
             ctx.isSdMounted ? "Mounted" : "No Card");
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
    canvas.drawCenterString(hwBuf, 160, 182);

    // [Wi-Fi Settings] Button (X: 100~220, Y: 202~230)
    canvas.fillRoundRect(100, 202, 120, 26, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(100, 202, 120, 26, 4, UiColors::COLOR_PRIMARY);
    canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_PANEL_BG);
    canvas.drawCenterString("[ Wi-Fi 配网 ]", 160, 208);

    // If SD format is required, draw popup dialog
    if (ctx.sdFormatRequired) {
        canvas.fillRoundRect(40, 60, 240, 120, 6, UiColors::COLOR_PANEL_BG);
        canvas.drawRoundRect(40, 60, 240, 120, 6, UiColors::COLOR_WARN);
        canvas.setTextColor(UiColors::COLOR_WARN, UiColors::COLOR_PANEL_BG);
        canvas.drawCenterString("TF卡格式不兼容", 160, 75);
        canvas.setTextColor(UiColors::COLOR_WHITE, UiColors::COLOR_PANEL_BG);
        canvas.drawCenterString("是否立即格式化为标准FAT32？", 160, 95);

        // [Cancel] button
        canvas.drawRoundRect(55, 130, 90, 30, 4, UiColors::COLOR_MUTED);
        canvas.drawCenterString("取消", 100, 138);

        // [Confirm] button
        canvas.fillRoundRect(175, 130, 90, 30, 4, UiColors::COLOR_DANGER);
        canvas.drawCenterString("确认格式化", 220, 138);
    }
}

void PageInit::handleTap(int16_t x, int16_t y) {
    SystemState& state = SystemState::instance();
    state.lock();
    bool formatReq = state.getContextUnsafe().sdFormatRequired;
    state.unlock();

    if (formatReq) {
        // Dialog buttons
        if (x >= 55 && x <= 145 && y >= 130 && y <= 160) {
            // Cancel
            state.lock();
            state.getContextUnsafe().sdFormatRequired = false;
            state.unlock();
            return;
        }
        if (x >= 175 && x <= 265 && y >= 130 && y <= 160) {
            // Confirm format
            TrackLogger::instance().formatSd();
            return;
        }
    }

    // [Wi-Fi Settings] Button clicked
    if (x >= 100 && x <= 220 && y >= 200 && y <= 235) {
        WifiNetworkManager::instance().startConfigPortal();
    }
}
