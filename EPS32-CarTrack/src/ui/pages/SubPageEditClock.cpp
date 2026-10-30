#include "ui/pages/SubPageEditClock.h"
#include <math.h>

struct CityPreset {
    const char* name;
    int16_t     offsetMin;
    bool        dst;
};

static const CityPreset PRESETS[4][4] = {
    { {"Beijing", 480, false}, {"Tokyo", 540, false}, {"Singapore", 480, false}, {"Sydney", 600, true} },
    { {"New York", -300, true}, {"Los Angeles", -480, true}, {"Chicago", -360, true}, {"Toronto", -300, true} },
    { {"London", 0, true}, {"Paris", 60, true}, {"Berlin", 60, true}, {"Dubai", 240, false} },
    { {"UTC+0", 0, false}, {"UTC+8", 480, false}, {"UTC-5", -300, false}, {"UTC+1", 60, false} }
};

void SubPageEditClock::render(M5Canvas& canvas, const SystemContext& ctx) {
    beginViewport(canvas);

    canvas.fillRoundRect(185, 20, 1070, 75, 10, UiColors::COLOR_PANEL_BG);
    canvas.setTextDatum(middle_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "  Edit World Clock Slot #%u (%s)", ctx.editingClockSlot + 1, ctx.clocks[ctx.editingClockSlot].cityName);
    canvas.drawString(hdr, 200, 57);

    // Back Button
    canvas.fillRoundRect(1080, 32, 155, 50, 8, UiColors::COLOR_DANGER);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString("Back", 1157, 57);

    // Mode 1: GPS Longitude Auto Timezone Button
    canvas.fillRoundRect(185, 115, 1070, 90, 10, 0x0208);
    canvas.drawRoundRect(185, 115, 1070, 90, 10, UiColors::COLOR_PRIMARY);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.drawString("Mode 1: Auto Calculate Timezone from Current GPS Longitude", 720, 160);

    // Mode 2: 4 Region Category Tabs
    const char* tabs[4] = {"Asia / Pacific", "Americas", "Europe / ME", "UTC Standard"};
    for (int t = 0; t < 4; t++) {
        int tx = 185 + t * 270;
        bool active = (_regionTab == t);
        canvas.fillRoundRect(tx, 225, 255, 65, 8, active ? UiColors::COLOR_SECONDARY : UiColors::COLOR_PANEL_BG);
        canvas.setTextColor(active ? UiColors::COLOR_BG : UiColors::COLOR_WHITE);
        canvas.drawString(tabs[t], tx + 127, 257);
    }

    // 2x2 Large City Selection Cards
    for (int i = 0; i < 4; i++) {
        int cx = (i % 2 == 0) ? 185 : 730;
        int cy = (i < 2) ? 315 : 505;
        canvas.fillRoundRect(cx, cy, 525, 165, 10, UiColors::COLOR_PANEL_BG);
        canvas.drawRoundRect(cx, cy, 525, 165, 10, UiColors::COLOR_BORDER);

        const auto& p = PRESETS[_regionTab][i];
        canvas.setTextColor(UiColors::COLOR_WHITE);
        canvas.setTextSize(3);
        canvas.drawString(p.name, cx + 262, cy + 60);

        char sub[32];
        snprintf(sub, sizeof(sub), "UTC%+d %s", p.offsetMin / 60, p.dst ? "(Auto DST)" : "");
        canvas.setTextSize(2);
        canvas.setTextColor(UiColors::COLOR_PRIMARY);
        canvas.drawString(sub, cx + 262, cy + 115);
    }

    endViewport(canvas);
}

bool SubPageEditClock::onTouchTap(int16_t x, int16_t y, SystemContext& ctx) {
    uint8_t slot = ctx.editingClockSlot % 4;

    // Back button
    if (x >= 1080 && x <= 1235 && y >= 32 && y <= 82) {
        ctx.currentPage = PAGE_7_WORLD_CLOCK;
        return true;
    }

    // Mode 1: GPS Longitude Auto Timezone
    if (x >= 185 && x <= 1255 && y >= 115 && y <= 205) {
        strncpy(ctx.clocks[slot].cityName, "GPS Local", sizeof(ctx.clocks[slot].cityName) - 1);
        int tzH = ctx.gnss.isValid ? (int)round(ctx.gnss.longitude / 15.0) : 8;
        ctx.clocks[slot].tzOffsetMinutes = tzH * 60;
        ctx.clocks[slot].autoDst = false;
        ctx.clocks[slot].useGpsLongitude = true;
        ctx.currentPage = PAGE_7_WORLD_CLOCK;
        return true;
    }

    // Region Tabs
    if (y >= 225 && y <= 290) {
        for (int t = 0; t < 4; t++) {
            int tx = 185 + t * 270;
            if (x >= tx && x <= tx + 255) {
                _regionTab = t;
                return true;
            }
        }
    }

    // 2x2 City Cards
    for (int i = 0; i < 4; i++) {
        int cx = (i % 2 == 0) ? 185 : 730;
        int cy = (i < 2) ? 315 : 505;
        if (x >= cx && x <= cx + 525 && y >= cy && y <= cy + 165) {
            const auto& p = PRESETS[_regionTab][i];
            strncpy(ctx.clocks[slot].cityName, p.name, sizeof(ctx.clocks[slot].cityName) - 1);
            ctx.clocks[slot].tzOffsetMinutes = p.offsetMin;
            ctx.clocks[slot].autoDst         = p.dst;
            ctx.clocks[slot].useGpsLongitude = false;
            ctx.currentPage = PAGE_7_WORLD_CLOCK;
            return true;
        }
    }

    return false;
}
