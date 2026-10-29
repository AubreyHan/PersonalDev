#include "ui/pages/Page1Dashboard.h"
#include "driver/PowerDriver.h"
#include "config.h"
#include <math.h>

void Page1Dashboard::drawCompass(M5Canvas& canvas, float headingDeg) {
    int cx = 72;
    int cy = 92;
    int r = 44;

    // Dial background & border
    canvas.fillCircle(cx, cy, r, UiColors::COLOR_PANEL_BG);
    canvas.drawCircle(cx, cy, r, UiColors::COLOR_BORDER);
    canvas.drawCircle(cx, cy, r - 4, UiColors::COLOR_BORDER);

    // Cardinal points
    canvas.setTextColor(UiColors::COLOR_DANGER, UiColors::COLOR_PANEL_BG);
    canvas.drawCenterString("N", cx, cy - r + 6);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
    canvas.drawCenterString("S", cx, cy + r - 14);
    canvas.drawCenterString("W", cx - r + 8, cy - 4);
    canvas.drawCenterString("E", cx + r - 8, cy - 4);

    // Rotating pointer needle
    float rad = headingDeg * 0.01745329f;
    int nx = cx + (int)((r - 12) * sin(rad));
    int ny = cy - (int)((r - 12) * cos(rad));
    int sx = cx - (int)((r - 18) * sin(rad));
    int sy = cy + (int)((r - 18) * cos(rad));

    canvas.drawLine(cx, cy, nx, ny, UiColors::COLOR_DANGER);
    canvas.drawLine(cx, cy, sx, sy, UiColors::COLOR_WHITE);
    canvas.fillCircle(cx, cy, 3, UiColors::COLOR_PRIMARY);

    // Heading text below dial
    char headTxt[16];
    const char* card = "N";
    if (headingDeg >= 22.5 && headingDeg < 67.5) card = "NE";
    else if (headingDeg >= 67.5 && headingDeg < 112.5) card = "E";
    else if (headingDeg >= 112.5 && headingDeg < 157.5) card = "SE";
    else if (headingDeg >= 157.5 && headingDeg < 202.5) card = "S";
    else if (headingDeg >= 202.5 && headingDeg < 247.5) card = "SW";
    else if (headingDeg >= 247.5 && headingDeg < 292.5) card = "W";
    else if (headingDeg >= 292.5 && headingDeg < 337.5) card = "NW";

    snprintf(headTxt, sizeof(headTxt), "%3.0f° %s", headingDeg, card);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_BG);
    canvas.drawCenterString(headTxt, cx, 140);
}

void Page1Dashboard::draw(M5Canvas& canvas, const SystemContext& ctx) {
    // 1. Left Half: Compass Dial (X: 8~136, Y: 28~160)
    drawCompass(canvas, ctx.gnss.headingDeg);

    // Vertical Divider Line
    canvas.drawFastVLine(144, 30, 130, UiColors::COLOR_BORDER);

    // 2. Right Half: Balanced Speedometer (X: 148~312, click -> Page 3)
    canvas.fillRoundRect(148, 28, 164, 130, 6, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(148, 28, 164, 130, 6, UiColors::COLOR_BORDER);

    // Big Speed text (44pt)
    char spdBuf[16];
    snprintf(spdBuf, sizeof(spdBuf), "%3.0f", ctx.gnss.speedKmh);
    canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_PANEL_BG);
    canvas.setTextSize(4);
    canvas.drawCenterString(spdBuf, 226, 44);

    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_SECONDARY, UiColors::COLOR_PANEL_BG);
    canvas.drawCenterString("km/h", 226, 96);

    // Sat badge inside speedometer
    canvas.setTextSize(1);
    char satStr[24];
    snprintf(satStr, sizeof(satStr), "Sat: %2d (3D Fix)", ctx.gnss.numSats);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
    canvas.drawCenterString(satStr, 226, 126);

    // Horizontal Divider Line
    canvas.drawFastHLine(8, 166, 304, UiColors::COLOR_BORDER);

    // 3. Bottom Left: Altitude Card (X: 8~144, Y: 172~224, click -> Page 2)
    canvas.fillRoundRect(8, 172, 136, 48, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(8, 172, 136, 48, 4, UiColors::COLOR_BORDER);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
    canvas.drawString("ALTITUDE", 16, 178);

    char altBuf[24];
    snprintf(altBuf, sizeof(altBuf), "%.0f m", ctx.gnss.altitudeMeters);
    canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_PANEL_BG);
    canvas.setTextSize(2);
    canvas.drawString(altBuf, 16, 194);

    // 4. Bottom Right: Trip Mileage Card (X: 152~312, Y: 172~224, long press -> Reset)
    canvas.setTextSize(1);
    canvas.fillRoundRect(152, 172, 160, 48, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(152, 172, 160, 48, 4, UiColors::COLOR_BORDER);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
    canvas.drawString("TRIP MILEAGE", 160, 178);

    char tripBuf[24];
    snprintf(tripBuf, sizeof(tripBuf), "%.1f km", ctx.trip.distanceKm);
    canvas.setTextColor(UiColors::COLOR_SECONDARY, UiColors::COLOR_PANEL_BG);
    canvas.setTextSize(2);
    canvas.drawString(tripBuf, 160, 194);

    // 5. Reset Feedback Dialog if recently triggered
    if (millis() - _resetFeedbackMs < 1500) {
        canvas.fillRoundRect(40, 90, 240, 50, 6, UiColors::COLOR_PANEL_BG);
        canvas.drawRoundRect(40, 90, 240, 50, 6, UiColors::COLOR_PRIMARY);
        canvas.setTextSize(1);
        canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_PANEL_BG);
        canvas.drawCenterString("[ TRIP 行程已全链路重置 ]", 160, 108);
    }
}

void Page1Dashboard::handleTap(int16_t x, int16_t y) {
    SystemState& state = SystemState::instance();
    // Speedometer area tapped (X: 148~312, Y: 28~160) -> Jump to Page 3 (Index 2)
    if (x >= 148 && x <= 312 && y >= 28 && y <= 160) {
        state.lock();
        state.getContextUnsafe().mainPageIdx = 2; // Page 3
        state.unlock();
        return;
    }

    // Altitude card tapped (X: 8~144, Y: 172~224) -> Jump to Page 2 (Index 1)
    if (x >= 8 && x <= 144 && y >= 172 && y <= 224) {
        state.lock();
        state.getContextUnsafe().mainPageIdx = 1; // Page 2
        state.unlock();
        return;
    }
}

void Page1Dashboard::handleLongPress(int16_t x, int16_t y) {
    // Check if long press happened inside TRIP card hitbox (X: 152~312, Y: 172~224)
    if (x >= 152 && x <= 312 && y >= 170 && y <= 230) {
        PowerDriver::instance().vibrate(TripResetConfig::VIBRATION_FEEDBACK_MS);
        _resetFeedbackMs = millis();

        SystemState& state = SystemState::instance();
        state.lock();
        SystemContext& ctx = state.getContextUnsafe();

        // 1. Reset Page 1 TRIP
        ctx.trip.reset(ctx.gnss.altitudeMeters, ctx.gnss.speedKmh, millis() / 1000);

        // 2. Reset Page 4 Trail Start Point
        ctx.trail.reset(ctx.gnss.latE7, ctx.gnss.lonE7, (int16_t)ctx.gnss.altitudeMeters);

        // 3. Reset Page 5 Peak Dynamics G-Forces
        ctx.dynamics.maxLateralG = 0.0f;
        ctx.dynamics.maxLongG = 0.0f;

        state.unlock();
    }
}
