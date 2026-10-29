#include "ui/pages/PageToolsGrid.h"
#include "config.h"

void PageToolsGrid::drawToolCard(M5Canvas& canvas, int x, int y, int w, int h, const char* title, const char* sub1, const char* sub2, uint16_t color) {
    canvas.fillRoundRect(x, y, w, h, 6, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(x, y, w, h, 6, UiColors::COLOR_BORDER);

    canvas.setTextSize(1);
    canvas.setTextColor(color, UiColors::COLOR_PANEL_BG);
    canvas.drawString(title, x + 8, y + 10);

    canvas.setTextColor(UiColors::COLOR_WHITE, UiColors::COLOR_PANEL_BG);
    canvas.drawString(sub1, x + 8, y + 32);

    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
    canvas.drawString(sub2, x + 8, y + 54);
}

void PageToolsGrid::draw(M5Canvas& canvas, const SystemContext& ctx) {
    // Check if any subpage is currently active
    if (_tfTool.isOpen()) { _tfTool.draw(canvas, ctx); return; }
    if (_wifiTool.isOpen()) { _wifiTool.draw(canvas, ctx); return; }
    if (_gnssTool.isOpen()) { _gnssTool.draw(canvas, ctx); return; }
    if (_settingsTool.isOpen()) { _settingsTool.draw(canvas, ctx); return; }

    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
    canvas.drawString("SYSTEM TOOLS (上滑一键秒回主仪表盘)", 8, 28);

    // 2x2 Grid (Each card: width 148, height 84)
    // Card 1: TF Manager (Row 1 Col 1)
    drawToolCard(canvas, 8, 44, 148, 84, "💾 TF 卡存储管理", "格式化 / 导出", "点击进入管理", UiColors::COLOR_PRIMARY);

    // Card 2: Wi-Fi Manager (Row 1 Col 2)
    drawToolCard(canvas, 164, 44, 148, 84, "📶 Wi-Fi 网络管理", ctx.isWifiConnected ? "已连接网络" : "未连接", "点击配置网络", UiColors::COLOR_BLUE);

    // Card 3: GNSS Info (Row 2 Col 1)
    drawToolCard(canvas, 8, 134, 148, 84, "🛰️ GPS 卫星详情", "四星系统分布", "射频抗干扰诊断", UiColors::COLOR_SECONDARY);

    // Card 4: System Settings (Row 2 Col 2)
    drawToolCard(canvas, 164, 134, 148, 84, "⚙️ 系统与电量", "INA226 库仑计", "背光与系统信息", UiColors::COLOR_WARN);
}

void PageToolsGrid::handleTap(int16_t x, int16_t y) {
    if (_tfTool.isOpen()) { _tfTool.handleTap(x, y); return; }
    if (_wifiTool.isOpen()) { _wifiTool.handleTap(x, y); return; }
    if (_gnssTool.isOpen()) { return; }
    if (_settingsTool.isOpen()) { _settingsTool.handleTap(x, y); return; }

    // Check which 4-tile card was tapped
    if (y >= 44 && y <= 128) {
        if (x >= 8 && x <= 156) _tfTool.open();
        else if (x >= 164 && x <= 312) _wifiTool.open();
    } else if (y >= 134 && y <= 218) {
        if (x >= 8 && x <= 156) _gnssTool.open();
        else if (x >= 164 && x <= 312) _settingsTool.open();
    }
}

void PageToolsGrid::handleSwipeUp() {
    if (_tfTool.isOpen()) { _tfTool.handleSwipeUp(); return; }
    if (_wifiTool.isOpen()) { _wifiTool.handleSwipeUp(); return; }
    if (_gnssTool.isOpen()) { _gnssTool.handleSwipeUp(); return; }
    if (_settingsTool.isOpen()) { _settingsTool.handleSwipeUp(); return; }

    // If on the 4-tile grid itself, swipe up jumps directly to Page 1 Dashboard!
    SystemState& state = SystemState::instance();
    state.lock();
    state.getContextUnsafe().mainPageIdx = 0; // Page 1
    state.unlock();
}
