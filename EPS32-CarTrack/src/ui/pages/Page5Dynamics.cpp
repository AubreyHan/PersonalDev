#include "ui/pages/Page5Dynamics.h"
#include <math.h>

void Page5Dynamics::render(M5Canvas& canvas, const SystemContext& ctx) {
    beginViewport(canvas);

    // Record 0.5s G-Force trail
    _trailGx[_trailIdx] = ctx.imu.lateralG;
    _trailGy[_trailIdx] = ctx.imu.longitudinalG;
    _trailIdx = (_trailIdx + 1) % 15;

    // Left Half: 400px Large G-Force Friction Circle (X: 185..655, Y: 20..695)
    canvas.fillRoundRect(185, 20, 470, 675, 12, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(185, 20, 470, 675, 12, UiColors::COLOR_BORDER);

    int gCx = 420, gCy = 265, gR = 195; // 390~400px diameter target
    canvas.drawCircle(gCx, gCy, gR, UiColors::COLOR_PRIMARY);       // 1.5G outer ring
    canvas.drawCircle(gCx, gCy, gR * 2 / 3, UiColors::COLOR_BORDER);// 1.0G middle ring
    canvas.drawCircle(gCx, gCy, gR / 3, UiColors::COLOR_BORDER);    // 0.5G inner ring
    canvas.drawFastHLine(gCx - gR, gCy, gR * 2, UiColors::COLOR_BORDER);
    canvas.drawFastVLine(gCx, gCy - gR, gR * 2, UiColors::COLOR_BORDER);

    // Draw trailing dots
    for (int i = 0; i < 15; i++) {
        int px = gCx + (int)((_trailGx[i] / 1.5f) * gR);
        int py = gCy - (int)((_trailGy[i] / 1.5f) * gR);
        canvas.fillCircle(px, py, 3, UiColors::COLOR_MUTED);
    }

    // Current G red glowing dot
    int curX = gCx + (int)((ctx.imu.lateralG / 1.5f) * gR);
    int curY = gCy - (int)((ctx.imu.longitudinalG / 1.5f) * gR);
    curX = constrain(curX, gCx - gR, gCx + gR);
    curY = constrain(curY, gCy - gR, gCy + gR);
    canvas.fillCircle(curX, curY, 10, UiColors::COLOR_DANGER);
    canvas.drawCircle(curX, curY, 13, UiColors::COLOR_SECONDARY);

    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    char gNow[48], gPeakLat[48], gPeakLon[48];
    snprintf(gNow,     sizeof(gNow),     "Combined G-Force : %.2f G", ctx.imu.totalG);
    snprintf(gPeakLat, sizeof(gPeakLat), "Peak Lateral G   : %.2f G", ctx.trip.maxLateralG);
    snprintf(gPeakLon, sizeof(gPeakLon), "Peak Longitud. G : %.2f G", ctx.trip.maxLongitudinalG);
    canvas.drawString(gNow,     215, 505);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.drawString(gPeakLat, 215, 555);
    canvas.setTextColor(UiColors::COLOR_WARN);
    canvas.drawString(gPeakLon, 215, 605);

    // Right Half: Road Slope % + Off-Road Pitch & Roll Horizon (X: 675..1255, Y: 20..695)
    // 1. Road Slope Card
    bool steepSlope = (fabsf(ctx.trip.roadSlopePercent) >= 7.0f);
    canvas.fillRoundRect(675, 20, 580, 165, 12, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(675, 20, 580, 165, 12, steepSlope ? UiColors::COLOR_WARN : UiColors::COLOR_PRIMARY);

    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString("REAL-TIME ROAD SLOPE", 700, 38);
    canvas.setTextSize(5);
    canvas.setTextColor(steepSlope ? UiColors::COLOR_WARN : UiColors::COLOR_PRIMARY);
    char slopeStr[32];
    snprintf(slopeStr, sizeof(slopeStr), "%+.1f %%", ctx.trip.roadSlopePercent);
    canvas.drawString(slopeStr, 700, 78);

    if (steepSlope) {
        canvas.setTextDatum(top_right);
        canvas.setTextSize(2);
        canvas.setTextColor(UiColors::COLOR_DANGER);
        canvas.drawString("STEEP GRADE ALERT!", 1230, 95);
    }

    // 2. Pitch & Roll Card
    canvas.fillRoundRect(675, 200, 580, 310, 12, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(675, 200, 580, 310, 12, UiColors::COLOR_BORDER);
    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.drawString("VEHICLE PITCH & ROLL HORIZON", 700, 220);

    canvas.setTextSize(4);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    char pitchStr[32], rollStr[32];
    snprintf(pitchStr, sizeof(pitchStr), "Pitch: %+.1f deg", ctx.imu.pitchDeg);
    snprintf(rollStr,  sizeof(rollStr),  "Roll : %+.1f deg", ctx.imu.rollDeg);
    canvas.drawString(pitchStr, 700, 280);
    canvas.drawString(rollStr,  700, 360);

    // Artificial Horizon Indicator
    int hCx = 1135, hCy = 345, hR = 80;
    canvas.drawCircle(hCx, hCy, hR, UiColors::COLOR_PRIMARY);
    float rollRad = ctx.imu.rollDeg * 0.0174532925f;
    int lx = (int)(cosf(rollRad) * (hR - 10));
    int ly = (int)(sinf(rollRad) * (hR - 10));
    canvas.drawLine(hCx - lx, hCy - ly, hCx + lx, hCy + ly, UiColors::COLOR_SECONDARY);

    // 3. Dual-IMU Redundancy Status Card
    canvas.fillRoundRect(675, 525, 580, 170, 12, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(675, 525, 580, 170, 12, UiColors::COLOR_BORDER);
    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.drawString(ctx.imu.isDualFused ? "Dual BMI270 (0x68+0x69): 50Hz Fused (-30% Noise)"
                                          : (ctx.imu.failoverActive ? "IMU Failover: Slave 0x69 Active" : "Single BMI270 Mode"),
                      700, 550);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString("Static Anti-Drift: Zero-Velocity Update (ZUPT)", 700, 595);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString("MagSafe Immunity: BMM150 Bypassed", 700, 640);

    endViewport(canvas);
}
