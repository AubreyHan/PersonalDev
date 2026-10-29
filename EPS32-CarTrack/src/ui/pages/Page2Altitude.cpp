#include "ui/pages/Page2Altitude.h"
#include "config.h"

void Page2Altitude::draw(M5Canvas& canvas, const SystemContext& ctx) {
    // 1. Current Altitude Header
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
    canvas.setTextSize(1);
    canvas.drawString("CURRENT ALTITUDE", 10, 28);

    char climbBuf[32];
    snprintf(climbBuf, sizeof(climbBuf), "累计爬升: +%.0fm", ctx.trip.cumulativeClimb);
    canvas.drawRightString(climbBuf, 310, 28);

    char altBuf[24];
    snprintf(altBuf, sizeof(altBuf), "%.0f m", ctx.gnss.altitudeMeters);
    canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_BG);
    canvas.setTextSize(3);
    canvas.drawString(altBuf, 10, 42);

    // 2. 1-Hour Chart Area (Width 308px: X 6~314, Y 75~170)
    int chartX = 6;
    int chartY = 75;
    int chartW = 308;
    int chartH = 95;

    canvas.fillRoundRect(chartX, chartY, chartW, chartH, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(chartX, chartY, chartW, chartH, 4, UiColors::COLOR_BORDER);

    // Find min and max in buffer
    float minVal = 9999.0f;
    float maxVal = -9999.0f;
    const HistoryBuffer& buf = ctx.trip.altHistory;
    size_t count = buf.count;

    if (count > 0) {
        for (size_t i = 0; i < count; i++) {
            float val = buf.getSample(i);
            if (val < minVal) minVal = val;
            if (val > maxVal) maxVal = val;
        }
    } else {
        minVal = ctx.gnss.altitudeMeters - 10.0f;
        maxVal = ctx.gnss.altitudeMeters + 10.0f;
    }

    if (maxVal - minVal < 20.0f) {
        maxVal += 10.0f;
        minVal -= 10.0f;
    }

    // Draw grid lines
    canvas.drawFastHLine(chartX + 4, chartY + chartH / 2, chartW - 8, UiColors::COLOR_BORDER);
    canvas.drawFastHLine(chartX + 4, chartY + chartH - 18, chartW - 8, UiColors::COLOR_BORDER);

    // Draw axis labels
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
    char maxLabel[16], minLabel[16];
    snprintf(maxLabel, sizeof(maxLabel), "%.0fm", maxVal);
    snprintf(minLabel, sizeof(minLabel), "%.0fm", minVal);
    canvas.drawString(maxLabel, chartX + 6, chartY + 4);
    canvas.drawString(minLabel, chartX + 6, chartY + chartH - 30);

    canvas.drawString("-60m", chartX + 10, chartY + chartH - 14);
    canvas.drawString("-30m", chartX + chartW / 2 - 12, chartY + chartH - 14);
    canvas.drawString("Now", chartX + chartW - 30, chartY + chartH - 14);

    // Draw profile line
    if (count >= 2) {
        int prevPx = chartX + 35;
        int prevPy = chartY + chartH - 20 - (int)(((buf.getSample(0) - minVal) / (maxVal - minVal)) * (chartH - 36));

        for (size_t i = 1; i < count; i++) {
            int px = chartX + 35 + (int)((i * (chartW - 45)) / HistoryBuffer::CAPACITY);
            int py = chartY + chartH - 20 - (int)(((buf.getSample(i) - minVal) / (maxVal - minVal)) * (chartH - 36));
            canvas.drawLine(prevPx, prevPy, px, py, UiColors::COLOR_PRIMARY);
            prevPx = px;
            prevPy = py;
        }
    }

    // 3. Bottom Extremes Cards (Y: 176~224)
    canvas.fillRoundRect(8, 178, 148, 44, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(8, 178, 148, 44, 4, UiColors::COLOR_BORDER);
    canvas.setTextColor(UiColors::COLOR_DANGER, UiColors::COLOR_PANEL_BG);
    char maxAltStr[32];
    snprintf(maxAltStr, sizeof(maxAltStr), "▲ 最高: %.0f m", ctx.trip.maxAltitude);
    canvas.drawString(maxAltStr, 14, 192);

    canvas.fillRoundRect(164, 178, 148, 44, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(164, 178, 148, 44, 4, UiColors::COLOR_BORDER);
    canvas.setTextColor(UiColors::COLOR_BLUE, UiColors::COLOR_PANEL_BG);
    char minAltStr[32];
    snprintf(minAltStr, sizeof(minAltStr), "▼ 最低: %.0f m", ctx.trip.minAltitude);
    canvas.drawString(minAltStr, 170, 192);
}
