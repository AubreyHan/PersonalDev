#include "ui/pages/Page6Weather.h"
#include "config.h"

void Page6Weather::draw(M5Canvas& canvas, const SystemContext& ctx) {
    // 1. Current Weather Banner
    canvas.setTextSize(1);
    char weatherBanner[48];
    snprintf(weatherBanner, sizeof(weatherBanner), "气象: 晴 / 多云  %.1f°C", ctx.weather.temperatureC);
    canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_BG);
    canvas.drawString(weatherBanner, 10, 28);

    char slotLabel[32];
    uint8_t slot = ctx.weather.currentSlotIdx;
    if (slot == 0) snprintf(slotLabel, sizeof(slotLabel), "[1/3] 0~8h ↕️");
    else if (slot == 1) snprintf(slotLabel, sizeof(slotLabel), "[2/3] 8~16h ↕️");
    else snprintf(slotLabel, sizeof(slotLabel), "[3/3] 16~24h ↕️");
    canvas.setTextColor(UiColors::COLOR_SECONDARY, UiColors::COLOR_BG);
    canvas.drawRightString(slotLabel, 310, 28);

    char subBanner[64];
    snprintf(subBanner, sizeof(subBanner), "湿度: %d%% | 风速: %.0f km/h", ctx.weather.relativeHumidity, ctx.weather.windSpeedKmh);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
    canvas.drawString(subBanner, 10, 44);

    // 2. 8-Hour Forecast Chart: 8 slots, slot width 38px
    // Viewport: X: 8~312, Y: 60~180 (Height 120)
    int chartX = 8;
    int chartY = 60;
    int chartW = 304;
    int chartH = 120;
    int slotW = chartW / 8; // 38px

    canvas.fillRoundRect(chartX, chartY, chartW, chartH, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(chartX, chartY, chartW, chartH, 4, UiColors::COLOR_BORDER);

    uint8_t startHourIdx = slot * 8;

    // Draw slot dividers and hourly predictions
    int prevX = -1, prevY = -1;
    for (uint8_t i = 0; i < 8; i++) {
        uint8_t hIdx = startHourIdx + i;
        int sx = chartX + i * slotW;

        if (i > 0) {
            canvas.drawFastVLine(sx, chartY + 4, chartH - 8, UiColors::COLOR_BORDER);
        }

        // Hour Label
        char hBuf[8];
        snprintf(hBuf, sizeof(hBuf), "%02d:00", hIdx);
        canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
        canvas.drawCenterString(hBuf, sx + slotW / 2, chartY + 6);

        int temp = ctx.weather.hourly[hIdx].tempC;
        uint8_t rainProb = ctx.weather.hourly[hIdx].rainProbPercent;

        // Temperature number
        char tBuf[8];
        snprintf(tBuf, sizeof(tBuf), "%d°", temp);
        canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_PANEL_BG);
        canvas.drawCenterString(tBuf, sx + slotW / 2, chartY + 22);

        // Precipitation Probability Bar at bottom (Y: chartY+80 ~ chartY+115)
        int barMaxH = 30;
        int barH = (rainProb * barMaxH) / 100;
        uint16_t barColor = (rainProb > 50) ? UiColors::COLOR_BLUE : UiColors::COLOR_MUTED;
        canvas.fillRect(sx + 10, chartY + chartH - 6 - barH, slotW - 20, barH, barColor);

        // Rain % text
        char rBuf[8];
        snprintf(rBuf, sizeof(rBuf), "%d%%", rainProb);
        canvas.setTextColor(UiColors::COLOR_WHITE, UiColors::COLOR_PANEL_BG);
        canvas.drawCenterString(rBuf, sx + slotW / 2, chartY + chartH - 24);

        // Temp curve point
        int ptX = sx + slotW / 2;
        int ptY = chartY + 54 - (temp - 15) * 2;
        if (ptY < chartY + 36) ptY = chartY + 36;
        if (ptY > chartY + 70) ptY = chartY + 70;

        canvas.fillCircle(ptX, ptY, 2, UiColors::COLOR_SECONDARY);
        if (prevX >= 0) {
            canvas.drawLine(prevX, prevY, ptX, ptY, UiColors::COLOR_SECONDARY);
        }
        prevX = ptX;
        prevPy = ptY;
    }

    // 3. Bottom Advice / Cache hint
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
    canvas.drawString("提示: 上下滑动切换时段 | 离线缓存全天可用", 10, 195);
}

void Page6Weather::handleSwipeUp() {
    SystemState& state = SystemState::instance();
    state.lock();
    uint8_t& slot = state.getContextUnsafe().weather.currentSlotIdx;
    if (slot < 2) slot++;
    else slot = 0;
    state.unlock();
}

void Page6Weather::handleSwipeDown() {
    SystemState& state = SystemState::instance();
    state.lock();
    uint8_t& slot = state.getContextUnsafe().weather.currentSlotIdx;
    if (slot > 0) slot--;
    else slot = 2;
    state.unlock();
}
