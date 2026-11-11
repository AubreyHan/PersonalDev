#include "Page2Altitude.h"
#include <stdio.h>
#include <math.h>

namespace {

constexpr int16_t TAB_AREA_RIGHT = 1094;
constexpr int16_t TAB_Y          = 14;
constexpr int16_t TAB_W          = 124;
constexpr int16_t TAB_H          = 44;
constexpr int16_t TAB_GAP        = 8;

// Strict 1050px wide 1-hour continuous altitude chart
constexpr int16_t CHART_X        = 44;
constexpr int16_t CHART_Y        = 274;
constexpr int16_t CHART_W        = 1050;
constexpr int16_t CHART_H        = 388;

uint16_t blendRgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
}

} // namespace

TripCategory Page2Altitude::effectiveCategory(const SystemStateSnapshot& state) {
    TripCategory cat = state.tripControl.activeViewCategory;
    if (cat >= TRIP_CATEGORY_COUNT) {
        return TRIP_SINGLE;
    }
    if (cat == TRIP_MANUAL && !state.tripControl.manualTripActive) {
        return TRIP_SINGLE;
    }
    return cat;
}

void Page2Altitude::render(M5Canvas& canvas, const SystemStateSnapshot& state) {
    clearViewportCanvas(canvas);

    const TripCategory cat   = effectiveCategory(state);
    const TripStats&   stats = state.trips[cat];

    drawHeaderAndTabs(canvas, state, cat);
    drawTopMetricsAndExtremes(canvas, state, stats);
    drawAltitudeChart1050(canvas, state, stats);
}

void Page2Altitude::drawHeaderAndTabs(M5Canvas& canvas, const SystemStateSnapshot& state, TripCategory cat) {
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextDatum(textdatum_t::middle_left);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString("气压+GNSS 融合海拔与爬升剖面", 28, 36);

    char baroInfo[64];
    snprintf(baroInfo, sizeof(baroInfo), "气压 %.1f hPa  |  海平面基准 P0 %.1f hPa",
             static_cast<double>(state.baro.pressureHpa),
             static_cast<double>(state.baro.seaLevelPressureHpa));
    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString(baroInfo, 392, 37);

    // Adaptive [单次 TRIP / 当日 DAY / (小记 SUB)] switcher
    const int count = state.tripControl.manualTripActive ? 3 : 2;
    const TripCategory cats[3] = { TRIP_SINGLE, TRIP_DAILY, TRIP_MANUAL };
    const char* labels[3]      = { "单次 TRIP", "当日 DAY", "小记 SUB" };

    const int16_t totalW = count * TAB_W + (count - 1) * TAB_GAP;
    const int16_t startX = TAB_AREA_RIGHT - totalW;

    for (int i = 0; i < count; ++i) {
        const int16_t tx = startX + i * (TAB_W + TAB_GAP);
        const bool sel   = (cat == cats[i]);
        const uint32_t activeCol = (cats[i] == TRIP_MANUAL) ? ThemeColors::RACING_YELLOW : ThemeColors::NEON_GREEN;

        canvas.fillRoundRect(tx, TAB_Y, TAB_W, TAB_H, 10, sel ? activeCol : ThemeColors::BG_CARD_DARK);
        if (!sel) {
            canvas.drawRoundRect(tx, TAB_Y, TAB_W, TAB_H, 10, ThemeColors::BORDER_SUBTLE);
        }
        canvas.setFont(&fonts::efontCN_16_b);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.setTextColor(sel ? TFT_BLACK : ThemeColors::TEXT_WHITE);
        canvas.drawString(labels[i], tx + TAB_W / 2, TAB_Y + TAB_H / 2);
    }
}

