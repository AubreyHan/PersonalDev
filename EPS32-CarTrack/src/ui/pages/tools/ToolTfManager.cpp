#include "ui/pages/tools/ToolTfManager.h"
#include "service/TrackLogger.h"
#include "service/NetworkManager.h"
#include "config.h"

void ToolTfManager::draw(M5Canvas& canvas, const SystemContext& ctx) {
    if (!_isOpen) return;

    canvas.fillScreen(UiColors::COLOR_BG);
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_SECONDARY, UiColors::COLOR_BG);
    canvas.drawString("[TF] 卡存储管理", 8, 28);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
    canvas.drawRightString("[上滑返回]", 312, 28);

    // 1. Format Button (X: 8~312, Y: 52~86)
    canvas.fillRoundRect(8, 52, 304, 34, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(8, 52, 304, 34, 4, UiColors::COLOR_DANGER);
    canvas.setTextColor(UiColors::COLOR_DANGER, UiColors::COLOR_PANEL_BG);
    canvas.drawString("格式化 TF 卡 (Format FAT32)", 16, 62);

    // 2. Wi-Fi Hotspot Download Button (X: 8~312, Y: 94~128)
    bool isHotspot = WifiNetworkManager::instance().isApActive();
    canvas.fillRoundRect(8, 94, 304, 34, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(8, 94, 304, 34, 4, isHotspot ? UiColors::COLOR_WARN : UiColors::COLOR_PRIMARY);
    canvas.setTextColor(isHotspot ? UiColors::COLOR_WARN : UiColors::COLOR_PRIMARY, UiColors::COLOR_PANEL_BG);
    canvas.drawString(isHotspot ? "关闭 Wi-Fi 轨迹下载热点" : "开启专属 SoftAP 无线导出 (M5Track-Share)", 16, 104);

    // 3. Status text
    canvas.setTextColor(UiColors::COLOR_WHITE, UiColors::COLOR_BG);
    canvas.drawString("存储状态: 挂载正常 (/tracks/ 目录)", 8, 142);
    char logInfo[48];
    snprintf(logInfo, sizeof(logInfo), "当前轨迹: %s (%d 点)", 
             ctx.isLogging ? "正在记录" : "未启动", ctx.recordedPoints);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
    canvas.drawString(logInfo, 8, 160);

    canvas.drawString("提示: 手机连接热点访问 http://192.168.4.1 下载", 8, 185);
}

void ToolTfManager::handleTap(int16_t x, int16_t y) {
    if (!_isOpen) return;

    // 1. Format button
    if (x >= 8 && x <= 312 && y >= 52 && y <= 86) {
        TrackLogger::instance().formatSd();
        return;
    }

    // 2. Hotspot toggle button
    if (x >= 8 && x <= 312 && y >= 94 && y <= 128) {
        if (WifiNetworkManager::instance().isApActive()) {
            WifiNetworkManager::instance().stopHotspot();
        } else {
            WifiNetworkManager::instance().startShareHotspot();
        }
        return;
    }
}
