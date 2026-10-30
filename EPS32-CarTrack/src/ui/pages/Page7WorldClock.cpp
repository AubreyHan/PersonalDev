#include "ui/pages/Page7WorldClock.h"
#include "service/TimeService.h"

extern TimeService g_timeService;

void Page7WorldClock::render(M5Canvas& canvas, const SystemContext& ctx) {
    beginViewport(canvas);

    const int cardW = 520;
    const int cardH = 300;
    const int posX[4] = {190, 730, 190, 730};
    const int posY[4] = {24,  24,  344, 344};
    const uint16_t colors[4] = {
        UiColors::COLOR_PRIMARY,   // Local (Green)
        UiColors::COLOR_BLUE,      // UTC (Ice Blue)
        UiColors::COLOR_WARN,      // US East (Amber)
        UiColors::COLOR_WHITE      // Tokyo / Custom (White)
    };

    for (int i = 0; i < 4; i++) {
        int x = posX[i], y = posY[i];
        canvas.fillRoundRect(x, y, cardW, cardH, 12, UiColors::COLOR_PANEL_BG);
        canvas.drawRoundRect(x, y, cardW, cardH, 12, colors[i]);

        uint16_t yr = 2026; uint8_t mo = 10, dy = 30, hh = 14, mm = 35, ss = 28, wd = 5;
        g_timeService.getLocalTimeParts(ctx.clocks[i].tzOffsetMinutes, ctx.clocks[i].autoDst, yr, mo, dy, hh, mm, ss, wd);

        int tzHrs = ctx.clocks[i].tzOffsetMinutes / 60;
        char title[64];
        snprintf(title, sizeof(title), "%s (UTC%+d)%s",
                 ctx.clocks[i].cityName, tzHrs,
                 ctx.clocks[i].useGpsLongitude ? " [GPS]" : (ctx.clocks[i].autoDst ? " [DST]" : ""));

        canvas.setTextDatum(top_left);
        canvas.setTextSize(2);
        canvas.setTextColor(colors[i]);
        canvas.drawString(title, x + 24, y + 24);

        char tStr[24];
        snprintf(tStr, sizeof(tStr), "%02u:%02u:%02u", hh, mm, ss);
        canvas.setTextDatum(middle_center);
        canvas.setTextSize(5);
        canvas.setTextColor(UiColors::COLOR_WHITE);
        canvas.drawString(tStr, x + cardW / 2, y + 145);

        char dateStr[64];
        int diffHrs = (ctx.clocks[i].tzOffsetMinutes - ctx.clocks[0].tzOffsetMinutes) / 60;
        snprintf(dateStr, sizeof(dateStr), "%04u-%02u-%02u  |  Rel Local: %+d h  [Tap to Edit]", yr, mo, dy, diffHrs);
        canvas.setTextDatum(bottom_left);
        canvas.setTextSize(2);
        canvas.setTextColor(UiColors::COLOR_MUTED);
        canvas.drawString(dateStr, x + 24, y + cardH - 22);
    }

    canvas.setTextDatum(middle_center);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString("Precision Source: NEO-M9N Atomic Clock + RX8130CE Hardware RTC + Wi-Fi NTP", 720, 682);

    endViewport(canvas);
}

bool Page7WorldClock::onTouchTap(int16_t x, int16_t y, SystemContext& ctx) {
    const int cardW = 520, cardH = 300;
    const int posX[4] = {190, 730, 190, 730};
    const int posY[4] = {24,  24,  344, 344};

    for (int i = 0; i < 4; i++) {
        if (x >= posX[i] && x <= posX[i] + cardW && y >= posY[i] && y <= posY[i] + cardH) {
            ctx.editingClockSlot = i;
            ctx.currentPage = SUBPAGE_EDIT_CLOCK;
            return true;
        }
    }
    return false;
}
