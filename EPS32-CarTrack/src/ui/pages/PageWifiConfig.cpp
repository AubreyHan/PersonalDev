#include "ui/pages/PageWifiConfig.h"
#include "service/NetworkManager.h"

void PageWifiConfig::render(M5Canvas& canvas, const SystemContext& ctx) {
    beginViewport(canvas, true); // Full 1280x720 QR Provisioning View

    canvas.fillRect(0, 0, 1280, 64, UiColors::COLOR_PANEL_BG);
    canvas.drawFastHLine(0, 64, 1280, UiColors::COLOR_BORDER);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.setTextSize(2);
    canvas.drawString("  Wi-Fi Smart QR Code Provisioning (Hotspot: M5Track-Setup)", 20, 32);

    // Exit Button on Top-Right
    canvas.fillRoundRect(1080, 10, 176, 44, 8, UiColors::COLOR_DANGER);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString("Exit / Back", 1168, 32);

    // Step 1 Card: Scan QR to Auto-Connect Wi-Fi AP "M5Track-Setup"
    canvas.fillRoundRect(60, 95, 540, 560, 12, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(60, 95, 540, 560, 12, UiColors::COLOR_PRIMARY);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.setTextSize(2);
    canvas.drawString("STEP 1: Scan to Connect Wi-Fi", 330, 120);
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString("SSID: M5Track-Setup (No Password)", 330, 155);

    // Draw QR Code 1: Standard WIFI auto-join URI
    canvas.fillRect(180, 190, 300, 300, UiColors::COLOR_WHITE);
    canvas.qrcode("WIFI:T:nopass;S:M5Track-Setup;;", 195, 205, 270, 6);

    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.setTextSize(2);
    canvas.drawString("Captive Portal Auto-Opens!", 330, 520);
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString("Phone will automatically pop up setup page", 330, 555);

    // Step 2 Card: Scan QR to Open Setup URL directly (http://192.168.4.1/setup)
    canvas.fillRoundRect(680, 95, 540, 560, 12, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(680, 95, 540, 560, 12, UiColors::COLOR_SECONDARY);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.setTextSize(2);
    canvas.drawString("STEP 2: Or Scan to Open Web UI", 950, 120);
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString("URL: http://192.168.4.1/setup", 950, 155);

    // Draw QR Code 2: Direct URL QR
    canvas.fillRect(800, 190, 300, 300, UiColors::COLOR_WHITE);
    canvas.qrcode("http://192.168.4.1/setup", 815, 205, 270, 6);

    canvas.setTextSize(2);
    canvas.setTextColor(ctx.wifi.isConnected ? UiColors::COLOR_PRIMARY : UiColors::COLOR_WARN);
    char stBuf[64];
    if (ctx.wifi.isConnected) {
        snprintf(stBuf, sizeof(stBuf), "Connected: %s (%s)", ctx.wifi.ssid, ctx.wifi.ipAddr);
    } else {
        snprintf(stBuf, sizeof(stBuf), "Waiting for phone configuration...");
    }
    canvas.drawString(stBuf, 950, 530);

    endViewport(canvas);
}

bool PageWifiConfig::onTouchTap(int16_t x, int16_t y, SystemContext& ctx) {
    // Exit button (X: 1080..1256, Y: 10..54)
    if (x >= 1080 && x <= 1256 && y >= 10 && y <= 64) {
        AppNetworkManager::instance().stopPortals();
        ctx.currentPage = (ctx.gnss.isValid && ctx.gnss.numSats >= 4) ? PAGE_1_DASHBOARD : PAGE_INIT;
        return true;
    }
    return false;
}
