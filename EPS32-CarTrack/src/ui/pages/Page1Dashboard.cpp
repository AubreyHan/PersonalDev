#include "ui/pages/Page1Dashboard.h"
#include "driver/HapticDriver.h"
#include <math.h>

static const char* headingCardinal(float deg) {
    static const char* dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int idx = (int)((deg + 22.5f) / 45.0f) % 8;
    return dirs[idx];
}

void Page1Dashboard::render(M5Canvas& canvas, const SystemContext& ctx) {
    beginViewport(canvas);

    // Left Half: 360px Large Glowing Compass + Mini G-Force Target (X: 160..640)
    int compCx = 400;
    int compCy = 235;
    int compR  = 175;

    canvas.drawCircle(compCx, compCy, compR, UiColors::COLOR_PRIMARY);
    canvas.drawCircle(compCx, compCy, compR - 2, UiColors::COLOR_BORDER);
    canvas.drawCircle(compCx, compCy, compR - 28, UiColors::COLOR_BORDER);

    // Degree ticks around 360px compass
    for (int deg = 0; deg < 360; deg += 15) {
        float rad = (deg - 90.0f) * 0.0174532925f;
        int rIn = (deg % 45 == 0) ? (compR - 18) : (compR - 10);
        int x1 = compCx + (int)(cosf(rad) * rIn);
        int y1 = compCy + (int)(sinf(rad) * rIn);
        int x2 = compCx + (int)(cosf(rad) * compR);
        int y2 = compCy + (int)(sinf(rad) * compR);
        canvas.drawLine(x1, y1, x2, y2, (deg % 90 == 0) ? UiColors::COLOR_SECONDARY : UiColors::COLOR_MUTED);
    }

    canvas.setTextDatum(middle_center);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_DANGER);
    canvas.drawString("N", compCx, compCy - compR + 30);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString("S", compCx, compCy + compR - 30);
    canvas.drawString("E", compCx + compR - 30, compCy);
    canvas.drawString("W", compCx - compR + 30, compCy);

    // Rotating pointer needle (MagSafe-immune fused heading)
    float hdg = ctx.imu.fusedHeadingDeg;
    float ptrRad = (hdg - 90.0f) * 0.0174532925f;
    int tipX = compCx + (int)(cosf(ptrRad) * (compR - 45));
    int tipY = compCy + (int)(sinf(ptrRad) * (compR - 45));
    int tailX = compCx - (int)(cosf(ptrRad) * 45);
    int tailY = compCy - (int)(sinf(ptrRad) * 45);
    int sideL_X = compCx + (int)(cosf(ptrRad + 1.57f) * 14);
    int sideL_Y = compCy + (int)(sinf(ptrRad + 1.57f) * 14);
    int sideR_X = compCx + (int)(cosf(ptrRad - 1.57f) * 14);
    int sideR_Y = compCy + (int)(sinf(ptrRad - 1.57f) * 14);

    canvas.fillTriangle(tipX, tipY, sideL_X, sideL_Y, sideR_X, sideR_Y, UiColors::COLOR_PRIMARY);
    canvas.fillTriangle(tailX, tailY, sideL_X, sideL_Y, sideR_X, sideR_Y, UiColors::COLOR_MUTED);
    canvas.fillCircle(compCx, compCy, 8, UiColors::COLOR_SECONDARY);

    // Heading numeric readout below compass
    char hdgStr[32];
    snprintf(hdgStr, sizeof(hdgStr), "%03d deg %s", (int)roundf(hdg) % 360, headingCardinal(hdg));
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.setTextSize(3);
    canvas.drawString(hdgStr, compCx, 442);

    // Bottom-Left: Mini G-Force Target Widget (Tap jumps to Page 5 Dynamics)
    canvas.fillRoundRect(190, 480, 420, 210, 10, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(190, 480, 420, 210, 10, UiColors::COLOR_BORDER);
    int gCx = 285, gCy = 585, gR = 75;
    canvas.drawCircle(gCx, gCy, gR, UiColors::COLOR_PRIMARY);
    canvas.drawCircle(gCx, gCy, gR / 2, UiColors::COLOR_BORDER);
    canvas.drawFastHLine(gCx - gR, gCy, gR * 2, UiColors::COLOR_BORDER);
    canvas.drawFastVLine(gCx, gCy - gR, gR * 2, UiColors::COLOR_BORDER);

    int dotX = gCx + (int)(ctx.imu.lateralG * gR);
    int dotY = gCy - (int)(ctx.imu.longitudinalG * gR);
    canvas.fillCircle(dotX, dotY, 7, UiColors::COLOR_DANGER);

    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.drawString("G-FORCE & ATTITUDE", 385, 505);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    char gBuf1[32], gBuf2[32], gBuf3[32];
    snprintf(gBuf1, sizeof(gBuf1), "Long: %+.2f G", ctx.imu.longitudinalG);
    snprintf(gBuf2, sizeof(gBuf2), "Lat : %+.2f G", ctx.imu.lateralG);
    snprintf(gBuf3, sizeof(gBuf3), "Slope: %+.1f%%", ctx.trip.roadSlopePercent);
    canvas.drawString(gBuf1, 385, 545);
    canvas.drawString(gBuf2, 385, 580);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.drawString(gBuf3, 385, 615);
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString("[Tap -> Attitude Details]", 385, 655);

    // Right Half: Primary Speedometer + Altitude Card + TRIP Card (X: 650..1250)
    // 1. Speedometer Card (X: 650..1250, Y: 24..310) -> Tap jumps to Page 3 Speed
    canvas.fillRoundRect(650, 24, 600, 286, 12, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(650, 24, 600, 286, 12, UiColors::COLOR_PRIMARY);

    uint16_t spdColor = (ctx.gnss.speedKmh >= 120.0f) ? UiColors::COLOR_DANGER
                      : ((ctx.gnss.speedKmh >= 100.0f) ? UiColors::COLOR_WARN : UiColors::COLOR_PRIMARY);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(spdColor);
    canvas.setTextSize(7); // Huge 120pt-equivalent digital speedometer
    char spdStr[16];
    snprintf(spdStr, sizeof(spdStr), "%d", (int)roundf(ctx.gnss.speedKmh));
    canvas.drawString(spdStr, 950, 50);

    canvas.setTextSize(3);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString("km/h", 950, 185);

    // Dynamic acceleration light bar
    int barW = 520;
    int fillW = (int)((ctx.gnss.speedKmh / 160.0f) * barW);
    if (fillW > barW) fillW = barW;
    canvas.fillRoundRect(690, 235, barW, 22, 6, UiColors::COLOR_CARD_BG);
    if (fillW > 0) {
        canvas.fillRoundRect(690, 235, fillW, 22, 6, spdColor);
    }
    canvas.drawRoundRect(690, 235, barW, 22, 6, UiColors::COLOR_BORDER);

    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString("[Tap Speedometer -> Speed Curve & Max Details]", 950, 275);

    // 2. Real-time Altitude Card (X: 650..1250, Y: 326..455) -> Tap jumps to Page 2 Altitude
    canvas.fillRoundRect(650, 326, 600, 128, 12, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(650, 326, 600, 128, 12, UiColors::COLOR_BORDER);
    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.drawString("ALTITUDE (Baro + GNSS Fused)", 675, 342);
    canvas.setTextSize(4);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    char altStr[32];
    snprintf(altStr, sizeof(altStr), "%.1f m", ctx.trip.currentAltitudeM);
    canvas.drawString(altStr, 675, 382);

    canvas.setTextDatum(top_right);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    char climbStr[32];
    snprintf(climbStr, sizeof(climbStr), "Climb: +%.0f m", ctx.trip.cumulativeClimbM);
    canvas.drawString(climbStr, 1225, 382);
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString("[Tap -> 1H Elevation Profile]", 1225, 422);

    // 3. TRIP Distance & Duration Card (X: 650..1250, Y: 470..690) -> Hold 2s Full System Reset
    canvas.fillRoundRect(650, 470, 600, 220, 12, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(650, 470, 600, 220, 12, UiColors::COLOR_SECONDARY);
    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.drawString("CURRENT TRIP (Hold 2s to Full Reset)", 675, 490);

    canvas.setTextSize(4);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    char tripDistStr[32];
    snprintf(tripDistStr, sizeof(tripDistStr), "%.2f km", ctx.trip.tripDistanceKm);
    canvas.drawString(tripDistStr, 675, 535);

    uint32_t totSec = ctx.trip.movingDurationSec + ctx.trip.idleDurationSec;
    uint32_t hh = totSec / 3600;
    uint32_t mm = (totSec % 3600) / 60;
    uint32_t ss = totSec % 60;
    char durStr[48];
    snprintf(durStr, sizeof(durStr), "Drive Time: %02u:%02u:%02u  |  Avg: %.1f km/h", hh, mm, ss, ctx.trip.avgSpeedKmh);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.drawString(durStr, 675, 605);

    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString("Long-press TRIP card for 2.0s to zero Trip, Altitude, Speed, Trail & GPX Segment", 675, 655);

    // Full Trip Reset Toast Banner Overlay
    if (ctx.tripResetBannerActive && millis() < ctx.tripResetBannerExpireMs) {
        canvas.fillRoundRect(420, 310, 600, 90, 14, 0x0208);
        canvas.drawRoundRect(420, 310, 600, 90, 14, UiColors::COLOR_PRIMARY);
        canvas.setTextDatum(middle_center);
        canvas.setTextSize(3);
        canvas.setTextColor(UiColors::COLOR_PRIMARY);
        canvas.drawString("ALL TRIP DATA RESET!", 720, 355);
    }

    endViewport(canvas);
}

bool Page1Dashboard::onTouchTap(int16_t x, int16_t y, SystemContext& ctx) {
    _resetTriggeredThisHold = false;
    // Tap Mini G-Force card (X: 190..610, Y: 480..690) -> Page 5 Dynamics
    if (x >= 190 && x <= 610 && y >= 480 && y <= 690) {
        ctx.currentPage = PAGE_5_DYNAMICS;
        return true;
    }
    // Tap Speedometer card (X: 650..1250, Y: 24..310) -> Page 3 Speed
    if (x >= 650 && x <= 1250 && y >= 24 && y <= 310) {
        ctx.currentPage = PAGE_3_SPEED;
        return true;
    }
    // Tap Altitude card (X: 650..1250, Y: 326..455) -> Page 2 Altitude
    if (x >= 650 && x <= 1250 && y >= 326 && y <= 455) {
        ctx.currentPage = PAGE_2_ALTITUDE;
        return true;
    }
    return false;
}

void Page1Dashboard::onTouchHold(int16_t x, int16_t y, uint32_t holdMs, SystemContext& ctx) {
    // Check TRIP hitbox (X: 650..1250, Y: 460..690)
    if (x >= 650 && x <= 1250 && y >= 460 && y <= 690) {
        if (holdMs >= TripResetConfig::LONG_PRESS_MS && !_resetTriggeredThisHold) {
            _resetTriggeredThisHold = true;
            HapticDriver::instance().triggerShortPulse(TripResetConfig::VIBRATION_FEEDBACK_MS);
            SystemState::instance().triggerFullTripReset();
        }
    } else {
        _resetTriggeredThisHold = false;
    }
}
