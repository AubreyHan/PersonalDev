#include "Page5Dynamics.h"
#include <stdio.h>
#include <math.h>
#include <type_traits>

static_assert(!std::is_abstract<Page5Dynamics>::value && std::is_default_constructible<Page5Dynamics>::value,
              "Page5Dynamics must be concrete and default-constructible");

namespace {

constexpr float kDegToRad = 0.01745329251994329577f;

uint32_t blendRgb888(uint32_t c0, uint32_t c1, float t) {
    if (t <= 0.0f) return c0;
    if (t >= 1.0f) return c1;
    const uint8_t r0 = static_cast<uint8_t>((c0 >> 16) & 0xFFU);
    const uint8_t g0 = static_cast<uint8_t>((c0 >> 8) & 0xFFU);
    const uint8_t b0 = static_cast<uint8_t>(c0 & 0xFFU);
    const uint8_t r1 = static_cast<uint8_t>((c1 >> 16) & 0xFFU);
    const uint8_t g1 = static_cast<uint8_t>((c1 >> 8) & 0xFFU);
    const uint8_t b1 = static_cast<uint8_t>(c1 & 0xFFU);
    const uint8_t r  = static_cast<uint8_t>(r0 + (r1 - r0) * t);
    const uint8_t g  = static_cast<uint8_t>(g0 + (g1 - g0) * t);
    const uint8_t b  = static_cast<uint8_t>(b0 + (b1 - b0) * t);
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

// Clip a line segment p0..p1 against a circle centered at (cx, cy) with radius r.
bool clipLineToCircle(float cx, float cy, float r,
                      float& x0, float& y0, float& x1, float& y1) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float fx = x0 - cx;
    const float fy = y0 - cy;

    const float a = dx * dx + dy * dy;
    if (a < 1e-6f) {
        return (fx * fx + fy * fy) <= (r * r);
    }
    const float b    = 2.0f * (fx * dx + fy * dy);
    const float c    = (fx * fx + fy * fy) - (r * r);
    const float disc = b * b - 4.0f * a * c;
    if (disc <= 0.0f) {
        return false;
    }
    const float sqrtDisc = sqrtf(disc);
    float t0 = (-b - sqrtDisc) / (2.0f * a);
    float t1 = (-b + sqrtDisc) / (2.0f * a);
    if (t0 > t1) {
        const float tmp = t0;
        t0 = t1;
        t1 = tmp;
    }
    if (t1 < 0.0f || t0 > 1.0f) {
        return false;
    }
    if (t0 < 0.0f) t0 = 0.0f;
    if (t1 > 1.0f) t1 = 1.0f;

    const float nx0 = x0 + t0 * dx;
    const float ny0 = y0 + t0 * dy;
    const float nx1 = x0 + t1 * dx;
    const float ny1 = y0 + t1 * dy;
    x0 = nx0;
    y0 = ny0;
    x1 = nx1;
    y1 = ny1;
    return true;
}

} // namespace

void Page5Dynamics::onEnter(const SystemStateSnapshot& state) {
    (void)state;
    _calibPressed = false;
}

void Page5Dynamics::render(M5Canvas& canvas, const SystemStateSnapshot& state) {
    clearViewportCanvas(canvas);
    canvas.setTextSize(1.0f);
    canvas.setTextWrap(false, false);

    drawGForceTarget(canvas, state);
    drawSlopeCard(canvas, state);
    drawAttitudeHorizonCard(canvas, state);
    drawDualImuStatusStrip(canvas, state);
    drawCalibrateButton(canvas, state);
}

