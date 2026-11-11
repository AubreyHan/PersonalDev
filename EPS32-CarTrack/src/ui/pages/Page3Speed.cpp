#include "Page3Speed.h"
#include <stdio.h>
#include <math.h>

namespace {

constexpr int16_t TAB_AREA_RIGHT = 1094;
constexpr int16_t TAB_Y          = 14;
constexpr int16_t TAB_W          = 124;
constexpr int16_t TAB_H          = 44;
constexpr int16_t TAB_GAP        = 8;

// Strict 1050px wide 1-hour continuous speed chart & 3-segment bar
constexpr int16_t CHART_X        = 44;
constexpr int16_t CHART_Y        = 252;
constexpr int16_t CHART_W        = 1050;
constexpr int16_t CHART_H        = 296;

uint16_t rgb565Fast(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
}

void formatSecondsHm(uint32_t sec, char* out, size_t outSize) {
    const uint32_t h = sec / 3600UL;
    const uint32_t m = (sec % 3600UL) / 60UL;
    const uint32_t s = sec % 60UL;
    if (h > 0UL) {
        snprintf(out, outSize, "%luh %02lum", static_cast<unsigned long>(h), static_cast<unsigned long>(m));
    } else {
        snprintf(out, outSize, "%02lum %02lus", static_cast<unsigned long>(m), static_cast<unsigned long>(s));
    }
}

} // namespace

TripCategory Page3Speed::effectiveCategory(const SystemStateSnapshot& state) {
    TripCategory cat = state.tripControl.activeViewCategory;
    if (cat >= TRIP_CATEGORY_COUNT) {
        return TRIP_SINGLE;
    }
    if (cat == TRIP_MANUAL && !state.tripControl.manualTripActive) {
        return TRIP_SINGLE;
    }
    return cat;
}

void Page3Speed::render(M5Canvas& canvas, const SystemStateSnapshot& state) {
    clearViewportCanvas(canvas);

    const TripCategory cat   = effectiveCategory(state);
    const TripStats&   stats = state.trips[cat];

    drawHeaderAndTabs(canvas, state, cat);
    drawSpeedHeroCards(canvas, state, stats);
    drawSpeedChart1050(canvas, state, stats);
    drawSpeedStructureBar(canvas, stats);
}

