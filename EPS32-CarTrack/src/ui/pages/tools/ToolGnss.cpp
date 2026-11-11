#include "ToolGnss.h"
#include <stdio.h>
#include <math.h>
#include <type_traits>

static_assert(!std::is_abstract<ToolGnss>::value && std::is_default_constructible<ToolGnss>::value,
              "ToolGnss must be concrete and default-constructible");

namespace {

constexpr uint32_t COL_BDS  = ThemeColors::NEON_GREEN;    // #00FFAA BeiDou
constexpr uint32_t COL_GPS  = ThemeColors::ICE_CYAN;      // #00D4FF GPS
constexpr uint32_t COL_GLO  = ThemeColors::AMBER_WARN;    // #FFA500 GLONASS
constexpr uint32_t COL_GAL  = 0xC77DFFU;                  // Violet Galileo
constexpr uint32_t COL_OTHER = ThemeColors::RACING_YELLOW;

uint32_t colorForConstellation(uint8_t gnssId) {
    switch (gnssId) {
        case GNSS_CONST_BEIDOU:  return COL_BDS;
        case GNSS_CONST_GPS:     return COL_GPS;
        case GNSS_CONST_GLONASS: return COL_GLO;
        case GNSS_CONST_GALILEO: return COL_GAL;
        default:                 return COL_OTHER;
    }
}

char prefixForConstellation(uint8_t gnssId) {
    switch (gnssId) {
        case GNSS_CONST_BEIDOU:  return 'B';
        case GNSS_CONST_GPS:     return 'G';
        case GNSS_CONST_GLONASS: return 'R';
        case GNSS_CONST_GALILEO: return 'E';
        case GNSS_CONST_QZSS:    return 'J';
        default:                 return 'S';
    }
}

const char* fixTypeBadgeText(GnssFixType ft, bool isDr) {
    if (isDr || ft == FIX_DEAD_REC || ft == FIX_GNSS_DR) return "GNSS+DR 惯导融合";
    switch (ft) {
        case FIX_3D:        return "3D FIX 精密定位";
        case FIX_2D:        return "2D FIX 平面定位";
        case FIX_TIME_ONLY: return "TIME ONLY 仅授时";
        default:            return "NO FIX 搜星中";
    }
}

} // namespace

void ToolGnss::onEnter(const SystemStateSnapshot& state) {
    (void)state;
    _agpsPressed = false;
}

void ToolGnss::render(M5Canvas& canvas, const SystemStateSnapshot& state) {
    clearViewportCanvas(canvas);
    drawHeaderBar(canvas, state);
    drawSkyplotPanel(canvas, state);
    drawCnoBarChart(canvas, state);
    drawDopAndFilterCard(canvas, state);
    drawMonRfAndAgpsCard(canvas, state);
}

void ToolGnss::drawHeaderBar(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    const GnssData& g = state.gnss;

    canvas.fillRoundRect(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H, 10, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H, 10, ThemeColors::ICE_CYAN);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.setTextDatum(middle_center);
    canvas.drawString("< 工具箱", BTN_BACK_X + BTN_BACK_W / 2, BTN_BACK_Y + BTN_BACK_H / 2);

    // Fix Status Pill
    const uint32_t fixCol = g.isDeadReckoning ? ThemeColors::AMBER_WARN :
                            (g.isValid ? ThemeColors::NEON_GREEN : ThemeColors::ALERT_RED);
    canvas.fillRoundRect(216, 14, 214, 50, 10, 0x12222EU);
    canvas.drawRoundRect(216, 14, 214, 50, 10, fixCol);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(fixCol);
    canvas.setTextDatum(middle_center);
    canvas.drawString(fixTypeBadgeText(g.fixType, g.isDeadReckoning), 323, 39);

    // Coordinates & Sat Summary
    char coordBuf[96];
    if (g.isValid) {
        snprintf(coordBuf, sizeof(coordBuf), "%.6f°%c,  %.6f°%c   |   解算 %u / 可见 %u 颗",
                 fabs(g.latitude), (g.latitude >= 0.0) ? 'N' : 'S',
                 fabs(g.longitude), (g.longitude >= 0.0) ? 'E' : 'W',
                 static_cast<unsigned>(g.numSats),
                 static_cast<unsigned>(g.satsInView));
    } else {
        snprintf(coordBuf, sizeof(coordBuf), "等待有效星历解算 (UART RX%d/TX%d)   |   解算 %u / 可见 %u 颗",
                 static_cast<int>(state.boot.gnssActiveRxPin),
                 static_cast<int>(state.boot.gnssActiveTxPin),
                 static_cast<unsigned>(g.numSats),
                 static_cast<unsigned>(g.satsInView));
    }
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(middle_right);
    canvas.drawString(coordBuf, 1102, 39);
}