void Page5Dynamics::drawGForceTarget(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    // Left panel card (X: 16..516, Y: 14..678, W: 500, H: 664)
    constexpr int16_t cardX = 16;
    constexpr int16_t cardY = 14;
    constexpr int16_t cardW = 500;
    constexpr int16_t cardH = 664;

    canvas.fillRoundRect(cardX, cardY, cardW, cardH, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(cardX, cardY, cardW, cardH, 14, ThemeColors::BORDER_SUBTLE);

    // Header
    canvas.setTextDatum(top_left);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString("动态 G 值摩擦力圆", cardX + 20, cardY + 14, &fonts::efontCN_24_b);

    char rateBadge[24];
    snprintf(rateBadge, sizeof(rateBadge), "%uHz 实时", static_cast<unsigned>(DualImuConfig::SAMPLE_RATE_HZ));
    canvas.fillRoundRect(cardX + cardW - 118, cardY + 14, 98, 28, 8, 0x102822U);
    canvas.drawRoundRect(cardX + cardW - 118, cardY + 14, 98, 28, 8, ThemeColors::NEON_GREEN);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.drawString(rateBadge, cardX + cardW - 69, cardY + 28, &fonts::efontCN_16_b);

    // 400px Diameter Racing G-Force Target (radius = 200px, diameter = 400px)
    constexpr int16_t cx     = 266;
    constexpr int16_t cy     = 286;
    constexpr int16_t rOuter = 200; // 1.5G outer envelope (400px diameter)
    constexpr int16_t r10G   = 133; // 1.0G ring
    constexpr int16_t r05G   = 67;  // 0.5G ring
    constexpr float   pxPerG = static_cast<float>(rOuter) / 1.5f;

    // Subtle dark target backdrop
    canvas.fillCircle(cx, cy, rOuter, 0x05080CU);

    // Radial 30-deg ticks on outer 400px ring
    for (int deg = 0; deg < 360; deg += 30) {
        const float rad = static_cast<float>(deg) * kDegToRad;
        const int16_t x0 = static_cast<int16_t>(lroundf(cx + cosf(rad) * (rOuter - ((deg % 90 == 0) ? 14 : 8))));
        const int16_t y0 = static_cast<int16_t>(lroundf(cy + sinf(rad) * (rOuter - ((deg % 90 == 0) ? 14 : 8))));
        const int16_t x1 = static_cast<int16_t>(lroundf(cx + cosf(rad) * rOuter));
        const int16_t y1 = static_cast<int16_t>(lroundf(cy + sinf(rad) * rOuter));
        canvas.drawLine(x0, y0, x1, y1, (deg % 90 == 0) ? 0x455A64U : 0x243242U);
    }

    // Crosshair axes
    canvas.drawFastHLine(cx - rOuter, cy, rOuter * 2, 0x1E2D3DU);
    canvas.drawFastVLine(cx, cy - rOuter, rOuter * 2, 0x1E2D3DU);

    // Concentric rings: 1.5G (400px dia), 1.0G, 0.5G
    canvas.drawCircle(cx, cy, rOuter, 0x3A4E63U);
    canvas.drawCircle(cx, cy, rOuter - 1, 0x243242U);
    canvas.drawCircle(cx, cy, r10G, ThemeColors::ICE_CYAN);
    canvas.drawCircle(cx, cy, r10G - 1, 0x0A4A5CU);
    canvas.drawCircle(cx, cy, r05G, 0x1B6B52U);

    // Ring & Axis Labels
    canvas.setTextDatum(top_center);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("+制动", cx, cy - rOuter + 6, &fonts::efontCN_14);
    canvas.setTextDatum(bottom_center);
    canvas.drawString("-加速", cx, cy + rOuter - 6, &fonts::efontCN_14);
    canvas.setTextDatum(middle_left);
    canvas.drawString("-左转", cx - rOuter + 8, cy - 12, &fonts::efontCN_14);
    canvas.setTextDatum(middle_right);
    canvas.drawString("+右转", cx + rOuter - 8, cy - 12, &fonts::efontCN_14);

    canvas.setTextDatum(bottom_left);
    canvas.setTextColor(0x2ECC9AU);
    canvas.drawString("0.5G", cx + 6, cy - r05G - 2, &fonts::efontCN_12);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString("1.0G", cx + 6, cy - r10G - 2, &fonts::efontCN_12);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("1.5G", cx + 6, cy - rOuter + 18, &fonts::efontCN_12);

    // Active trip category for peak G memory
    const uint8_t catIdx = (state.tripControl.activeViewCategory < TRIP_CATEGORY_COUNT)
                               ? static_cast<uint8_t>(state.tripControl.activeViewCategory)
                               : static_cast<uint8_t>(TRIP_SINGLE);
    const TripStats& trip = state.trips[catIdx];
    const float peakLatG  = fmaxf(fabsf(trip.maxLateralG), fabsf(state.imu.lateralG));
    const float peakLonG  = fmaxf(fabsf(trip.maxLongitudinalG), fabsf(state.imu.longitudinalG));

    // Draw peak G markers on axes
    if (peakLatG > 0.02f) {
        const int16_t pxOff = static_cast<int16_t>(lroundf(fminf(peakLatG, 1.5f) * pxPerG));
        canvas.fillTriangle(cx + pxOff, cy - 6, cx + pxOff - 4, cy - 13, cx + pxOff + 4, cy - 13, ThemeColors::RACING_YELLOW);
        canvas.fillTriangle(cx - pxOff, cy - 6, cx - pxOff - 4, cy - 13, cx - pxOff + 4, cy - 13, ThemeColors::RACING_YELLOW);
    }
    if (peakLonG > 0.02f) {
        const int16_t pyOff = static_cast<int16_t>(lroundf(fminf(peakLonG, 1.5f) * pxPerG));
        canvas.fillTriangle(cx + 6, cy - pyOff, cx + 13, cy - pyOff - 4, cx + 13, cy - pyOff + 4, ThemeColors::AMBER_WARN);
        canvas.fillTriangle(cx + 6, cy + pyOff, cx + 13, cy + pyOff - 4, cx + 13, cy + pyOff + 4, ThemeColors::AMBER_WARN);
    }

    auto mapGToCanvas = [&](float latG, float lonG, int16_t& outX, int16_t& outY) {
        float dx = latG * pxPerG;
        float dy = -lonG * pxPerG; // +brake up, -accel down
        const float dist = hypotf(dx, dy);
        const float maxR = static_cast<float>(rOuter - 8);
        if (dist > maxR && dist > 1e-4f) {
            const float scale = maxR / dist;
            dx *= scale;
            dy *= scale;
        }
        outX = static_cast<int16_t>(lroundf(cx + dx));
        outY = static_cast<int16_t>(lroundf(cy + dy));
    };

    // 0.5-second Comet Gradient Trail (ImuData::gTrailLat / gTrailLon, 25 samples @ 50Hz)
    const uint8_t count = (state.imu.gTrailCount <= ImuData::G_TRAIL_POINTS)
                              ? state.imu.gTrailCount
                              : static_cast<uint8_t>(ImuData::G_TRAIL_POINTS);
    int16_t prevX = cx;
    int16_t prevY = cy;
    bool hasPrev  = false;

    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t ringIdx = (count < ImuData::G_TRAIL_POINTS)
                                    ? i
                                    : static_cast<uint8_t>((state.imu.gTrailHead + i) % ImuData::G_TRAIL_POINTS);
        int16_t tx = cx;
        int16_t ty = cy;
        mapGToCanvas(state.imu.gTrailLat[ringIdx], state.imu.gTrailLon[ringIdx], tx, ty);

        const float ratio = (count > 1U) ? (static_cast<float>(i) / static_cast<float>(count - 1U)) : 1.0f;
        const uint32_t tailColor = (ratio < 0.6f)
                                       ? blendRgb888(0x06202AU, ThemeColors::ICE_CYAN, ratio / 0.6f)
                                       : blendRgb888(ThemeColors::ICE_CYAN, ThemeColors::NEON_GREEN, (ratio - 0.6f) / 0.4f);
        const int16_t dotR = static_cast<int16_t>(2 + lroundf(ratio * 5.0f));
        if (hasPrev) {
            canvas.drawWideLine(prevX, prevY, tx, ty, fmaxf(1.2f, ratio * 3.5f), tailColor);
        }
        canvas.fillSmoothCircle(tx, ty, dotR, tailColor);
        prevX   = tx;
        prevY   = ty;
        hasPrev = true;
    }

    // Live G-ball puck
    int16_t liveX = cx;
    int16_t liveY = cy;
    mapGToCanvas(state.imu.lateralG, state.imu.longitudinalG, liveX, liveY);
    if (hasPrev) {
        canvas.drawWideLine(prevX, prevY, liveX, liveY, 3.5f, ThemeColors::NEON_GREEN);
    }

    const float combG = (state.imu.combinedG > 0.0f)
                            ? state.imu.combinedG
                            : hypotf(state.imu.lateralG, state.imu.longitudinalG);
    const uint32_t puckColor = (combG >= 0.90f)
                                   ? ThemeColors::ALERT_RED
                                   : ((combG >= 0.50f) ? ThemeColors::RACING_YELLOW : ThemeColors::NEON_GREEN);

    // Center origin crosshair dot
    canvas.fillCircle(cx, cy, 3, 0x546E7AU);
    // Live vector line & glowing puck
    canvas.drawLine(cx, cy, liveX, liveY, 0x2E5866U);
    canvas.fillSmoothCircle(liveX, liveY, 12, blendRgb888(0x000000U, puckColor, 0.35f));
    canvas.fillSmoothCircle(liveX, liveY, 8, puckColor);
    canvas.fillSmoothCircle(liveX, liveY, 3, ThemeColors::TEXT_WHITE);

    // Bottom Telemetry Readouts inside Left Panel (Y: 500..666)
    canvas.drawFastHLine(cardX + 18, 502, cardW - 36, ThemeColors::BORDER_SUBTLE);

    // Combined G + Instantaneous Lat/Lon G
    canvas.setTextDatum(top_left);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("实时合成 G 值", cardX + 22, 514, &fonts::efontCN_16);

    char combStr[24];
    snprintf(combStr, sizeof(combStr), "%.2f", combG);
    canvas.setTextColor(puckColor);
    canvas.drawString(combStr, cardX + 22, 540, &fonts::DejaVu40);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("G", cardX + 132, 554, &fonts::efontCN_24_b);

    char instBuf[48];
    snprintf(instBuf, sizeof(instBuf), "横 %+.2fG  |  纵 %+.2fG", state.imu.lateralG, state.imu.longitudinalG);
    canvas.setTextDatum(top_right);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString(instBuf, cardX + cardW - 22, 518, &fonts::efontCN_16);

    // Peak Lateral & Peak Longitudinal Memory Boxes
    const int16_t boxY = 594;
    const int16_t boxW = 222;
    const int16_t boxH = 70;

    canvas.fillRoundRect(cardX + 20, boxY, boxW, boxH, 10, 0x111A24U);
    canvas.drawRoundRect(cardX + 20, boxY, boxW, boxH, 10, 0x2C3E50U);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("极限横向 G (过弯)", cardX + 32, boxY + 8, &fonts::efontCN_14);
    char maxLatStr[24];
    snprintf(maxLatStr, sizeof(maxLatStr), "%.2f G", peakLatG);
    canvas.setTextColor(ThemeColors::RACING_YELLOW);
    canvas.drawString(maxLatStr, cardX + 32, boxY + 32, &fonts::efontCN_24_b);

    canvas.fillRoundRect(cardX + 258, boxY, boxW, boxH, 10, 0x111A24U);
    canvas.drawRoundRect(cardX + 258, boxY, boxW, boxH, 10, 0x2C3E50U);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("极限纵向 G (制动/推背)", cardX + 270, boxY + 8, &fonts::efontCN_14);
    char maxLonStr[24];
    snprintf(maxLonStr, sizeof(maxLonStr), "%.2f G", peakLonG);
    canvas.setTextColor(ThemeColors::AMBER_WARN);
    canvas.drawString(maxLonStr, cardX + 270, boxY + 32, &fonts::efontCN_24_b);
}

