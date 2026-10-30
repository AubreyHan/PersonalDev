#include "ui/pages/Page6Weather.h"
#include <math.h>

void Page6Weather::render(M5Canvas& canvas, const SystemContext& ctx) {
    beginViewport(canvas);

    // Top Current Weather Header + 12H Segment Toggle Button
    canvas.fillRoundRect(185, 20, 1070, 120, 10, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(185, 20, 1070, 120, 10, UiColors::COLOR_BORDER);

    canvas.setTextDatum(top_left);
    canvas.setTextSize(4);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    char curBuf[48];
    snprintf(curBuf, sizeof(curBuf), "%.1f C", ctx.weather.currentTempC);
    canvas.drawString(curBuf, 210, 35);

    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    char metaBuf[80];
    snprintf(metaBuf, sizeof(metaBuf), "Feels %.0fC | Humidity %u%% | Wind %.0f km/h %s",
             ctx.weather.apparentTempC, ctx.weather.humidityPercent, ctx.weather.windSpeedKmh,
             ctx.weather.isOfflineCache ? "[Offline Cache]" : "[Live GPS]");
    canvas.drawString(metaBuf, 210, 92);

    // Top-Right Toggle Button: [First 12H | Next 12H]
    canvas.fillRoundRect(920, 42, 315, 68, 34, 0x0208);
    canvas.drawRoundRect(920, 42, 315, 68, 34, UiColors::COLOR_PRIMARY);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.drawString(_showSecond12Hours ? "Showing: +12H..+24H" : "Showing: Next 12H", 1077, 76);

    // Middle: 12-Hour Large-Font Forecast (88~90px per hour column)
    canvas.fillRoundRect(185, 155, 1070, 415, 10, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(185, 155, 1070, 415, 10, UiColors::COLOR_BORDER);

    int baseIdx = _showSecond12Hours ? 12 : 0;
    float minT = 100.0f, maxT = -100.0f;
    for (int i = 0; i < 12; i++) {
        float t = ctx.weather.hourly[baseIdx + i].tempC;
        if (t < minT) minT = t;
        if (t > maxT) maxT = t;
    }
    if (maxT - minT < 6.0f) { minT -= 3.0f; maxT += 3.0f; }

    int colStartX = 205;
    int colW      = 86;

    for (int i = 0; i < 12; i++) {
        const auto& h = ctx.weather.hourly[baseIdx + i];
        int cx = colStartX + i * colW + colW / 2;

        // Hour Label
        canvas.setTextDatum(top_center);
        canvas.setTextSize(2);
        canvas.setTextColor(UiColors::COLOR_WHITE);
        char hStr[12];
        snprintf(hStr, sizeof(hStr), "%02u:00", h.hour);
        canvas.drawString(hStr, cx, 175);

        // Temperature Value
        char tStr[12];
        snprintf(tStr, sizeof(tStr), "%.0fC", h.tempC);
        canvas.setTextColor(UiColors::COLOR_SECONDARY);
        canvas.drawString(tStr, cx, 220);

        // Temperature Curve Point & Line
        int py = 395 - (int)(((h.tempC - minT) / (maxT - minT)) * 120);
        canvas.fillCircle(cx, py, 5, UiColors::COLOR_PRIMARY);
        if (i > 0) {
            const auto& prev = ctx.weather.hourly[baseIdx + i - 1];
            int prevCx = colStartX + (i - 1) * colW + colW / 2;
            int prevPy = 395 - (int)(((prev.tempC - minT) / (maxT - minT)) * 120);
            canvas.drawLine(prevCx, prevPy, cx, py, UiColors::COLOR_PRIMARY);
        }

        // Precipitation Probability Bar & Percentage
        char pStr[12];
        snprintf(pStr, sizeof(pStr), "%u%%", h.precipProb);
        canvas.setTextSize(1);
        canvas.setTextColor(h.precipProb >= 50 ? UiColors::COLOR_BLUE : UiColors::COLOR_MUTED);
        canvas.drawString(pStr, cx, 435);

        int barH = (h.precipProb * 65) / 100;
        canvas.fillRect(cx - 12, 530 - barH, 24, barH, h.precipProb >= 50 ? UiColors::COLOR_BLUE : UiColors::COLOR_BORDER);
    }

    // Bottom Driving Weather Warning Card
    canvas.fillRoundRect(185, 585, 1070, 110, 10, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(185, 585, 1070, 110, 10, UiColors::COLOR_WARN);
    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_WARN);
    canvas.drawString("DRIVING WEATHER ADVISORY:", 210, 605);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString(ctx.weather.warningText, 210, 645);

    endViewport(canvas);
}

bool Page6Weather::onTouchTap(int16_t x, int16_t y, SystemContext& ctx) {
    if (x >= 185 && x <= 1255 && y >= 20 && y <= 570) {
        _showSecond12Hours = !_showSecond12Hours;
        return true;
    }
    return false;
}