void Page2Altitude::drawTopMetricsAndExtremes(M5Canvas& canvas,
                                              const SystemStateSnapshot& state,
                                              const TripStats& stats) {
    const int16_t rowY = 72;
    const int16_t rowH = 176;

    // 1. Hero Fused Altitude Card (X: 26..396, W: 370)
    const int16_t c1X = 26;
    const int16_t c1W = 370;
    canvas.fillRoundRect(c1X, rowY, c1W, rowH, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(c1X, rowY, c1W, rowH, 14, ThemeColors::NEON_GREEN);

    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.drawString("当前融合海拔 FUSED ALT", c1X + 20, rowY + 14);

    char altStr[24];
    if (fabsf(state.baro.fusedAltitudeM) < 1000.0f) {
        snprintf(altStr, sizeof(altStr), "%.1f", static_cast<double>(state.baro.fusedAltitudeM));
    } else {
        snprintf(altStr, sizeof(altStr), "%.0f", static_cast<double>(state.baro.fusedAltitudeM));
    }
    canvas.setFont(&fonts::DejaVu72);
    canvas.setTextDatum(textdatum_t::bottom_left);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString(altStr, c1X + 18, rowY + 142);

    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextDatum(textdatum_t::bottom_right);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.drawString("m", c1X + c1W - 20, rowY + 138);

    char rawCmp[48];
    snprintf(rawCmp, sizeof(rawCmp), "GNSS %.0fm  |  坡度 %+.1f%%",
             static_cast<double>(state.gnss.altitudeMeters),
             static_cast<double>(state.baro.roadSlopePercent));
    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextDatum(textdatum_t::bottom_left);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString(rawCmp, c1X + 20, rowY + rowH - 10);

    // 2. Cumulative Climb & Vertical Speed Card (X: 410..736, W: 326)
    const int16_t c2X = 410;
    const int16_t c2W = 326;
    canvas.fillRoundRect(c2X, rowY, c2W, rowH, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(c2X, rowY, c2W, rowH, 14, ThemeColors::BORDER_SUBTLE);

    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(ThemeColors::RACING_YELLOW);
    canvas.drawString("累计爬升 CLIMB", c2X + 18, rowY + 14);

    char clbStr[24];
    snprintf(clbStr, sizeof(clbStr), "+%.0f m", static_cast<double>(stats.cumulativeClimbM));
    canvas.setFont(&fonts::FreeSansBold24pt7b);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.drawString(clbStr, c2X + 18, rowY + 46);

    canvas.drawFastHLine(c2X + 16, rowY + 98, c2W - 32, ThemeColors::BORDER_SUBTLE);

    const float vspd = state.baro.verticalSpeedMps;
    const uint32_t vCol = (vspd > 0.15f)
                              ? ThemeColors::NEON_GREEN
                              : ((vspd < -0.15f) ? ThemeColors::ICE_CYAN : ThemeColors::TEXT_WHITE);
    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("垂直升降速 V-SPD", c2X + 18, rowY + 110);

    char vspdStr[32];
    snprintf(vspdStr, sizeof(vspdStr), "%s %+.2f m/s",
             (vspd >= 0.0f) ? "▲" : "▼",
             static_cast<double>(vspd));
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(vCol);
    canvas.drawString(vspdStr, c2X + 18, rowY + 134);

    // 3. Max / Min Extreme Cards Stacked on Right (X: 750..1094, W: 344)
    const int16_t c3X   = 750;
    const int16_t c3W   = 344;
    const int16_t halfH = (rowH - 12) / 2; // 82px each

    const float maxAlt = stats.altitudeInitialized ? stats.maxAltitudeM : state.baro.fusedAltitudeM;
    const float minAlt = stats.altitudeInitialized ? stats.minAltitudeM : state.baro.fusedAltitudeM;

    // Top Extreme: MAX Altitude (Warm Orange/Red Highlight)
    canvas.fillRoundRect(c3X, rowY, c3W, halfH, 12, 0x221408U);
    canvas.drawRoundRect(c3X, rowY, c3W, halfH, 12, ThemeColors::AMBER_WARN);
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextDatum(textdatum_t::middle_left);
    canvas.setTextColor(ThemeColors::AMBER_WARN);
    canvas.drawString("▲ 最高海拔 MAX", c3X + 18, rowY + halfH / 2);

    char maxStr[24];
    snprintf(maxStr, sizeof(maxStr), "%.1f m", static_cast<double>(maxAlt));
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextDatum(textdatum_t::middle_right);
    canvas.setTextColor(ThemeColors::AMBER_WARN);
    canvas.drawString(maxStr, c3X + c3W - 18, rowY + halfH / 2);

    // Bottom Extreme: MIN Altitude (Ice Cyan Highlight)
    const int16_t minY = rowY + halfH + 12;
    canvas.fillRoundRect(c3X, minY, c3W, halfH, 12, 0x081B26U);
    canvas.drawRoundRect(c3X, minY, c3W, halfH, 12, ThemeColors::ICE_CYAN);
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextDatum(textdatum_t::middle_left);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString("▼ 最低海拔 MIN", c3X + 18, minY + halfH / 2);

    char minStr[24];
    snprintf(minStr, sizeof(minStr), "%.1f m", static_cast<double>(minAlt));
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextDatum(textdatum_t::middle_right);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString(minStr, c3X + c3W - 18, minY + halfH / 2);
}

void Page2Altitude::drawAltitudeChart1050(M5Canvas& canvas,
                                          const SystemStateSnapshot& state,
                                          const TripStats& stats) {
    // Outer chart frame enclosing the exact 1050px plot area
    canvas.fillRoundRect(CHART_X - 18, CHART_Y - 12, CHART_W + 26, CHART_H + 50, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(CHART_X - 18, CHART_Y - 12, CHART_W + 26, CHART_H + 50, 14, ThemeColors::BORDER_SUBTLE);

    const uint16_t n = stats.altitudeHistory.count;

    // Determine auto-scale min/max across 1-hour history + live fused altitude
    float lo = state.baro.fusedAltitudeM;
    float hi = state.baro.fusedAltitudeM;
    if (n > 0) {
        lo = stats.altitudeHistory.getChronological(0);
        hi = lo;
        for (uint16_t i = 1; i < n; ++i) {
            const float v = stats.altitudeHistory.getChronological(i);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        if (state.baro.fusedAltitudeM < lo) lo = state.baro.fusedAltitudeM;
        if (state.baro.fusedAltitudeM > hi) hi = state.baro.fusedAltitudeM;
    }

    float span = hi - lo;
    if (span < 24.0f) {
        const float mid = 0.5f * (hi + lo);
        lo   = mid - 12.0f;
        hi   = mid + 12.0f;
        span = 24.0f;
    } else {
        const float pad = span * 0.12f;
        lo -= pad;
        hi += pad;
        span = hi - lo;
    }

    const int16_t plotTop    = CHART_Y + 18;
    const int16_t plotBottom = CHART_Y + CHART_H - 10;
    const int16_t plotH      = plotBottom - plotTop;

    // Horizontal grid lines & Y-axis altitude labels
    for (int g = 0; g <= 4; ++g) {
        const int16_t gy  = plotTop + (plotH * g) / 4;
        const float   val = hi - (span * static_cast<float>(g)) / 4.0f;
        canvas.drawFastHLine(CHART_X, gy, CHART_W, 0x192532U);

        char lbl[16];
        snprintf(lbl, sizeof(lbl), "%.0fm", static_cast<double>(val));
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextDatum(textdatum_t::bottom_left);
        canvas.setTextColor(ThemeColors::TEXT_MUTED);
        canvas.drawString(lbl, CHART_X + 4, gy - 2);
    }

    // Vertical time grid (-60m, -45m, -30m, -15m, Now) across 1050px width
    static constexpr const char* TIME_TICKS[5] = { "-60 min", "-45 min", "-30 min", "-15 min", "Now 实时" };
    for (int t = 0; t <= 4; ++t) {
        const int16_t gx = CHART_X + (CHART_W * t) / 4;
        canvas.drawFastVLine(gx, plotTop, plotH, 0x15202BU);
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextDatum((t == 0) ? textdatum_t::top_left
                                     : ((t == 4) ? textdatum_t::top_right : textdatum_t::top_center));
        canvas.setTextColor((t == 4) ? ThemeColors::NEON_GREEN : ThemeColors::TEXT_MUTED);
        canvas.drawString(TIME_TICKS[t], gx, plotBottom + 8);
    }

    // Build screen-space polyline (up to 121 points: 120 history + current live anchor)
    int16_t px[HistoryBuffer::CAPACITY + 1];
    int16_t py[HistoryBuffer::CAPACITY + 1];
    uint16_t pts = 0;

    auto altToY = [&](float a) -> int16_t {
        const float norm = fminf(1.0f, fmaxf(0.0f, (a - lo) / span));
        return static_cast<int16_t>(plotBottom - static_cast<int16_t>(lroundf(norm * plotH)));
    };

    if (n == 0) {
        // Fresh reset / boot anchor: flat glowing baseline at right edge ("Now")
        px[0] = CHART_X + CHART_W - 28;
        py[0] = altToY(state.baro.fusedAltitudeM);
        px[1] = CHART_X + CHART_W - 1;
        py[1] = py[0];
        pts   = 2;
    } else {
        // Right-aligned to "Now" so 1 sample = CHART_W / CAPACITY across 60 minutes, or full span when full
        const uint16_t totalSlots = (n >= 8U) ? n : 8U;
        for (uint16_t i = 0; i < n; ++i) {
            const float xRatio = (totalSlots > 1U)
                                     ? (static_cast<float>(totalSlots - n + i) / static_cast<float>(totalSlots - 1U))
                                     : 1.0f;
            px[pts] = CHART_X + static_cast<int16_t>(lroundf(xRatio * (CHART_W - 1)));
            py[pts] = altToY(stats.altitudeHistory.getChronological(i));
            pts++;
        }
    }

    // 1. Continuous Mountain Vertical Gradient Fill under the curve
    for (uint16_t i = 0; i + 1 < pts; ++i) {
        const int16_t x0 = px[i];
        const int16_t x1 = px[i + 1];
        const int16_t y0 = py[i];
        const int16_t y1 = py[i + 1];
        const int16_t dx = x1 - x0;
        if (dx <= 0) continue;

        for (int16_t x = x0; x <= x1; ++x) {
            const float t      = static_cast<float>(x - x0) / static_cast<float>(dx);
            const int16_t yTop = static_cast<int16_t>(lroundf(y0 + t * (y1 - y0)));
            const int16_t colH = plotBottom - yTop;
            if (colH <= 1) continue;

            // 3-band fast vertical gradient column for smooth mountain glow at 60fps
            const int16_t band1 = colH / 3;
            const int16_t band2 = (colH * 2) / 3;
            if (band1 > 0) {
                canvas.drawFastVLine(x, yTop, band1, blendRgb565(0, 128, 92));
            }
            if (band2 > band1) {
                canvas.drawFastVLine(x, yTop + band1, band2 - band1, blendRgb565(0, 72, 58));
            }
            if (colH > band2) {
                canvas.drawFastVLine(x, yTop + band2, colH - band2, blendRgb565(4, 34, 32));
            }
        }
    }

    // 2. Multi-pass Glowing Anti-Aliased Elevation Polyline
    for (uint16_t i = 0; i + 1 < pts; ++i) {
        canvas.drawWideLine(px[i], py[i], px[i + 1], py[i + 1], 5.5f, 0x064D38U);
    }
    for (uint16_t i = 0; i + 1 < pts; ++i) {
        canvas.drawWideLine(px[i], py[i], px[i + 1], py[i + 1], 2.4f, ThemeColors::NEON_GREEN);
    }

    // Glowing live endpoint beacon at "Now"
    const int16_t lastX = px[pts - 1];
    const int16_t lastY = py[pts - 1];
    canvas.fillSmoothCircle(lastX, lastY, 8, 0x085E44U);
    canvas.fillSmoothCircle(lastX, lastY, 4, ThemeColors::TEXT_WHITE);

    canvas.setFont(&fonts::efontCN_14_b);
    canvas.setTextDatum(textdatum_t::top_right);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("过去 1 小时连续海拔曲线 (1050px 自适应量程)", CHART_X + CHART_W - 6, CHART_Y - 4);
}

bool Page2Altitude::onTouch(const TouchEvent& event, const SystemStateSnapshot& state) {
    if (event.type != TOUCH_EVT_TAP) {
        return false;
    }

    const int count = state.tripControl.manualTripActive ? 3 : 2;
    const TripCategory cats[3] = { TRIP_SINGLE, TRIP_DAILY, TRIP_MANUAL };
    const int16_t totalW       = count * TAB_W + (count - 1) * TAB_GAP;
    const int16_t startX       = TAB_AREA_RIGHT - totalW;

    for (int i = 0; i < count; ++i) {
        const int16_t tx = startX + i * (TAB_W + TAB_GAP);
        if (event.hitTestLocal(tx, TAB_Y, TAB_W, TAB_H)) {
            playFeedbackTone(state);
            StateGuard guard(SystemState::instance());
            if (guard.locked()) {
                SystemState::instance().data().tripControl.activeViewCategory = cats[i];
            }
            return true;
        }
    }
    return false;
}