void Page5Dynamics::drawSlopeCard(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    // Top-right card: Real-time Road Slope % (X: 532..1104, Y: 14..166, W: 572, H: 152)
    constexpr int16_t x = 532;
    constexpr int16_t y = 14;
    constexpr int16_t w = 572;
    constexpr int16_t h = 152;

    const float slopePct  = state.baro.roadSlopePercent;
    const bool  steepWarn = state.baro.steepSlopeWarning ||
                            (fabsf(slopePct) >= BaroFusionConfig::STEEP_SLOPE_WARN_PERCENT);
    const bool  flashOn   = ((millis() / 320U) % 2U) == 0U;

    const uint32_t cardBg = (steepWarn && flashOn) ? 0x3A0B0AU : ThemeColors::BG_CARD_DARK;
    const uint32_t border = steepWarn ? (flashOn ? ThemeColors::ALERT_RED : ThemeColors::AMBER_WARN)
                                      : ThemeColors::BORDER_SUBTLE;

    canvas.fillRoundRect(x, y, w, h, 14, cardBg);
    canvas.drawRoundRect(x, y, w, h, 14, border);
    if (steepWarn) {
        canvas.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 13, border);
    }

    canvas.setTextDatum(top_left);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString("实时道路坡度", x + 22, y + 14, &fonts::efontCN_24_b);

    // Baro + GNSS fusion badge
    canvas.setTextDatum(top_right);
    canvas.setTextColor(state.baro.isOnline ? ThemeColors::ICE_CYAN : ThemeColors::TEXT_MUTED);
    char baroInfo[48];
    snprintf(baroInfo, sizeof(baroInfo), "BMP280 %.1f hPa | 升降 %+.1fm/s",
             state.baro.pressureHpa, state.baro.verticalSpeedMps);
    canvas.drawString(baroInfo, x + w - 20, y + 18, &fonts::efontCN_14);

    // Huge Slope Percentage Value
    char slopeNum[24];
    snprintf(slopeNum, sizeof(slopeNum), "%+.1f", slopePct);
    const uint32_t valColor = steepWarn
                                  ? ThemeColors::ALERT_RED
                                  : ((fabsf(slopePct) >= 4.0f) ? ThemeColors::RACING_YELLOW : ThemeColors::NEON_GREEN);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(valColor);
    canvas.drawString(slopeNum, x + 22, y + 54, &fonts::DejaVu56);

    const int32_t numW = canvas.textWidth(slopeNum, &fonts::DejaVu56);
    canvas.drawString("%", x + 30 + numW, y + 70, &fonts::DejaVu40);

    // Steep Slope Flashing Red Warning Pill (>= 7.0%) or Normal Road Grade Status
    const int16_t badgeX = x + 265;
    const int16_t badgeY = y + 58;
    const int16_t badgeW = w - 285;
    const int16_t badgeH = 74;

    if (steepWarn) {
        const uint32_t pillBg = flashOn ? ThemeColors::ALERT_RED : 0x6E1410U;
        canvas.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 10, pillBg);
        canvas.drawRoundRect(badgeX, badgeY, badgeW, badgeH, 10, ThemeColors::TEXT_WHITE);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(ThemeColors::TEXT_WHITE);
        canvas.drawString(slopePct >= 0.0f ? "! 大陡坡爬升预警 (>=7%)" : "! 大陡坡缓降警告 (>=7%)",
                          badgeX + badgeW / 2, badgeY + 24, &fonts::efontCN_16_b);
        canvas.drawString("请低挡控速·谨防制动热衰减", badgeX + badgeW / 2, badgeY + 52, &fonts::efontCN_14);
    } else {
        canvas.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 10, 0x111B26U);
        canvas.drawRoundRect(badgeX, badgeY, badgeW, badgeH, 10, 0x243242U);
        canvas.setTextDatum(middle_center);
        const char* gradeDesc = "平缓巡航路段 (<2.0%)";
        uint32_t    descColor = ThemeColors::NEON_GREEN;
        if (slopePct >= 4.0f) {
            gradeDesc = "中度上坡路段";
            descColor = ThemeColors::RACING_YELLOW;
        } else if (slopePct <= -4.0f) {
            gradeDesc = "中度下坡路段";
            descColor = ThemeColors::RACING_YELLOW;
        } else if (slopePct >= 2.0f) {
            gradeDesc = "缓坡爬升路段";
        } else if (slopePct <= -2.0f) {
            gradeDesc = "缓坡滑行路段";
        }
        canvas.setTextColor(descColor);
        canvas.drawString(gradeDesc, badgeX + badgeW / 2, badgeY + 25, &fonts::efontCN_16_b);
        canvas.setTextColor(ThemeColors::TEXT_MUTED);
        canvas.drawString("气压定高 + GNSS 互补坡度引擎", badgeX + badgeW / 2, badgeY + 52, &fonts::efontCN_14);
    }
}

