#include "ui/pages/PageWifiConfig.h"
#include "service/NetworkManager.h"

void PageWifiConfig::draw(M5Canvas& canvas, const SystemContext& ctx) {
    canvas.fillScreen(UiColors::COLOR_BG);

    canvas.setTextColor(UiColors::COLOR_SECONDARY, UiColors::COLOR_BG);
    canvas.setTextSize(2);
    canvas.drawCenterString("Wi-Fi 配网模式", 160, 20);

    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_WHITE, UiColors::COLOR_BG);
    canvas.drawCenterString("1. 手机连接热点:", 160, 55);

    canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_BG);
    canvas.setTextSize(2);
    canvas.drawCenterString("M5Track-Setup", 160, 75);

    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_WHITE, UiColors::COLOR_BG);
    canvas.drawCenterString("2. 打开浏览器访问:", 160, 110);

    canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_BG);
    canvas.setTextSize(2);
    canvas.drawCenterString("http://192.168.4.1", 160, 130);

    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
    canvas.drawCenterString("后台 GPS 定位持续进行中...", 160, 165);

    // [Exit] button
    canvas.fillRoundRect(100, 195, 120, 28, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(100, 195, 120, 28, 4, UiColors::COLOR_WARN);
    canvas.setTextColor(UiColors::COLOR_WARN, UiColors::COLOR_PANEL_BG);
    canvas.drawCenterString("[ 保存并退出 ]", 160, 202);
}

void PageWifiConfig::handleTap(int16_t x, int16_t y) {
    if (x >= 90 && x <= 230 && y >= 190 && y <= 235) {
        NetworkManager::instance().stopHotspot();

        SystemState& state = SystemState::instance();
        state.lock();
        SystemContext& ctx = state.getContextUnsafe();
        if (ctx.gnss.isValid && ctx.gnss.fixType >= FIX_3D) {
            ctx.currentState = STATE_MAIN_APP;
        } else {
            ctx.currentState = STATE_BOOT_INIT;
        }
        state.unlock();
    }
}
