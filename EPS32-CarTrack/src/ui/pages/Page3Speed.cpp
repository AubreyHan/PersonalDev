#include "ui/pages/Page3Speed.h"
#include <math.h>

void Page3Speed::render(M5Canvas& canvas, const SystemContext& ctx) {
    beginViewport(canvas);

    // Top Header: Current Speed + Max Speed (MAX) + Average Speed (AVG) - Strictly NO Min Speed
    canvas.fillRoundRect(185, 20, 1070, 125, 10, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(185, 20, 1070, 125, 10, UiColors::COLOR_BORDER);

    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString("CURRENT SPEED", 210, 34);

    canvas.setTextSize(5);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    char spdBuf[32];
    snprintf(spdBuf, sizeof(spdBuf), "%.1f km/h", ctx.gnss.speedKmh);
    canvas.drawString(spdBuf, 210, 68);

    canvas.setTextDatum(top_right);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_WARN);
    char maxBuf[40], avgBuf[40];
    snprintf(maxBuf, sizeof(maxBuf), "MAX SPEED: %.1f km/h", ctx.trip.maxSpeedKmh);
    snprintf(avgBuf, sizeof(avgBuf), "AVG SPEED: %.1f km/h", ctx.trip.avgSpeedKmh);
    canvas.drawString(maxBuf, 1230, 42);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.drawString(avgBuf, 1230, 88);

    // Middle: 1050px Wide 1-Hour Continuous Speed Curve (X: 185..1255, Y: 160..545)
    int chartX = 205, chartY = 180, chartW = 1030, chartH = 335;
    canvas.fillRoundRect(185, 160, 1070, 385, 10, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(185, 160, 1070, 385, 10, UiColors::COLOR_BORDER);

    float maxS = (ctx.trip.maxSpeedKmh > 120.0f) ? (ctx.trip.maxSpeedKmh + 20.0f) : 140.0f;

    for (int i = 0; i <= 4; i++) {
        int gy = chartY + (chartH * i) / 4;
        canvas.drawFastHLine(chartX, gy, chartW, UiColors::COLOR_BORDER);
    }

    uint16_t cnt = ctx.trip.speedHistory.count;
    if (cnt >= 2) {
        for (uint16_t i = 1; i < cnt; i++) {
            float v0 = ctx.trip.speedHistory.getOldestFirst(i - 1);
            float v1 = ctx.trip.speedHistory.getOldestFirst(i);
            int x0 = chartX + (int)((uint32_t)(i - 1) * chartW / (HistoryBuffer::CAPACITY - 1));
            int x1 = chartX + (int)((uint32_t)i * chartW / (HistoryBuffer::CAPACITY - 1));
            int y0 = chartY + chartH - (int)((v0 / maxS) * chartH);
            int y1 = chartY + chartH - (int)((v1 / maxS) * chartH);
            y0 = constrain(y0, chartY, chartY + chartH);
            y1 = constrain(y1, chartY, chartY + chartH);
            canvas.drawLine(x0, y0, x1, y1, UiColors::COLOR_SECONDARY);
            canvas.drawLine(x0, y0 + 1, x1, y1 + 1, UiColors::COLOR_PRIMARY);
        }
    } else {
        canvas.drawFastHLine(chartX, chartY + chartH, chartW, UiColors::COLOR_PRIMARY);
    }

    // Bottom Card: Speed Structure & Moving vs Idle Breakdown
    canvas.fillRoundRect(185, 560, 1070, 135, 10, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(185, 560, 1070, 135, 10, UiColors::COLOR_BORDER);

    uint32_t moveTot = ctx.trip.highwaySec + ctx.trip.urbanSec + ctx.trip.slowSec;
    int pctHwy = (moveTot > 0) ? (int)((ctx.trip.highwaySec * 100) / moveTot) : 0;
    int pctUrb = (moveTot > 0) ? (int)((ctx.trip.urbanSec   * 100) / moveTot) : 0;
    int pctSlw = (moveTot > 0) ? (100 - pctHwy - pctUrb) : 0;

    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    char distStats[96];
    snprintf(distStats, sizeof(distStats),
             "Highway (>80km/h): %d%%   |   Urban (40-80): %d%%   |   Slow (<40): %d%%",
             pctHwy, pctUrb, pctSlw);
    canvas.drawString(distStats, 210, 582);

    uint32_t mH = ctx.trip.movingDurationSec / 3600, mM = (ctx.trip.movingDurationSec % 3600) / 60, mS = ctx.trip.movingDurationSec % 60;
    uint32_t iH = ctx.trip.idleDurationSec   / 3600, iM = (ctx.trip.idleDurationSec   % 3600) / 60, iS = ctx.trip.idleDurationSec   % 60;
    char timeStats[96];
    snprintf(timeStats, sizeof(timeStats),
             "Moving Time: %02u:%02u:%02u          |          Idle / Stop Time: %02u:%02u:%02u",
             mH, mM, mS, iH, iM, iS);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString(timeStats, 210, 635);

    endViewport(canvas);
}