void Page5Dynamics::drawAttitudeHorizonCard(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    // Middle-right card: Off-Road Artificial Horizon + Pitch / Roll (X: 532..1104, Y: 178..504, W: 572, H: 326)
    constexpr int16_t x = 532;
    constexpr int16_t y = 178;
    constexpr int16_t w = 572;
    constexpr int16_t h = 326;

    canvas.fillRoundRect(x, y, w, h, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(x, y, w, h, 14, ThemeColors::BORDER_SUBTLE);

    canvas.setTextDatum(top_left);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString("越野姿态人工地平仪", x + 22, y + 14, &fonts::efontCN_24_b);

    // ZUPT status pill in top-right of card
    const bool zupt = state.imu.zuptLocked;
    canvas.fillRoundRect(x + w - 210, y + 14, 190, 28, 8, zupt ? 0x0E2920U : 0x0E2233U);
    canvas.drawRoundRect(x + w - 210, y + 14, 190, 28, 8, zupt ? ThemeColors::NEON_GREEN : ThemeColors::ICE_CYAN);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(zupt ? ThemeColors::NEON_GREEN : ThemeColors::ICE_CYAN);
    canvas.drawString(zupt ? "静态零速锁 ZUPT" : "动态姿态解算中", x + w - 115, y + 28, &fonts::efontCN_16_b);

    // Artificial Horizon Sphere on left half of card
    constexpr int16_t hCx = 682;
    constexpr int16_t hCy = 354;
    constexpr int16_t hR  = 114;

    const float pitchDeg = fmaxf(-45.0f, fminf(45.0f, state.imu.pitchDeg));
    const float rollDeg  = fmaxf(-45.0f, fminf(45.0f, state.imu.rollDeg));
    const float rollRad  = -rollDeg * kDegToRad;
    const float cosR     = cosf(rollRad);
    const float sinR     = sinf(rollRad);
    const float pxPerDeg = 2.1f; // ~2.1 px per degree of pitch

    // Fill scanlines inside circular horizon viewport cleanly (sky vs ground split by rotated horizon line)
    const float nx     = -sinR;
    const float ny     = cosR;
    const float pitchO = pitchDeg * pxPerDeg; // +pitch (nose up) shifts horizon line down (+Y)

    for (int16_t dy = -(hR - 3); dy <= (hR - 3); ++dy) {
        const int32_t spanSq = (hR - 3) * (hR - 3) - dy * dy;
        if (spanSq <= 0) continue;
        const int16_t halfW = static_cast<int16_t>(sqrtf(static_cast<float>(spanSq)));
        const int16_t xMin  = -halfW;
        const int16_t xMax  = halfW;

        // Signed distance to horizon: d(dx) = dx*nx + dy*ny - pitchO (d < 0 => Sky, d >= 0 => Ground)
        const uint32_t skyCol = 0x082238U;
        const uint32_t gndCol = 0x261A0CU;

        if (fabsf(nx) < 1e-4f) {
            const float d = dy * ny - pitchO;
            canvas.drawFastHLine(hCx + xMin, hCy + dy, xMax - xMin + 1, (d < 0.0f) ? skyCol : gndCol);
        } else {
            const float splitDx = (pitchO - dy * ny) / nx;
            const int16_t splitX = static_cast<int16_t>(lroundf(splitDx));
            const uint32_t leftCol  = (nx > 0.0f) ? skyCol : gndCol;
            const uint32_t rightCol = (nx > 0.0f) ? gndCol : skyCol;
            if (splitX <= xMin) {
                canvas.drawFastHLine(hCx + xMin, hCy + dy, xMax - xMin + 1, rightCol);
            } else if (splitX >= xMax) {
                canvas.drawFastHLine(hCx + xMin, hCy + dy, xMax - xMin + 1, leftCol);
            } else {
                canvas.drawFastHLine(hCx + xMin, hCy + dy, splitX - xMin, leftCol);
                canvas.drawFastHLine(hCx + splitX, hCy + dy, xMax - splitX + 1, rightCol);
            }
        }
    }

    // Pitch ladder lines (-20, -10, 0, +10, +20 deg) clipped mathematically to circle
    for (int pStep = -30; pStep <= 30; pStep += 10) {
        const float offsetPx = (pitchDeg - static_cast<float>(pStep)) * pxPerDeg;
        const float halfSpan = (pStep == 0) ? 140.0f : ((pStep % 20 == 0) ? 38.0f : 22.0f);

        // Center of ladder rung in rotated frame: (0, offsetPx) -> world: (-sinR*offsetPx, cosR*offsetPx)
        const float mx = hCx + (-sinR) * offsetPx;
        const float my = hCy + ( cosR) * offsetPx;
        float lx0 = mx - cosR * halfSpan;
        float ly0 = my - sinR * halfSpan;
        float lx1 = mx + cosR * halfSpan;
        float ly1 = my + sinR * halfSpan;

        if (clipLineToCircle(hCx, hCy, static_cast<float>(hR - 5), lx0, ly0, lx1, ly1)) {
            if (pStep == 0) {
                canvas.drawWideLine(static_cast<int32_t>(lroundf(lx0)), static_cast<int32_t>(lroundf(ly0)),
                                    static_cast<int32_t>(lroundf(lx1)), static_cast<int32_t>(lroundf(ly1)),
                                    2.2f, ThemeColors::NEON_GREEN);
            } else {
                canvas.drawLine(static_cast<int32_t>(lroundf(lx0)), static_cast<int32_t>(lroundf(ly0)),
                                static_cast<int32_t>(lroundf(lx1)), static_cast<int32_t>(lroundf(ly1)),
                                0x8BA4B8U);
            }
        }
    }

    // Fixed aircraft/vehicle wing reference reticle in center (Racing Yellow)
    canvas.drawWideLine(hCx - 48, hCy, hCx - 16, hCy, 2.5f, ThemeColors::RACING_YELLOW);
    canvas.drawWideLine(hCx - 16, hCy, hCx - 16, hCy + 8, 2.5f, ThemeColors::RACING_YELLOW);
    canvas.drawWideLine(hCx + 16, hCy, hCx + 48, hCy, 2.5f, ThemeColors::RACING_YELLOW);
    canvas.drawWideLine(hCx + 16, hCy, hCx + 16, hCy + 8, 2.5f, ThemeColors::RACING_YELLOW);
    canvas.fillSmoothCircle(hCx, hCy, 4, ThemeColors::RACING_YELLOW);

    // Outer Bezel & Roll Angle Scale Ticks (-45, -30, -15, 0, +15, +30, +45 at top)
    canvas.drawCircle(hCx, hCy, hR, 0x4A627AU);
    canvas.drawCircle(hCx, hCy, hR - 1, ThemeColors::ICE_CYAN);
    for (int bank = -45; bank <= 45; bank += 15) {
        const float aRad = (-90.0f + static_cast<float>(bank)) * kDegToRad;
        const int16_t tx0 = static_cast<int16_t>(lroundf(hCx + cosf(aRad) * (hR - 10)));
        const int16_t ty0 = static_cast<int16_t>(lroundf(hCy + sinf(aRad) * (hR - 10)));
        const int16_t tx1 = static_cast<int16_t>(lroundf(hCx + cosf(aRad) * (hR - 2)));
        const int16_t ty1 = static_cast<int16_t>(lroundf(hCy + sinf(aRad) * (hR - 2)));
        canvas.drawLine(tx0, ty0, tx1, ty1, (bank == 0) ? ThemeColors::RACING_YELLOW : ThemeColors::TEXT_WHITE);
    }
    // Live Roll Pointer Triangle at top arc
    const float rollPtrRad = (-90.0f + rollDeg) * kDegToRad;
    const int16_t pxTip = static_cast<int16_t>(lroundf(hCx + cosf(rollPtrRad) * (hR - 12)));
    const int16_t pyTip = static_cast<int16_t>(lroundf(hCy + sinf(rollPtrRad) * (hR - 12)));
    const int16_t pxL   = static_cast<int16_t>(lroundf(hCx + cosf(rollPtrRad - 0.06f) * (hR - 3)));
    const int16_t pyL   = static_cast<int16_t>(lroundf(hCy + sinf(rollPtrRad - 0.06f) * (hR - 3)));
    const int16_t pxR   = static_cast<int16_t>(lroundf(hCx + cosf(rollPtrRad + 0.06f) * (hR - 3)));
    const int16_t pyR   = static_cast<int16_t>(lroundf(hCy + sinf(rollPtrRad + 0.06f) * (hR - 3)));
    canvas.fillTriangle(pxTip, pyTip, pxL, pyL, pxR, pyR, ThemeColors::NEON_GREEN);

    // Right-half Pitch & Roll Digital Readout Boxes (X: 816..1086)
    const int16_t rBoxX = 818;
    const int16_t rBoxW = 268;

    // 1) Pitch Readout
    const uint32_t pitchColor = (fabsf(state.imu.pitchDeg) >= 20.0f)
                                    ? ThemeColors::ALERT_RED
                                    : ((fabsf(state.imu.pitchDeg) >= 10.0f) ? ThemeColors::AMBER_WARN : ThemeColors::NEON_GREEN);
    canvas.fillRoundRect(rBoxX, y + 56, rBoxW, 104, 10, 0x111A24U);
    canvas.drawRoundRect(rBoxX, y + 56, rBoxW, 104, 10, 0x283848U);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("俯仰角 Pitch", rBoxX + 14, y + 64, &fonts::efontCN_16);

    const char* pitchDir = (state.imu.pitchDeg > 0.6f) ? "车头抬起" : ((state.imu.pitchDeg < -0.6f) ? "车头下压" : "车身水平");
    canvas.setTextDatum(top_right);
    canvas.setTextColor(pitchColor);
    canvas.drawString(pitchDir, rBoxX + rBoxW - 14, y + 64, &fonts::efontCN_16_b);

    char pitchVal[24];
    snprintf(pitchVal, sizeof(pitchVal), "%+.1f", state.imu.pitchDeg);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(pitchColor);
    canvas.drawString(pitchVal, rBoxX + 14, y + 94, &fonts::DejaVu40);
    canvas.drawString("deg", rBoxX + 158, y + 112, &fonts::efontCN_16);

    // 2) Roll Readout
    const uint32_t rollColor = (fabsf(state.imu.rollDeg) >= 20.0f)
                                   ? ThemeColors::ALERT_RED
                                   : ((fabsf(state.imu.rollDeg) >= 10.0f) ? ThemeColors::AMBER_WARN : ThemeColors::ICE_CYAN);
    canvas.fillRoundRect(rBoxX, y + 172, rBoxW, 104, 10, 0x111A24U);
    canvas.drawRoundRect(rBoxX, y + 172, rBoxW, 104, 10, 0x283848U);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("横滚角 Roll", rBoxX + 14, y + 180, &fonts::efontCN_16);

    const char* rollDir = (state.imu.rollDeg > 0.6f) ? "车身右倾" : ((state.imu.rollDeg < -0.6f) ? "车身左倾" : "左右水平");
    canvas.setTextDatum(top_right);
    canvas.setTextColor(rollColor);
    canvas.drawString(rollDir, rBoxX + rBoxW - 14, y + 180, &fonts::efontCN_16_b);

    char rollVal[24];
    snprintf(rollVal, sizeof(rollVal), "%+.1f", state.imu.rollDeg);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(rollColor);
    canvas.drawString(rollVal, rBoxX + 14, y + 210, &fonts::DejaVu40);
    canvas.drawString("deg", rBoxX + 158, y + 228, &fonts::efontCN_16);

    // Zero calibration offset caption
    char zeroInfo[64];
    snprintf(zeroInfo, sizeof(zeroInfo), "零位补偿偏置: P %+.1f° / R %+.1f°",
             state.imu.pitchZeroOffsetDeg, state.imu.rollZeroOffsetDeg);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString(zeroInfo, rBoxX + 4, y + 288, &fonts::efontCN_14);
}

void Page5Dynamics::drawDualImuStatusStrip(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    // Dual BMI270 (0x68 + 0x69) Redundant Fusion / Failover Indicator (X: 532..1104, Y: 514..586, W: 572, H: 72)
    constexpr int16_t x = 532;
    constexpr int16_t y = 514;
    constexpr int16_t w = 572;
    constexpr int16_t h = 72;

    uint32_t stripBorder = ThemeColors::BORDER_SUBTLE;
    uint32_t modeColor   = ThemeColors::NEON_GREEN;
    const char* modeText = "双 BMI270 均值融合降噪 (-30% 白噪声)";

    if (state.imu.failoverActive) {
        stripBorder = ThemeColors::ALERT_RED;
        modeColor   = ThemeColors::ALERT_RED;
        modeText    = "容灾热备接管: 副片 0x69 独立工作 (<20ms)";
    } else if (state.imu.crossCheckWarning) {
        stripBorder = ThemeColors::AMBER_WARN;
        modeColor   = ThemeColors::AMBER_WARN;
        modeText    = "双芯片交叉互验偏差预警 (自检比对中)";
    } else if (state.imu.isDualFused) {
        stripBorder = 0x1D5C47U;
        modeColor   = ThemeColors::NEON_GREEN;
        modeText    = "双 BMI270 冗余融合已激活 (降噪 30%)";
    } else if (state.imu.masterOnline) {
        modeColor   = ThemeColors::ICE_CYAN;
        modeText    = "主片 BMI270 (0x68) 单通道运行中";
    } else if (state.imu.slaveOnline) {
        modeColor   = ThemeColors::AMBER_WARN;
        modeText    = "副片 BMI270 (0x69) 单通道运行中";
    } else {
        stripBorder = ThemeColors::ALERT_RED;
        modeColor   = ThemeColors::ALERT_RED;
        modeText    = "双路 BMI270 传感器均未就绪";
    }

    canvas.fillRoundRect(x, y, w, h, 12, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(x, y, w, h, 12, stripBorder);

    // Master 0x68 chip pill
    const uint32_t mCol = state.imu.masterOnline ? ThemeColors::NEON_GREEN : ThemeColors::ALERT_RED;
    canvas.fillRoundRect(x + 14, y + 10, 126, 24, 6, state.imu.masterOnline ? 0x0D2920U : 0x2E1010U);
    canvas.drawRoundRect(x + 14, y + 10, 126, 24, 6, mCol);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(mCol);
    canvas.drawString(state.imu.masterOnline ? "主0x68 在线" : "主0x68 异常", x + 77, y + 22, &fonts::efontCN_14);

    // Slave 0x69 chip pill
    const uint32_t sCol = state.imu.slaveOnline ? ThemeColors::NEON_GREEN : ThemeColors::AMBER_WARN;
    canvas.fillRoundRect(x + 148, y + 10, 126, 24, 6, state.imu.slaveOnline ? 0x0D2920U : 0x2B200CU);
    canvas.drawRoundRect(x + 148, y + 10, 126, 24, 6, sCol);
    canvas.setTextColor(sCol);
    canvas.drawString(state.imu.slaveOnline ? "副0x69 在线" : "副0x69 离线", x + 211, y + 22, &fonts::efontCN_14);

    // MagSafe bypass badge
    canvas.setTextDatum(middle_right);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("MagSafe 抗磁已旁路 BMM150", x + w - 16, y + 22, &fonts::efontCN_14);

    // Fusion / Failover status line
    canvas.fillCircle(x + 22, y + 52, 5, modeColor);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(modeColor);
    canvas.drawString(modeText, x + 34, y + 52, &fonts::efontCN_16_b);
}

void Page5Dynamics::drawCalibrateButton(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    const uint32_t nowMs   = millis();
    const bool     justSet = state.imu.cmdCalibrateZeroLevel ||
                             (_lastCalibMs != 0U && (nowMs - _lastCalibMs) < 1600U);

    uint32_t bgCol     = 0x12263AU;
    uint32_t borderCol = ThemeColors::ICE_CYAN;
    uint32_t textCol   = ThemeColors::TEXT_WHITE;
    const char* label  = "水平仪一键归零校准 (将当前驻车姿态设为 0° 基准)";

    if (justSet) {
        bgCol     = 0x0F3828U;
        borderCol = ThemeColors::NEON_GREEN;
        textCol   = ThemeColors::NEON_GREEN;
        label     = "已触发水平仪归零校准 ✓ (NVS 零位已更新)";
    } else if (_calibPressed) {
        bgCol     = ThemeColors::BG_CARD_HIGHLIGHT;
        borderCol = ThemeColors::NEON_GREEN;
    }

    canvas.fillRoundRect(BTN_CALIB_X, BTN_CALIB_Y, BTN_CALIB_W, BTN_CALIB_H, 14, bgCol);
    canvas.drawRoundRect(BTN_CALIB_X, BTN_CALIB_Y, BTN_CALIB_W, BTN_CALIB_H, 14, borderCol);
    canvas.drawRoundRect(BTN_CALIB_X + 1, BTN_CALIB_Y + 1, BTN_CALIB_W - 2, BTN_CALIB_H - 2, 13, borderCol);

    // Target reticle icon on left of button
    const int16_t icX = BTN_CALIB_X + 38;
    const int16_t icY = BTN_CALIB_Y + BTN_CALIB_H / 2;
    canvas.drawCircle(icX, icY, 14, borderCol);
    canvas.drawFastHLine(icX - 19, icY, 39, borderCol);
    canvas.drawFastVLine(icX, icY - 19, 39, borderCol);
    canvas.fillCircle(icX, icY, 3, borderCol);

    canvas.setTextDatum(middle_center);
    canvas.setTextColor(textCol);
    canvas.drawString(label, BTN_CALIB_X + BTN_CALIB_W / 2 + 18, BTN_CALIB_Y + BTN_CALIB_H / 2, &fonts::efontCN_24_b);
}

bool Page5Dynamics::onTouch(const TouchEvent& event, const SystemStateSnapshot& state) {
    const bool inCalibBtn = event.hitTestLocal(BTN_CALIB_X, BTN_CALIB_Y, BTN_CALIB_W, BTN_CALIB_H);

    if (event.type == TOUCH_EVT_DOWN) {
        _calibPressed = inCalibBtn;
        return false; // Allow horizontal carousel swipe unless cleanly tapped
    }

    if (event.type == TOUCH_EVT_UP) {
        _calibPressed = false;
        return false;
    }

    if (event.type == TOUCH_EVT_TAP && inCalibBtn) {
        _calibPressed = false;
        const uint32_t nowMs = millis();
        if (_lastCalibMs == 0U || (nowMs - _lastCalibMs) >= 250U) {
            {
                StateGuard lock(SystemState::instance());
                SystemState::instance().data().imu.cmdCalibrateZeroLevel = true;
            }
            _lastCalibMs = nowMs;
            playFeedbackTone(state, HapticConfig::TONE_CONFIRM_1_HZ, HapticConfig::TONE_CONFIRM_MS);
        }
        return true;
    }

    return false;
}
