#include "ui/pages/tools/ToolGnss.h"
#include "config.h"

void ToolGnss::draw(M5Canvas& canvas, const SystemContext& ctx) {
    if (!_isOpen) return;

    canvas.fillScreen(UiColors::COLOR_BG);
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_SECONDARY, UiColors::COLOR_BG);
    canvas.drawString("[GPS] 卫星详情与诊断", 8, 28);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
    canvas.drawRightString("[上滑返回]", 312, 28);

    // 1. Constellations Distribution Card
    canvas.fillRoundRect(8, 46, 304, 52, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(8, 46, 304, 52, 4, UiColors::COLOR_BORDER);

    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
    canvas.drawString("四星系统并发 (NEO-M9N)", 16, 52);

    char satDist[64];
    snprintf(satDist, sizeof(satDist), "北斗:%d | GPS:%d | GLO:%d | GAL:%d (总星数:%d)", 
             ctx.gnss.numBdsSats, ctx.gnss.numGpsSats, ctx.gnss.numGloSats, ctx.gnss.numGalSats, ctx.gnss.numSats);
    canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_PANEL_BG);
    canvas.drawString(satDist, 16, 72);

    // 2. Position Coordinates & Precision
    canvas.fillRoundRect(8, 106, 304, 52, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(8, 106, 304, 52, 4, UiColors::COLOR_BORDER);

    char coordBuf[64];
    snprintf(coordBuf, sizeof(coordBuf), "Lat: %.6f° | Lon: %.6f°", ctx.gnss.latitude, ctx.gnss.longitude);
    canvas.setTextColor(UiColors::COLOR_WHITE, UiColors::COLOR_PANEL_BG);
    canvas.drawString(coordBuf, 16, 112);

    char dopBuf[64];
    snprintf(dopBuf, sizeof(dopBuf), "HDOP: %.2f | PDOP: %.2f | VDOP: %.2f", ctx.gnss.hdop, ctx.gnss.pdop, ctx.gnss.vdop);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
    canvas.drawString(dopBuf, 16, 132);

    // 3. RF Jamming & Antenna Status
    canvas.fillRoundRect(8, 166, 304, 48, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(8, 166, 304, 48, 4, UiColors::COLOR_BORDER);

    const char* jamTxt = "正常 (未检测到车内干扰)";
    uint16_t jamColor = UiColors::COLOR_PRIMARY;
    if (ctx.gnss.jammingState == 1) { jamTxt = "轻微射频干扰"; jamColor = UiColors::COLOR_WARN; }
    else if (ctx.gnss.jammingState >= 2) { jamTxt = "⚠️ 强射频压制，请远离记录仪"; jamColor = UiColors::COLOR_DANGER; }

    canvas.setTextColor(jamColor, UiColors::COLOR_PANEL_BG);
    canvas.drawString(jamTxt, 16, 174);

    char antTxt[48];
    snprintf(antTxt, sizeof(antTxt), "天线状态: %s", (ctx.gnss.antennaStatus == 2) ? "开路/未插好" : "外置有源天线正常");
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
    canvas.drawString(antTxt, 16, 192);
}
