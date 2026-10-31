#include "ui/pages/Page2Altitude.h"
#include "driver/HapticDriver.h"
#include <math.h>

void Page2Altitude::render(M5Canvas& canvas, const SystemContext& ctx) {
    beginViewport(canvas);

    // Top Header: Current Altitude + Cumulative Climb + Vertical Speed
    canvas.fillRoundRect(185, 20, 1070, 125, 10, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(185, 20, 1070, 125, 10, UiColors::COLOR_BORDER);

    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString("CURRENT ALTITUDE (BMP280 + GNSS Fused)", 210, 34);

    // Dynamic Interactive Trip View Tabs: [单次 TRIP] [单日 DAY] (and [SUB小记] only when active)
    auto drawTripTab = [&](int x, int w, const char* label, bool active) {
        if (active) {
            canvas.fillRoundRect(x, 28, w, 32, 6, 0x0208);
            canvas.drawRoundRect(x, 28, w, 32, 6, UiColors::COLOR_PRIMARY);
            canvas.setTextColor(UiColors::COLOR_PRIMARY);
        } else {
            canvas.fillRoundRect(x, 28, w, 32, 6, UiColors::COLOR_CARD_BG);
            canvas.drawRoundRect(x, 28, w, 32, 6, UiColors::COLOR_BORDER);
            canvas.setTextColor(UiColors::COLOR_MUTED);
        }
        canvas.setTextDatum(middle_center);
        canvas.setTextSize(2);
        canvas.drawString(label, x + w / 2, 44);
    };

    if (!ctx.manualTripActive) {
        drawTripTab(590, 110, "TRIP单次", ctx.activeTripCategory == TRIP_SINGLE);
        drawTripTab(715, 110, "DAY单日",  ctx.activeTripCategory == TRIP_DAILY);
    } else {
        drawTripTab(560, 100, "TRIP单次", ctx.activeTripCategory == TRIP_SINGLE);
        drawTripTab(670, 100, "DAY单日",  ctx.activeTripCategory == TRIP_DAILY);
        drawTripTab(780, 100, "SUB小记",  ctx.activeTripCategory == TRIP_MANUAL);
    }

    canvas.setTextDatum(top_left);
    canvas.setTextSize(5);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    char altBuf[32];
    snprintf(altBuf, sizeof(altBuf), "%.1f m", ctx.trip.currentAltitudeM);
    canvas.drawString(altBuf, 210, 68);

    canvas.setTextDatum(top_right);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    char climbBuf[40], vsBuf[40];
    snprintf(climbBuf, sizeof(climbBuf), "Cumulative Climb: +%.1f m", ctx.trip.cumulativeClimbM);
    snprintf(vsBuf,    sizeof(vsBuf),    "Vertical Speed: %+.2f m/s", ctx.trip.verticalSpeedMps);
    canvas.drawString(climbBuf, 1230, 42);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString(vsBuf, 1230, 88);

    // Middle: 1050px Wide 1-Hour Continuous Elevation Profile Chart (X: 185..1255, Y: 160..550)
    int chartX = 205, chartY = 180, chartW = 1030, chartH = 340;
    canvas.fillRoundRect(185, 160, 1070, 395, 10, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(185, 160, 1070, 395, 10, UiColors::COLOR_BORDER);

    float minA = ctx.trip.minAltitudeM - 15.0f;
    float maxA = ctx.trip.maxAltitudeM + 15.0f;
    if (maxA - minA < 40.0f) {
        minA = ctx.trip.currentAltitudeM - 20.0f;
        maxA = ctx.trip.currentAltitudeM + 20.0f;
    }

    // Horizontal grid lines
    for (int i = 0; i <= 4; i++) {
        int gy = chartY + (chartH * i) / 4;
        canvas.drawFastHLine(chartX, gy, chartW, UiColors::COLOR_BORDER);
    }

    uint16_t cnt = ctx.trip.altitudeHistory.count;
    if (cnt >= 2) {
        for (uint16_t i = 1; i < cnt; i++) {
            float v0 = ctx.trip.altitudeHistory.getOldestFirst(i - 1);
            float v1 = ctx.trip.altitudeHistory.getOldestFirst(i);
            int x0 = chartX + (int)((uint32_t)(i - 1) * chartW / (HistoryBuffer::CAPACITY - 1));
            int x1 = chartX + (int)((uint32_t)i * chartW / (HistoryBuffer::CAPACITY - 1));
            int y0 = chartY + chartH - (int)(((v0 - minA) / (maxA - minA)) * chartH);
            int y1 = chartY + chartH - (int)(((v1 - minA) / (maxA - minA)) * chartH);
            y0 = constrain(y0, chartY, chartY + chartH);
            y1 = constrain(y1, chartY, chartY + chartH);

            // Subtle under-curve glow line
            canvas.drawLine(x0, y0, x1, y1, UiColors::COLOR_PRIMARY);
            canvas.drawLine(x0, y0 + 1, x1, y1 + 1, UiColors::COLOR_PRIMARY);
        }
    } else {
        int midY = chartY + chartH / 2;
        canvas.drawFastHLine(chartX, midY, chartW, UiColors::COLOR_PRIMARY);
    }

    canvas.setTextDatum(top_center);
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString("-60m", chartX + 20, chartY + chartH + 8);
    canvas.drawString("-45m", chartX + chartW / 4, chartY + chartH + 8);
    canvas.drawString("-30m", chartX + chartW / 2, chartY + chartH + 8);
    canvas.drawString("-15m", chartX + chartW * 3 / 4, chartY + chartH + 8);
    canvas.drawString("Now",  chartX + chartW - 20, chartY + chartH + 8);

    // Bottom Cards: Max Altitude & Min Altitude Extremes
    canvas.fillRoundRect(185, 570, 520, 125, 10, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(185, 570, 520, 125, 10, UiColors::COLOR_WARN);
    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_WARN);
    canvas.drawString("MAX TRIP ALTITUDE", 210, 588);
    canvas.setTextSize(4);
    char maxStr[32];
    snprintf(maxStr, sizeof(maxStr), "%.1f m", ctx.trip.maxAltitudeM);
    canvas.drawString(maxStr, 210, 628);

    canvas.fillRoundRect(735, 570, 520, 125, 10, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(735, 570, 520, 125, 10, UiColors::COLOR_BLUE);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_BLUE);
    canvas.drawString("MIN TRIP ALTITUDE", 760, 588);
    canvas.setTextSize(4);
    char minStr[32];
    snprintf(minStr, sizeof(minStr), "%.1f m", ctx.trip.minAltitudeM);
    canvas.drawString(minStr, 760, 628);

    endViewport(canvas);
}

bool Page2Altitude::onTouchTap(int16_t x, int16_t y, SystemContext& ctx) {
    if (y >= 20 && y <= 68) {
        if (!ctx.manualTripActive) {
            if (x >= 585 && x <= 705) {
                SystemState::instance().switchActiveTripCategory(TRIP_SINGLE);
                HapticDriver::instance().triggerShortPulse(50);
                return true;
            }
            if (x >= 710 && x <= 830) {
                SystemState::instance().switchActiveTripCategory(TRIP_DAILY);
                HapticDriver::instance().triggerShortPulse(50);
                return true;
            }
        } else {
            if (x >= 555 && x <= 665) {
                SystemState::instance().switchActiveTripCategory(TRIP_SINGLE);
                HapticDriver::instance().triggerShortPulse(50);
                return true;
            }
            if (x >= 665 && x <= 775) {
                SystemState::instance().switchActiveTripCategory(TRIP_DAILY);
                HapticDriver::instance().triggerShortPulse(50);
                return true;
            }
            if (x >= 775 && x <= 885) {
                SystemState::instance().switchActiveTripCategory(TRIP_MANUAL);
                HapticDriver::instance().triggerShortPulse(50);
                return true;
            }
        }
    }
    return false;
}
