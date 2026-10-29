#include "ui/pages/tools/ToolWifi.h"
#include "service/NetworkManager.h"
#include "config.h"

void ToolWifi::draw(M5Canvas& canvas, const SystemContext& ctx) {
    if (!_isOpen) return;

    canvas.fillScreen(UiColors::COLOR_BG);
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_SECONDARY, UiColors::COLOR_BG);
    canvas.drawString("📶 Wi-Fi 网络管理", 8, 28);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
    canvas.drawRightString("[上滑返回]", 312, 28);

    // 1. Current Network Info Card
    canvas.fillRoundRect(8, 48, 304, 50, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(8, 48, 304, 50, 4, UiColors::COLOR_BORDER);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
    canvas.drawString("当前连接状态", 16, 54);

    char connBuf[64];
    if (ctx.isWifiConnected) {
        snprintf(connBuf, sizeof(connBuf), "已连接: %s (%d dBm)", ctx.wifiSsid.c_str(), ctx.wifiRssi);
        canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_PANEL_BG);
    } else {
        snprintf(connBuf, sizeof(connBuf), "未连接 Wi-Fi");
        canvas.setTextColor(UiColors::COLOR_WARN, UiColors::COLOR_PANEL_BG);
    }
    canvas.drawString(connBuf, 16, 72);

    // 2. Add Network Button (X: 8~312, Y: 108~142)
    canvas.fillRoundRect(8, 108, 304, 34, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(8, 108, 304, 34, 4, UiColors::COLOR_PRIMARY);
    canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_PANEL_BG);
    canvas.drawString("[+ 开启 Web 配网添加新 Wi-Fi]", 16, 118);

    // Hint
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
    canvas.drawString("支持自动记忆并漫游最多 5 组车机/手机热点", 8, 160);
    canvas.drawString("可通过配网页查看并删除指定已知网络", 8, 178);
}

void ToolWifi::handleTap(int16_t x, int16_t y) {
    if (!_isOpen) return;

    // Add Network button
    if (x >= 8 && x <= 312 && y >= 108 && y <= 142) {
        NetworkManager::instance().startConfigPortal();
        close();
    }
}
