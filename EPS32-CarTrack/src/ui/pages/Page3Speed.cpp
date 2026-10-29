#include "ui/pages/Page3Speed.h"
#include "config.h"

void Page3Speed::draw(M5Canvas& canvas, const SystemContext& ctx) {
    // 1. Current Speed Header
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
    canvas.setTextSize(1);
    canvas.drawString("CURRENT SPEED", 10, 28);

    char spdBuf[24];
    snprintf(spdBuf, sizeof(spdBuf), "%.0f km/h", ctx.gnss.speedKmh);
    canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_BG);
    canvas.setTextSize(3);
    canvas.drawString(spdBuf, 10, 42);

    // 2. 1-Hour Chart Area (Width 308px: X 6~314, Y 75~170)
    int chartX = 6;
    int chartY = 75;
    int chartW = 308;
    int chartH = 95;

    canvas.fillRoundRect(chartX, chartY, chartW, chartH, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(chartX, chartY, chartW, chartH, 4, UiColors::COLOR_BORDER);

    // Speed always starts at 0, max scale depends on highest speed
    float maxVal = 100.0f;
    const HistoryBuffer& buf = ctx.trip.speedHistory;
    size_t count = buf.count;

    if (count > 0) {
        for (size_t i = 0; i < count; i++) {
            float val = buf.getSample(i);
            if (val > maxVal) maxVal = val;
        }
    }
    if (ctx.gnss.speedKmh > maxVal) maxVal = ctx.gnss.speedKmh;
    maxVal = ceilf(maxVal / 20.0f) * 20.0f; // Round up to nearest 20km/h

    // Draw grid lines
    canvas.drawFastHLine(chartX + 4, chartY + chartH / 2, chartW - 8, UiColors::COLOR_BORDER);
    canvas.drawFastHLine(chartX + 4, chartY + chartH - 18, chartW - 8, UiColors::COLOR_BORDER);

    // Draw axis labels
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
    char maxLabel[16];
    snprintf(maxLabel, sizeof(maxLabel), "%.0fkm", maxVal);
    canvas.drawString(maxLabel, chartX + 6, chartY + 4);
    canvas.drawString("0km", chartX + 6, chartY + chartH - 30);

    canvas.drawString("-60m", chartX + 10, chartY + chartH - 14);
    canvas.drawString("-30m", chartX + chartW / 2 - 12, chartY + chartH - 14);
    canvas.drawString("Now", chartX + chartW - 30, chartY + chartH - 14);

    // Draw speed profile line
    if (count >= 2) {
        int prevPx = chartX + 35;
        int prevPy = chartY + chartH - 20 - (int)((buf.getSample(0) / maxVal) * (chartH - 36));

        for (size_t i = 1; i < count; i++) {
            int px = chartX + 35 + (int)((i * (chartW - 45)) / HistoryBuffer::CAPACITY);
            int py = chartY + chartH - 20 - (int)((buf.getSample(i) / maxVal) * (chartH - 36));
            canvas.drawLine(prevPx, prevPy, px, py, UiColors::COLOR_SECONDARY);
            prevPx = px;
            prevPy = py;
        }
    }

    // 3. Bottom Extremes Cards (Strictly NO minimum speed)
    canvas.fillRoundRect(8, 178, 148, 44, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(8, 178, 148, 44, 4, UiColors::COLOR_BORDER);
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_DANGER, UiColors::COLOR_PANEL_BG);
    canvas.drawString("▲ MAX SPEED", 14, 182);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_WHITE, UiColors::COLOR_PANEL_BG);
    char maxSpdVal[16];
    snprintf(maxSpdVal, sizeof(maxSpdVal), "%.0f km/h", ctx.trip.maxSpeedKmh);
    canvas.drawString(maxSpdVal, 14, 198);

    canvas.fillRoundRect(164, 178, 148, 44, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(164, 178, 148, 44, 4, UiColors::COLOR_BORDER);
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_PANEL_BG);
    canvas.drawString("AVG SPEED", 170, 182);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_WHITE, UiColors::COLOR_PANEL_BG);
    char avgSpdVal[16];
    snprintf(avgSpdVal, sizeof(avgSpdVal), "%.1f km/h", ctx.trip.avgSpeedKmh);
    canvas.drawString(avgSpdVal, 170, 198);
}
