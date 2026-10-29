#include "ui/pages/Page5Dynamics.h"
#include "config.h"
#include <math.h>

void Page5Dynamics::draw(M5Canvas& canvas, const SystemContext& ctx) {
    // 1. Left Half: G-Force Friction Circle (Center: X 74, Y 105, Radius 42)
    int cx = 74;
    int cy = 105;
    int r = 42;

    canvas.fillCircle(cx, cy, r, UiColors::COLOR_PANEL_BG);
    canvas.drawCircle(cx, cy, r, UiColors::COLOR_BORDER);      // 1.0G outer
    canvas.drawCircle(cx, cy, r / 2, UiColors::COLOR_BORDER);  // 0.5G inner
    canvas.drawFastHLine(cx - r, cy, r * 2, UiColors::COLOR_BORDER);
    canvas.drawFastVLine(cx, cy - r, r * 2, UiColors::COLOR_BORDER);

    // Dynamic red dot based on lateralG and longG
    // latG: +Right, -Left
    // longG: +Braking (forward), -Accel (backward)
    int dotX = cx + (int)(ctx.dynamics.lateralG * (r / 1.0f));
    int dotY = cy - (int)(ctx.dynamics.longG * (r / 1.0f));

    // Clamp dot inside circle
    float dist = sqrtf((dotX - cx) * (dotX - cx) + (dotY - cy) * (dotY - cy));
    if (dist > r - 4) {
        float scale = (r - 4) / dist;
        dotX = cx + (int)((dotX - cx) * scale);
        dotY = cy + (int)((dotY - cy) * scale);
    }
    canvas.fillCircle(dotX, dotY, 4, UiColors::COLOR_DANGER);

    // G-Force stats below circle
    char totalGBuf[24];
    snprintf(totalGBuf, sizeof(totalGBuf), "%.2f G", ctx.dynamics.totalG);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_BG);
    canvas.drawCenterString(totalGBuf, cx, 154);

    canvas.setTextSize(1);
    char peakGBuf[32];
    snprintf(peakGBuf, sizeof(peakGBuf), "MAX L%.2f B%.2f", ctx.dynamics.maxLateralG, ctx.dynamics.maxLongG);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
    canvas.drawCenterString(peakGBuf, cx, 174);

    // Vertical Divider
    canvas.drawFastVLine(148, 28, 185, UiColors::COLOR_BORDER);

    // 2. Right Half: Inclinometer & Road Slope (X: 156~312)
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
    canvas.drawString("VEHICLE ATTITUDE", 156, 28);

    // 2.1 Road Slope Card
    canvas.fillRoundRect(156, 46, 156, 50, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(156, 46, 156, 50, 4, UiColors::COLOR_BORDER);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
    canvas.drawString("道路坡度", 164, 52);

    char slopeBuf[24];
    snprintf(slopeBuf, sizeof(slopeBuf), "%+.1f %%", ctx.dynamics.roadSlopePercent);
    uint16_t slopeColor = (fabs(ctx.dynamics.roadSlopePercent) > 6.0f) ? UiColors::COLOR_WARN : UiColors::COLOR_PRIMARY;
    canvas.setTextColor(slopeColor, UiColors::COLOR_PANEL_BG);
    canvas.setTextSize(2);
    canvas.drawString(slopeBuf, 164, 68);

    // 2.2 Pitch & Roll Card
    canvas.setTextSize(1);
    canvas.fillRoundRect(156, 104, 156, 75, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(156, 104, 156, 75, 4, UiColors::COLOR_BORDER);

    // Left Col: Pitch
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
    canvas.drawString("PITCH 俯仰", 164, 110);
    char pitchBuf[16];
    snprintf(pitchBuf, sizeof(pitchBuf), "%+.1f°", ctx.dynamics.pitchDeg);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_WHITE, UiColors::COLOR_PANEL_BG);
    canvas.drawString(pitchBuf, 164, 126);

    // Right Col: Roll
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
    canvas.drawString("ROLL 侧倾", 240, 110);
    char rollBuf[16];
    snprintf(rollBuf, sizeof(rollBuf), "%+.1f°", ctx.dynamics.rollDeg);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_WHITE, UiColors::COLOR_PANEL_BG);
    canvas.drawString(rollBuf, 240, 126);

    // Subtext
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_PANEL_BG);
    canvas.drawString("姿态自校准: 零偏跟踪", 164, 152);

    // Bottom Mode Info
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
    canvas.drawString("传感器: BMI270 6-Axis + BMP280", 156, 195);
}
