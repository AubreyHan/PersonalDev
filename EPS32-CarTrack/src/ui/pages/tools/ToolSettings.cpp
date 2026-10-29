#include "ui/pages/tools/ToolSettings.h"
#include "driver/PowerDriver.h"
#include "config.h"

void ToolSettings::draw(M5Canvas& canvas, const SystemContext& ctx) {
    if (!_isOpen) return;

    canvas.fillScreen(UiColors::COLOR_BG);
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_SECONDARY, UiColors::COLOR_BG);
    canvas.drawString("[SYS] 系统偏好与电量计量", 8, 28);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
    canvas.drawRightString("[上滑返回]", 312, 28);

    // 1. INA226 Power Meter Card
    canvas.fillRoundRect(8, 46, 304, 65, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(8, 46, 304, 65, 4, UiColors::COLOR_BORDER);

    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
    canvas.drawString("Base Dual 16340 库仑计计量 (INA226 0x45)", 16, 52);

    char vbusTxt[64];
    if (ctx.batteryMode == BATTERY_MODE_BASE_ATTACHED) {
        snprintf(vbusTxt, sizeof(vbusTxt), "电压: %.2fV | 电流: %+.1fmA | 功率: %.0fmW", 
                 ctx.powerMonitor.busVoltageV, ctx.powerMonitor.currentMa, ctx.powerMonitor.powerMw);
        canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_PANEL_BG);
        canvas.drawString(vbusTxt, 16, 70);

        char socTxt[64];
        snprintf(socTxt, sizeof(socTxt), "剩余容量: %.0f/1400mAh (%d%%) | 预计续航: %d分", 
                 ctx.powerMonitor.remainingMah, ctx.batteryPercent, ctx.powerMonitor.remainingMinutes);
        canvas.setTextColor(UiColors::COLOR_WHITE, UiColors::COLOR_PANEL_BG);
        canvas.drawString(socTxt, 16, 88);
    } else {
        snprintf(vbusTxt, sizeof(vbusTxt), "供电: 车载 USB 5V 直供 (无底座)");
        canvas.setTextColor(UiColors::COLOR_WARN, UiColors::COLOR_PANEL_BG);
        canvas.drawString(vbusTxt, 16, 70);

        canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
        canvas.drawString("若扣上双 16340 底座，系统将免重新烧录自动点亮识别", 16, 88);
    }

    // 2. Backlight Settings
    canvas.fillRoundRect(8, 120, 304, 40, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(8, 120, 304, 40, 4, UiColors::COLOR_BORDER);
    canvas.setTextColor(UiColors::COLOR_WHITE, UiColors::COLOR_PANEL_BG);
    canvas.drawString("屏幕背光亮度: [降低]  [恢复180]  [最高255]", 16, 134);

    // 3. Firmware info
    canvas.fillRoundRect(8, 168, 304, 45, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(8, 168, 304, 45, 4, UiColors::COLOR_BORDER);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
    canvas.drawString("固件版本: v1.0.0 (M5Track-M9N) | PSRAM: 8MB", 16, 176);

    char heapTxt[64];
    snprintf(heapTxt, sizeof(heapTxt), "Free Heap: %d KB | Free PSRAM: %d KB", 
             (int)(ESP.getFreeHeap() / 1024), (int)(ESP.getFreePsram() / 1024));
    canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_PANEL_BG);
    canvas.drawString(heapTxt, 16, 194);
}

void ToolSettings::handleTap(int16_t x, int16_t y) {
    if (!_isOpen) return;

    // Backlight buttons
    if (y >= 120 && y <= 160) {
        if (x >= 120 && x <= 165) {
            PowerDriver::instance().setBacklight(80);
        } else if (x >= 170 && x <= 235) {
            PowerDriver::instance().setBacklight(180);
        } else if (x >= 240 && x <= 305) {
            PowerDriver::instance().setBacklight(255);
        }
    }
}