void Page3Speed::drawHeaderAndTabs(M5Canvas& canvas, const SystemStateSnapshot& state, TripCategory cat) {
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextDatum(textdatum_t::middle_left);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.drawString("车速历史曲线与驾驶工况分布", 28, 36);

    char moveIdle[64];
    char mvBuf[20], idBuf[20];
    formatSecondsHm(state.trips[cat].movingDurationSec, mvBuf, sizeof(mvBuf));
    formatSecondsHm(state.trips[cat].idleDurationSec, idBuf, sizeof(idBuf));
    snprintf(moveIdle, sizeof(moveIdle), "行驶 %s  |  驻车怠速 %s", mvBuf, idBuf);
    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString(moveIdle, 380, 37);

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

void Page3Speed::drawSpeedHeroCards(M5Canvas& canvas,
                                    const SystemStateSnapshot& state,
                                    const TripStats& stats) {
    // PRD §3.3 Strict Rule: NEVER show Min Speed! Only Real-Time Speed, ▲ MAX Speed (warm orange/red), and AVG Speed.
    const int16_t rowY = 70;
    const int16_t rowH = 160;

    // 1. Live Speed Card (X: 26..396, W: 370)
    const int16_t c1X = 26;
    const int16_t c1W = 370;
    const float liveSpd = fmaxf(0.0f, state.gnss.speedKmh);
    const uint32_t spdCol = (liveSpd >= 120.0f)
                                ? ThemeColors::ALERT_RED
                                : ((liveSpd >= 95.0f) ? ThemeColors::RACING_YELLOW : ThemeColors::NEON_GREEN);

    canvas.fillRoundRect(c1X, rowY, c1W, rowH, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(c1X, rowY, c1W, rowH, 14, spdCol);

    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(spdCol);
    canvas.drawString("当前实时车速 LIVE SPEED", c1X + 20, rowY + 14);

    char liveBuf[24];
    snprintf(liveBuf, sizeof(liveBuf), "%.0f", static_cast<double>(liveSpd));
    canvas.setFont(&fonts::DejaVu72);
    canvas.setTextDatum(textdatum_t::bottom_left);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString(liveBuf, c1X + 20, rowY + rowH - 14);

    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextDatum(textdatum_t::bottom_right);
    canvas.setTextColor(spdCol);
    canvas.drawString("km/h", c1X + c1W - 20, rowY + rowH - 18);

    // 2. ▲ MAX Speed Card (Warm Orange / Alert Red Highlight — X: 410..744, W: 334)
    const int16_t c2X = 410;
    const int16_t c2W = 334;
    const uint32_t maxCol = (stats.maxSpeedKmh >= 120.0f) ? ThemeColors::ALERT_RED : ThemeColors::AMBER_WARN;
    const uint32_t maxBg  = (stats.maxSpeedKmh >= 120.0f) ? 0x290D0DU : 0x241506U;

    canvas.fillRoundRect(c2X, rowY, c2W, rowH, 14, maxBg);
    canvas.drawRoundRect(c2X, rowY, c2W, rowH, 14, maxCol);

    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(maxCol);
    canvas.drawString("▲ 最高车速 MAX SPEED", c2X + 20, rowY + 14);

    char maxBuf[24];
    snprintf(maxBuf, sizeof(maxBuf), "%.1f", static_cast<double>(stats.maxSpeedKmh));
    canvas.setFont(&fonts::DejaVu56);
    canvas.setTextDatum(textdatum_t::bottom_left);
    canvas.setTextColor(maxCol);
    canvas.drawString(maxBuf, c2X + 20, rowY + rowH - 18);

    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextDatum(textdatum_t::bottom_right);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("km/h", c2X + c2W - 18, rowY + rowH - 22);

    // 3. AVG Speed Card (X: 758..1094, W: 336)
    const int16_t c3X = 758;
    const int16_t c3W = 336;
    canvas.fillRoundRect(c3X, rowY, c3W, rowH, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(c3X, rowY, c3W, rowH, 14, ThemeColors::ICE_CYAN);

    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString("平均行驶车速 AVG SPEED", c3X + 20, rowY + 14);

    char avgBuf[24];
    snprintf(avgBuf, sizeof(avgBuf), "%.1f", static_cast<double>(stats.avgSpeedKmh));
    canvas.setFont(&fonts::DejaVu56);
    canvas.setTextDatum(textdatum_t::bottom_left);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString(avgBuf, c3X + 20, rowY + rowH - 18);

    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextDatum(textdatum_t::bottom_right);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString("km/h", c3X + c3W - 18, rowY + rowH - 22);
}

void Page3Speed::drawSpeedChart1050(M5Canvas& canvas,
                                    const SystemStateSnapshot& state,
                                    const TripStats& stats) {
    canvas.fillRoundRect(CHART_X - 18, CHART_Y - 10, CHART_W + 26, CHART_H + 40, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(CHART_X - 18, CHART_Y - 10, CHART_W + 26, CHART_H + 40, 14, ThemeColors::BORDER_SUBTLE);

    const uint16_t n = stats.speedHistory.count;
    float hi = fmaxf(80.0f, fmaxf(state.gnss.speedKmh, stats.maxSpeedKmh));
    for (uint16_t i = 0; i < n; ++i) {
        const float v = stats.speedHistory.getChronological(i);
        if (v > hi) hi = v;
    }
    // Round upper scale cleanly to nearest 20 km/h (minimum 120 km/h so 40 & 80 bands are clear)
    hi = ceilf(fmaxf(120.0f, hi * 1.10f) / 20.0f) * 20.0f;

    const int16_t plotTop    = CHART_Y + 14;
    const int16_t plotBottom = CHART_Y + CHART_H - 10;
    const int16_t plotH      = plotBottom - plotTop;

    auto spdToY = [&](float s) -> int16_t {
        const float norm = fminf(1.0f, fmaxf(0.0f, s / hi));
        return static_cast<int16_t>(plotBottom - static_cast<int16_t>(lroundf(norm * plotH)));
    };

    // Grid & reference lines (0, 40 urban, 80 highway, 120 limit, top)
    const float refSpeeds[] = { 0.0f, 40.0f, 80.0f, 120.0f, hi };
    for (size_t r = 0; r < sizeof(refSpeeds) / sizeof(refSpeeds[0]); ++r) {
        const float rv = refSpeeds[r];
        if (rv > hi + 0.5f) continue;
        if (r == 4 && fabsf(rv - 120.0f) < 1.0f) continue;

        const int16_t gy = spdToY(rv);
        const uint32_t gCol = (fabsf(rv - 120.0f) < 0.5f) ? 0x3D1818U : 0x192532U;
        canvas.drawFastHLine(CHART_X, gy, CHART_W, gCol);

        char lbl[16];
        snprintf(lbl, sizeof(lbl), "%.0f", static_cast<double>(rv));
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextDatum(textdatum_t::bottom_left);
        canvas.setTextColor((fabsf(rv - 120.0f) < 0.5f) ? ThemeColors::ALERT_RED : ThemeColors::TEXT_MUTED);
        canvas.drawString(lbl, CHART_X + 4, gy - 2);
    }

    static constexpr const char* TIME_TICKS[5] = { "-60 min", "-45 min", "-30 min", "-15 min", "Now 实时" };
    for (int t = 0; t <= 4; ++t) {
        const int16_t gx = CHART_X + (CHART_W * t) / 4;
        canvas.drawFastVLine(gx, plotTop, plotH, 0x15202BU);
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextDatum((t == 0) ? textdatum_t::top_left
                                     : ((t == 4) ? textdatum_t::top_right : textdatum_t::top_center));
        canvas.setTextColor((t == 4) ? ThemeColors::ICE_CYAN : ThemeColors::TEXT_MUTED);
        canvas.drawString(TIME_TICKS[t], gx, plotBottom + 6);
    }

    int16_t px[HistoryBuffer::CAPACITY + 1];
    int16_t py[HistoryBuffer::CAPACITY + 1];
    uint16_t pts = 0;

    if (n == 0) {
        px[0] = CHART_X + CHART_W - 28;
        py[0] = spdToY(state.gnss.speedKmh);
        px[1] = CHART_X + CHART_W - 1;
        py[1] = py[0];
        pts   = 2;
    } else {
        const uint16_t totalSlots = (n >= 8U) ? n : 8U;
        for (uint16_t i = 0; i < n; ++i) {
            const float xRatio = (totalSlots > 1U)
                                     ? (static_cast<float>(totalSlots - n + i) / static_cast<float>(totalSlots - 1U))
                                     : 1.0f;
            px[pts] = CHART_X + static_cast<int16_t>(lroundf(xRatio * (CHART_W - 1)));
            py[pts] = spdToY(stats.speedHistory.getChronological(i));
            pts++;
        }
    }

    // Soft electric-cyan vertical gradient fill under speed curve
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

            const int16_t half = colH / 2;
            if (half > 0) {
                canvas.drawFastVLine(x, yTop, half, rgb565Fast(0, 96, 132));
            }
            if (colH > half) {
                canvas.drawFastVLine(x, yTop + half, colH - half, rgb565Fast(4, 38, 56));
            }
        }
    }

    // Glowing cyan/green speed polyline
    for (uint16_t i = 0; i + 1 < pts; ++i) {
        canvas.drawWideLine(px[i], py[i], px[i + 1], py[i + 1], 5.0f, 0x074158U);
    }
    for (uint16_t i = 0; i + 1 < pts; ++i) {
        canvas.drawWideLine(px[i], py[i], px[i + 1], py[i + 1], 2.4f, ThemeColors::ICE_CYAN);
    }

    canvas.fillSmoothCircle(px[pts - 1], py[pts - 1], 7, 0x0A5A78U);
    canvas.fillSmoothCircle(px[pts - 1], py[pts - 1], 4, ThemeColors::TEXT_WHITE);

    canvas.setFont(&fonts::efontCN_14_b);
    canvas.setTextDatum(textdatum_t::top_right);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("过去 1 小时连续车速折线图 (km/h)", CHART_X + CHART_W - 6, CHART_Y - 2);
}

void Page3Speed::drawSpeedStructureBar(M5Canvas& canvas, const TripStats& stats) {
    const int16_t boxX = CHART_X - 18;
    const int16_t boxY = 586;
    const int16_t boxW = CHART_W + 26;
    const int16_t boxH = 112;

    canvas.fillRoundRect(boxX, boxY, boxW, boxH, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(boxX, boxY, boxW, boxH, 14, ThemeColors::BORDER_SUBTLE);

    const uint32_t hwSec   = stats.highwayMovingSec;
    const uint32_t urbSec  = stats.urbanMovingSec;
    const uint32_t slowSec = stats.slowMovingSec;
    const uint32_t sumSec  = hwSec + urbSec + slowSec;

    float hwPct   = 0.0f;
    float urbPct  = 0.0f;
    float slowPct = 0.0f;
    if (sumSec > 0UL) {
        hwPct   = (static_cast<float>(hwSec)   * 100.0f) / static_cast<float>(sumSec);
        urbPct  = (static_cast<float>(urbSec)  * 100.0f) / static_cast<float>(sumSec);
        slowPct = fmaxf(0.0f, 100.0f - hwPct - urbPct);
    }

    // 1050px segmented bar inside box
    const int16_t barX = CHART_X;
    const int16_t barY = boxY + 18;
    const int16_t barW = CHART_W;
    const int16_t barH = 26;

    canvas.fillRoundRect(barX, barY, barW, barH, 8, 0x16202CU);
    if (sumSec > 0UL) {
        const int16_t wHw  = static_cast<int16_t>(lroundf((hwPct / 100.0f) * barW));
        const int16_t wUrb = static_cast<int16_t>(lroundf((urbPct / 100.0f) * barW));
        const int16_t wSlw = barW - wHw - wUrb;

        int16_t curX = barX;
        if (wHw > 0) {
            canvas.fillRect(curX, barY, wHw, barH, ThemeColors::NEON_GREEN);
            curX += wHw;
        }
        if (wUrb > 0) {
            canvas.fillRect(curX, barY, wUrb, barH, ThemeColors::ICE_CYAN);
            curX += wUrb;
        }
        if (wSlw > 0) {
            canvas.fillRect(curX, barY, wSlw, barH, ThemeColors::AMBER_WARN);
        }
    }
    canvas.drawRoundRect(barX, barY, barW, barH, 8, ThemeColors::BORDER_SUBTLE);

    // 3 Legend & Duration Columns below segmented bar
    struct SegLegend {
        const char* name;
        float       pct;
        uint32_t    sec;
        uint32_t    color;
    };
    const SegLegend segs[3] = {
        { "高速巡航 (>80 km/h)",     hwPct,   hwSec,   ThemeColors::NEON_GREEN },
        { "城市快速 (40~80 km/h)",   urbPct,  urbSec,  ThemeColors::ICE_CYAN },
        { "缓行拥堵 (<=40 km/h)",    slowPct, slowSec, ThemeColors::AMBER_WARN }
    };

    const int16_t legY = barY + 42;
    const int16_t colW = barW / 3;

    for (int i = 0; i < 3; ++i) {
        const int16_t lx = barX + i * colW;
        canvas.fillSmoothCircle(lx + 8, legY + 12, 6, segs[i].color);

        char durStr[20];
        formatSecondsHm(segs[i].sec, durStr, sizeof(durStr));
        char txt[64];
        snprintf(txt, sizeof(txt), "%s: %.0f%% (%s)",
                 segs[i].name, static_cast<double>(segs[i].pct), durStr);

        canvas.setFont(&fonts::efontCN_14_b);
        canvas.setTextDatum(textdatum_t::middle_left);
        canvas.setTextColor(ThemeColors::TEXT_WHITE);
        canvas.drawString(txt, lx + 22, legY + 12);
    }
}

bool Page3Speed::onTouch(const TouchEvent& event, const SystemStateSnapshot& state) {
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