void ToolGnss::drawSkyplotPanel(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    const GnssData& g = state.gnss;
    const int16_t px = 18;
    const int16_t py = 78;
    const int16_t pw = 468;
    const int16_t ph = 624;

    canvas.fillRoundRect(px, py, pw, ph, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(px, py, pw, ph, 14, ThemeColors::BORDER_SUBTLE);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(top_left);
    canvas.drawString("四星极坐标星空图", px + 18, py + 14);

    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.setTextDatum(top_right);
    canvas.drawString("实心=参与解算  空心=可见跟踪", px + pw - 18, py + 20);

    const int16_t cx   = px + pw / 2;
    const int16_t cy   = py + 258;
    const int16_t maxR = 186; // 0 deg horizon
    const int16_t r30  = (maxR * 2) / 3; // 30 deg elevation
    const int16_t r60  = maxR / 3;       // 60 deg elevation

    // Polar rings & crosshairs
    canvas.fillCircle(cx, cy, maxR, 0x070C12U);
    canvas.drawCircle(cx, cy, maxR, 0x2E4258U);
    canvas.drawCircle(cx, cy, r30, 0x203042U);
    canvas.drawCircle(cx, cy, r60, 0x203042U);
    canvas.drawFastHLine(cx - maxR, cy, maxR * 2, 0x1C2B3AU);
    canvas.drawFastVLine(cx, cy - maxR, maxR * 2, 0x1C2B3AU);

    // Diagonal 45 deg spokes
    const int16_t diag = static_cast<int16_t>(static_cast<float>(maxR) * 0.7071f);
    canvas.drawLine(cx - diag, cy - diag, cx + diag, cy + diag, 0x15202CU);
    canvas.drawLine(cx - diag, cy + diag, cx + diag, cy - diag, 0x15202CU);

    // Cardinal & elevation labels
    canvas.setFont(&fonts::DejaVu18);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(ThemeColors::ALERT_RED);
    canvas.drawString("N", cx, cy - maxR - 13);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("S", cx, cy + maxR + 13);
    canvas.drawString("W", cx - maxR - 14, cy);
    canvas.drawString("E", cx + maxR + 14, cy);

    canvas.setFont(&fonts::DejaVu12);
    canvas.setTextColor(0x526478U);
    canvas.drawString("30", cx + 14, cy - r30 + 8);
    canvas.drawString("60", cx + 14, cy - r60 + 8);

    // Plot tracked satellites
    const uint8_t nSats = (g.activeSatCount <= GnssData::MAX_TRACKED_SATS)
                              ? g.activeSatCount
                              : static_cast<uint8_t>(GnssData::MAX_TRACKED_SATS);
    for (uint8_t i = 0; i < nSats; ++i) {
        const SatInfo& s = g.sats[i];
        if (s.svId == 0 && s.cno == 0) {
            continue;
        }
        int16_t elevClamped = s.elev;
        if (elevClamped < 0)  elevClamped = 0;
        if (elevClamped > 90) elevClamped = 90;

        const float rNorm = static_cast<float>(90 - elevClamped) / 90.0f;
        const float rPx   = rNorm * static_cast<float>(maxR - 8);
        const float azRad = static_cast<float>(s.azim) * 0.0174532925f;

        const int16_t sx = cx + static_cast<int16_t>(roundf(rPx * sinf(azRad)));
        const int16_t sy = cy - static_cast<int16_t>(roundf(rPx * cosf(azRad)));
        const uint32_t col = colorForConstellation(s.gnssId);

        if (s.used) {
            canvas.fillCircle(sx, sy, 8, col);
            canvas.drawCircle(sx, sy, 9, TFT_BLACK);
        } else {
            canvas.fillCircle(sx, sy, 7, 0x070C12U);
            canvas.drawCircle(sx, sy, 7, col);
            canvas.drawCircle(sx, sy, 6, col);
        }
    }

    // 4-Constellation Legend Grid at Bottom of Skyplot Card (2x2)
    struct ConstLegend {
        const char* label;
        uint8_t     used;
        uint8_t     view;
        uint32_t    color;
    };
    const ConstLegend legends[4] = {
        { "北斗 BDS",    g.numBdsSats, g.viewBdsSats, COL_BDS },
        { "美国 GPS",    g.numGpsSats, g.viewGpsSats, COL_GPS },
        { "格洛纳斯 GLO", g.numGloSats, g.viewGloSats, COL_GLO },
        { "伽利略 GAL",  g.numGalSats, g.viewGalSats, COL_GAL }
    };

    canvas.drawFastHLine(px + 18, py + 474, pw - 36, 0x1B2634U);
    for (int k = 0; k < 4; ++k) {
        const int16_t lx = px + 22 + (k % 2) * 218;
        const int16_t ly = py + 490 + (k / 2) * 60;

        canvas.fillRoundRect(lx, ly, 206, 48, 8, 0x121C28U);
        canvas.fillCircle(lx + 18, ly + 24, 7, legends[k].color);

        canvas.setFont(&fonts::efontCN_16_b);
        canvas.setTextColor(ThemeColors::TEXT_WHITE);
        canvas.setTextDatum(middle_left);
        canvas.drawString(legends[k].label, lx + 32, ly + 24);

        char cntTxt[24];
        snprintf(cntTxt, sizeof(cntTxt), "%u/%u",
                 static_cast<unsigned>(legends[k].used),
                 static_cast<unsigned>(legends[k].view));
        canvas.setFont(&fonts::DejaVu18);
        canvas.setTextColor(legends[k].color);
        canvas.setTextDatum(middle_right);
        canvas.drawString(cntTxt, lx + 194, ly + 24);
    }
}

void ToolGnss::drawCnoBarChart(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    const GnssData& g = state.gnss;
    const int16_t cx = 500;
    const int16_t cy = 78;
    const int16_t cw = 602;
    const int16_t ch = 248;

    canvas.fillRoundRect(cx, cy, cw, ch, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(cx, cy, cw, ch, 14, ThemeColors::BORDER_SUBTLE);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(top_left);
    canvas.drawString("活跃卫星载噪比 C/N0 (dBHz)", cx + 18, cy + 12);

    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.setTextDatum(top_right);
    canvas.drawString("20 / 35 dBHz 门限参考线", cx + cw - 18, cy + 18);

    const int16_t plotX = cx + 22;
    const int16_t plotY = cy + 52;
    const int16_t plotW = cw - 44; // 558px
    const int16_t plotH = 152;
    const int16_t baseY = plotY + plotH;

    // Reference horizontal lines (20 dBHz & 35 dBHz out of 55 dBHz max)
    const int16_t y20 = baseY - (20 * plotH) / 55;
    const int16_t y35 = baseY - (35 * plotH) / 55;
    canvas.drawFastHLine(plotX, baseY, plotW, 0x263648U);
    canvas.drawFastHLine(plotX, y20, plotW, 0x2A2214U);
    canvas.drawFastHLine(plotX, y35, plotW, 0x142E26U);

    // Select up to 16 satellites with highest C/N0
    uint8_t indices[GnssData::MAX_TRACKED_SATS];
    uint8_t validCount = 0;
    const uint8_t limit = (g.activeSatCount <= GnssData::MAX_TRACKED_SATS)
                              ? g.activeSatCount
                              : static_cast<uint8_t>(GnssData::MAX_TRACKED_SATS);
    for (uint8_t i = 0; i < limit; ++i) {
        if (g.sats[i].svId != 0 || g.sats[i].cno > 0) {
            indices[validCount++] = i;
        }
    }
    // Simple selection sort descending by (used, cno)
    for (uint8_t i = 0; i < validCount; ++i) {
        for (uint8_t j = static_cast<uint8_t>(i + 1U); j < validCount; ++j) {
            const SatInfo& a = g.sats[indices[i]];
            const SatInfo& b = g.sats[indices[j]];
            if (b.cno > a.cno || (b.cno == a.cno && b.used && !a.used)) {
                const uint8_t tmp = indices[i];
                indices[i] = indices[j];
                indices[j] = tmp;
            }
        }
    }

    const uint8_t showCount = (validCount > 16U) ? 16U : validCount;
    if (showCount == 0U) {
        canvas.setFont(&fonts::efontCN_24);
        canvas.setTextColor(ThemeColors::TEXT_MUTED);
        canvas.setTextDatum(middle_center);
        canvas.drawString("正在采集 UBX-NAV-SAT 卫星载噪比...", cx + cw / 2, cy + ch / 2 + 12);
        return;
    }

    const int16_t slotW = plotW / 16; // 34px
    const int16_t barW  = slotW - 8;  // 26px

    for (uint8_t k = 0; k < showCount; ++k) {
        const SatInfo& s   = g.sats[indices[k]];
        const uint8_t cno  = (s.cno > 55U) ? 55U : s.cno;
        const int16_t bh   = static_cast<int16_t>((static_cast<int>(cno) * plotH) / 55);
        const int16_t bx   = plotX + static_cast<int16_t>(k) * slotW + 4;
        const int16_t by   = baseY - bh;
        const uint32_t col = colorForConstellation(s.gnssId);

        if (bh > 0) {
            if (s.used) {
                canvas.fillRoundRect(bx, by, barW, bh, 4, col);
            } else {
                canvas.fillRoundRect(bx, by, barW, bh, 4, 0x162230U);
                canvas.drawRoundRect(bx, by, barW, bh, 4, col);
            }
        }

        // C/N0 value above bar
        char cnoStr[8];
        snprintf(cnoStr, sizeof(cnoStr), "%u", static_cast<unsigned>(s.cno));
        canvas.setFont(&fonts::DejaVu12);
        canvas.setTextColor(s.used ? ThemeColors::TEXT_WHITE : ThemeColors::TEXT_MUTED);
        canvas.setTextDatum(bottom_center);
        canvas.drawString(cnoStr, bx + barW / 2, by - 2);

        // PRN ID below baseline
        char prnStr[10];
        snprintf(prnStr, sizeof(prnStr), "%c%02u",
                 prefixForConstellation(s.gnssId),
                 static_cast<unsigned>(s.svId % 100U));
        canvas.setTextColor(col);
        canvas.setTextDatum(top_center);
        canvas.drawString(prnStr, bx + barW / 2, baseY + 6);
    }
}

void ToolGnss::drawDopAndFilterCard(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    const GnssData& g = state.gnss;
    const int16_t cx = 500;
    const int16_t cy = 338;
    const int16_t cw = 602;
    const int16_t ch = 152;

    canvas.fillRoundRect(cx, cy, cw, ch, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(cx, cy, cw, ch, 14, ThemeColors::BORDER_SUBTLE);

    // DOP Trio + hAcc
    struct DopCell {
        const char* name;
        float       val;
    };
    const DopCell dops[3] = {
        { "HDOP 水平精度因子", g.hdop },
        { "VDOP 垂直精度因子", g.vdop },
        { "PDOP 空间位置因子", g.pdop }
    };

    for (int i = 0; i < 3; ++i) {
        const int16_t dx = cx + 18 + i * 194;
        canvas.setFont(&fonts::efontCN_14);
        canvas.setTextColor(ThemeColors::TEXT_MUTED);
        canvas.setTextDatum(top_left);
        canvas.drawString(dops[i].name, dx, cy + 14);

        char valBuf[16];
        if (dops[i].val >= 50.0f) {
            snprintf(valBuf, sizeof(valBuf), "--.-");
        } else {
            snprintf(valBuf, sizeof(valBuf), "%.2f", dops[i].val);
        }
        const uint32_t dCol = (dops[i].val <= 1.5f) ? ThemeColors::NEON_GREEN :
                              (dops[i].val <= 3.0f) ? ThemeColors::ICE_CYAN :
                                                      ThemeColors::AMBER_WARN;
        canvas.setFont(&fonts::DejaVu40);
        canvas.setTextColor(dCol);
        canvas.drawString(valBuf, dx, cy + 36);
    }

    canvas.drawFastHLine(cx + 18, cy + 92, cw - 36, 0x1B2634U);

    // Two-Stage Dynamic Elevation/C/N0 Filter & Watchdog status
    const bool stage2 = g.stage2TightFilter;
    canvas.fillRoundRect(cx + 18, cy + 104, 304, 34, 8, stage2 ? 0x0E2B22U : 0x162434U);
    canvas.drawRoundRect(cx + 18, cy + 104, 304, 34, 8, stage2 ? ThemeColors::NEON_GREEN : ThemeColors::ICE_CYAN);
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextColor(stage2 ? ThemeColors::NEON_GREEN : ThemeColors::ICE_CYAN);
    canvas.setTextDatum(middle_center);
    canvas.drawString(stage2 ? "二阶段抗多径门限 (10° / 20dBHz)" : "一阶段快捕门限 (5° / 14dBHz)",
                      cx + 170, cy + 121);

    char extraAcc[64];
    snprintf(extraAcc, sizeof(extraAcc), "水平误差 ±%.1fm  |  热重启 %u 次",
             (g.hAccMeters > 99.9f) ? 99.9f : g.hAccMeters,
             static_cast<unsigned>(g.hotRestartCount));
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(middle_right);
    canvas.drawString(extraAcc, cx + cw - 18, cy + 121);
}

void ToolGnss::drawMonRfAndAgpsCard(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    const GnssData& g      = state.gnss;
    const NetworkData& net = state.network;
    const int16_t cx = 500;
    const int16_t cy = 502;
    const int16_t cw = 602;
    const int16_t ch = 200;

    canvas.fillRoundRect(cx, cy, cw, ch, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(cx, cy, cw, ch, 14, ThemeColors::BORDER_SUBTLE);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(top_left);
    canvas.drawString("UBX-MON-RF 射频与有源天线诊断", cx + 18, cy + 12);

    // Authoritative u-blox M9 GnssAntennaStatus (ANT_OK=2, ANT_SHORT=3, ANT_OPEN=4)
    const char* antTitle = "INIT 自检中";
    uint32_t antCol      = ThemeColors::TEXT_MUTED;
    switch (g.antennaStatus) {
        case ANT_OK:
            antTitle = "OK 有源天线正常";
            antCol   = ThemeColors::NEON_GREEN;
            break;
        case ANT_SHORT:
            antTitle = "SHORT 天线短路!";
            antCol   = ThemeColors::ALERT_RED;
            break;
        case ANT_OPEN:
            antTitle = "OPEN 天线断路/未接!";
            antCol   = ThemeColors::ALERT_RED;
            break;
        case ANT_DONTKNOW:
            antTitle = "DONTKNOW 未知";
            antCol   = ThemeColors::AMBER_WARN;
            break;
        case ANT_INIT:
        default:
            break;
    }

    // Authoritative u-blox M9 RfJammingState (JAMMING_OK=1, JAMMING_WARN=2, JAMMING_CRIT=3)
    const char* jamTitle = "UNKNOWN 待启用";
    uint32_t jamCol      = ThemeColors::TEXT_MUTED;
    switch (g.jammingState) {
        case JAMMING_OK:
            jamTitle = "OK 射频净空良好";
            jamCol   = ThemeColors::NEON_GREEN;
            break;
        case JAMMING_WARN:
            jamTitle = "WARN 存在频段干扰";
            jamCol   = ThemeColors::AMBER_WARN;
            break;
        case JAMMING_CRIT:
            jamTitle = "CRIT 严重压制干扰!";
            jamCol   = ThemeColors::ALERT_RED;
            break;
        case JAMMING_UNKNOWN:
        default:
            break;
    }

    // Left sub-box: Antenna circuit status
    canvas.fillRoundRect(cx + 18, cy + 48, 274, 54, 10, 0x121C28U);
    canvas.drawRoundRect(cx + 18, cy + 48, 274, 54, 10, antCol);
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextColor(antCol);
    canvas.setTextDatum(top_left);
    canvas.drawString(antTitle, cx + 30, cy + 54);

    char pwrStr[48];
    const char* pwrState = (g.antennaPower == 1U) ? "供电 ON" : ((g.antennaPower == 0U) ? "供电 OFF" : "供电 UNK");
    snprintf(pwrStr, sizeof(pwrStr), "馈电状态: %s (enum=%u)", pwrState, static_cast<unsigned>(g.antennaStatus));
    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString(pwrStr, cx + 30, cy + 78);

    // Right sub-box: RF Jamming + Noise floor
    canvas.fillRoundRect(cx + 308, cy + 48, 276, 54, 10, 0x121C28U);
    canvas.drawRoundRect(cx + 308, cy + 48, 276, 54, 10, jamCol);
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextColor(jamCol);
    canvas.setTextDatum(top_left);
    canvas.drawString(jamTitle, cx + 320, cy + 54);

    char rfStr[64];
    snprintf(rfStr, sizeof(rfStr), "底噪 %u  |  AGC %u  |  CW %u",
             static_cast<unsigned>(g.rfNoiseLevel),
             static_cast<unsigned>(g.rfAgcCount),
             static_cast<unsigned>(g.cwJammingIndicator));
    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString(rfStr, cx + 320, cy + 78);

    canvas.drawFastHLine(cx + 18, cy + 110, cw - 36, 0x1B2634U);

    // AssistNow Online AGPS Row + Action Button
    const char* agpsTxt = "空闲待命";
    uint32_t agpsCol    = ThemeColors::TEXT_MUTED;
    switch (net.agpsStatus) {
        case AGPS_STATE_DOWNLOADING:
            agpsTxt = "正在下载星历...";
            agpsCol = ThemeColors::ICE_CYAN;
            break;
        case AGPS_STATE_INJECTED:
            agpsTxt = "星历已成功注入";
            agpsCol = ThemeColors::NEON_GREEN;
            break;
        case AGPS_STATE_ERROR:
            agpsTxt = "星历获取失败";
            agpsCol = ThemeColors::ALERT_RED;
            break;
        default:
            break;
    }

    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextColor(agpsCol);
    canvas.setTextDatum(top_left);
    char agpsHdr[64];
    snprintf(agpsHdr, sizeof(agpsHdr), "AssistNow AGPS: %s", agpsTxt);
    canvas.drawString(agpsHdr, cx + 18, cy + 124);

    char agpsSub[64];
    snprintf(agpsSub, sizeof(agpsSub), "累计注入字节: %u B  |  秒级热启动辅助",
             static_cast<unsigned>(net.agpsInjectedBytes));
    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString(agpsSub, cx + 18, cy + 154);

    const uint32_t btnBg = _agpsPressed ? ThemeColors::BG_CARD_HIGHLIGHT : 0x102834U;
    canvas.fillRoundRect(BTN_AGPS_X, BTN_AGPS_Y, BTN_AGPS_W, BTN_AGPS_H, 10, btnBg);
    canvas.drawRoundRect(BTN_AGPS_X, BTN_AGPS_Y, BTN_AGPS_W, BTN_AGPS_H, 10, ThemeColors::ICE_CYAN);
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.setTextDatum(middle_center);
    canvas.drawString("立即注入 AGPS", BTN_AGPS_X + BTN_AGPS_W / 2, BTN_AGPS_Y + BTN_AGPS_H / 2);
}

bool ToolGnss::onTouch(const TouchEvent& event, const SystemStateSnapshot& state) {
    if (event.type == TOUCH_EVT_DOWN) {
        _agpsPressed = event.hitTestLocal(BTN_AGPS_X, BTN_AGPS_Y, BTN_AGPS_W, BTN_AGPS_H);
        return true;
    }

    if (event.type == TOUCH_EVT_UP) {
        _agpsPressed = false;
        return true;
    }

    if (event.type == TOUCH_EVT_TAP) {
        _agpsPressed = false;

        if (event.hitTestLocal(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H)) {
            playFeedbackTone(state);
            requestNavigate(PAGE_TOOLS_GRID);
            return true;
        }

        if (event.hitTestLocal(BTN_AGPS_X, BTN_AGPS_Y, BTN_AGPS_W, BTN_AGPS_H)) {
            {
                StateGuard lock(SystemState::instance());
                if (lock.locked()) {
                    SystemState::instance().data().network.cmdRunAgpsNow = true;
                }
            }
            playFeedbackTone(state, HapticConfig::TONE_CONFIRM_1_HZ, HapticConfig::TONE_CONFIRM_MS);
            return true;
        }
    }

    return true;
}
