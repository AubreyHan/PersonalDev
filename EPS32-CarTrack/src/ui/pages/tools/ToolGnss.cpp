#include "ui/pages/tools/ToolGnss.h"
#include "driver/GnssDriver.h"

extern GnssDriver g_gnssDriver;

void ToolGnss::render(M5Canvas& canvas, const SystemContext& ctx) {
    beginViewport(canvas);

    canvas.fillRoundRect(185, 20, 1070, 72, 10, UiColors::COLOR_PANEL_BG);
    canvas.setTextDatum(middle_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.drawString("  Tool 3: u-blox NEO-M9N 4-Constellation & RF Telemetry", 205, 56);

    canvas.fillRoundRect(1080, 32, 155, 48, 8, UiColors::COLOR_DANGER);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString("Back", 1157, 56);

    canvas.fillRoundRect(185, 110, 1070, 420, 12, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(185, 110, 1070, 420, 12, UiColors::COLOR_BORDER);

    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    char l1[96], l2[96], l3[96], l4[96], l5[96];
    snprintf(l1, sizeof(l1), "Fix Status   : %s | Total Sats: %u (BDS:%u GPS:%u GLO:%u GAL:%u)",
             ctx.gnss.isValid ? "3D FIX LOCKED" : "SEARCHING (Stage-1 Open C/N0>=14)",
             ctx.gnss.numSats, ctx.gnss.numBdsSats, ctx.gnss.numGpsSats, ctx.gnss.numGloSats, ctx.gnss.numGalSats);
    snprintf(l2, sizeof(l2), "Coordinates  : Lat %.7f  |  Lon %.7f", ctx.gnss.latitude, ctx.gnss.longitude);
    snprintf(l3, sizeof(l3), "DOP Metrics  : HDOP %.2f  |  VDOP %.2f  |  PDOP %.2f", ctx.gnss.hdop, ctx.gnss.vdop, ctx.gnss.pdop);
    snprintf(l4, sizeof(l4), "RF Telemetry : Antenna [%u]  |  Jamming [%u]  |  Noise %u",
             ctx.gnss.antennaStatus, ctx.gnss.jammingState, ctx.gnss.rfNoiseLevel);
    snprintf(l5, sizeof(l5), "UTC Clock    : %04u-%02u-%02u %02u:%02u:%02u.%03u UTC",
             ctx.gnss.year, ctx.gnss.month, ctx.gnss.day, ctx.gnss.hour, ctx.gnss.minute, ctx.gnss.second, ctx.gnss.millisecond);

    canvas.drawString(l1, 215, 140);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString(l2, 215, 200);
    canvas.drawString(l3, 215, 260);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.drawString(l4, 215, 320);
    canvas.setTextColor(UiColors::COLOR_BLUE);
    canvas.drawString(l5, 215, 380);

    // Hot-Restart GNSS Re-Acquisition Button
    canvas.fillRoundRect(185, 555, 1070, 115, 12, 0x0208);
    canvas.drawRoundRect(185, 555, 1070, 115, 12, UiColors::COLOR_PRIMARY);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.drawString("Force Hot-Restart GNSS & Re-Inject RTC/NVS Position (Fast Lock)", 720, 612);

    endViewport(canvas);
}

bool ToolGnss::onTouchTap(int16_t x, int16_t y, SystemContext& ctx) {
    if (x >= 1080 && x <= 1235 && y >= 32 && y <= 80) {
        ctx.currentPage = PAGE_8_TOOLS;
        return true;
    }
    if (x >= 185 && x <= 1255 && y >= 555 && y <= 670) {
        g_gnssDriver.triggerHotStartReacquisition();
        return true;
    }
    return false;
}
