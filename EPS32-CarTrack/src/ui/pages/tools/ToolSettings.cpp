#include "ui/pages/tools/ToolSettings.h"
#include <M5Unified.h>

void ToolSettings::render(M5Canvas& canvas, const SystemContext& ctx) {
    beginViewport(canvas);

    canvas.fillRoundRect(185, 20, 1070, 72, 10, UiColors::COLOR_PANEL_BG);
    canvas.setTextDatum(middle_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_WARN);
    canvas.drawString("  Tool 4: Screen Brightness & INA226 Coulomb Gauge Telemetry", 205, 56);

    canvas.fillRoundRect(1080, 32, 155, 48, 8, UiColors::COLOR_DANGER);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString("Back", 1157, 56);

    // Brightness Slider Card
    canvas.fillRoundRect(185, 110, 1070, 165, 12, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(185, 110, 1070, 165, 12, UiColors::COLOR_BORDER);
    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    char brStr[64];
    snprintf(brStr, sizeof(brStr), "Screen Backlight Brightness: %u / 255 (Tap Bar to Adjust)", ctx.screenBrightness);
    canvas.drawString(brStr, 215, 130);

    int barX = 215, barY = 185, barW = 1010, barH = 54;
    int fillW = (ctx.screenBrightness * barW) / 255;
    canvas.fillRoundRect(barX, barY, barW, barH, 10, UiColors::COLOR_CARD_BG);
    canvas.fillRoundRect(barX, barY, fillW, barH, 10, UiColors::COLOR_PRIMARY);
    canvas.drawRoundRect(barX, barY, barW, barH, 10, UiColors::COLOR_BORDER);

    // INA226 & NP-F550 Power Card
    canvas.fillRoundRect(185, 295, 1070, 390, 12, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(185, 295, 1070, 390, 12, UiColors::COLOR_BORDER);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.drawString("INA226 (0x40) + NP-F550 (2S 7.4V 2000mAh) Coulomb Counter", 215, 320);

    char p1[80], p2[80], p3[80], p4[80];
    snprintf(p1, sizeof(p1), "Power Source   : %s", ctx.power.hasExternalPower ? "External USB-C / HVIN (GREEN)" : "NP-F550 Battery Only (RED)");
    snprintf(p2, sizeof(p2), "Bus Voltage    : %.3f V  |  Shunt Drop: %.2f mV (20 mOhm)", ctx.power.busVoltageV, ctx.power.shuntVoltageMv);
    snprintf(p3, sizeof(p3), "Instant Current: %+.1f mA |  Instant Power: %.1f mW", ctx.power.currentMa, ctx.power.powerMw);
    snprintf(p4, sizeof(p4), "Remaining Cap  : %.1f / 2000.0 mAh (%.1f%%) | Est: %u min",
             ctx.power.remainingMah, ctx.power.batterySocPercent, ctx.power.remainingMinutes);

    canvas.setTextColor(ctx.power.uiDisplayColor);
    canvas.drawString(p1, 215, 380);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString(p2, 215, 435);
    canvas.drawString(p3, 215, 490);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.drawString(p4, 215, 545);

    char memStr[80];
    snprintf(memStr, sizeof(memStr), "ESP32-P4 Free PSRAM: %u KB  |  Free Heap: %u KB",
             (unsigned)(ESP.getFreePsram() / 1024), (unsigned)(ESP.getFreeHeap() / 1024));
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString(memStr, 215, 610);

    endViewport(canvas);
}

bool ToolSettings::onTouchTap(int16_t x, int16_t y, SystemContext& ctx) {
    if (x >= 1080 && x <= 1235 && y >= 32 && y <= 80) {
        ctx.currentPage = PAGE_8_TOOLS;
        return true;
    }
    if (x >= 215 && x <= 1225 && y >= 175 && y <= 250) {
        int val = ((x - 215) * 255) / 1010;
        if (val < 25) val = 25;
        if (val > 255) val = 255;
        ctx.screenBrightness = (uint8_t)val;
        M5.Display.setBrightness(ctx.screenBrightness);
        return true;
    }
    return false;
}
