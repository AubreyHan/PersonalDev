#include "PageInit.h"
#include <stdio.h>
#include <math.h>

namespace {

constexpr int16_t BTN_WIFI_X = 784;
constexpr int16_t BTN_WIFI_Y = 472;
constexpr int16_t BTN_WIFI_W = 300;
constexpr int16_t BTN_WIFI_H = 72;

constexpr int16_t BTN_SKIP_X = 784;
constexpr int16_t BTN_SKIP_Y = 598;
constexpr int16_t BTN_SKIP_W = 300;
constexpr int16_t BTN_SKIP_H = 76;

constexpr int16_t MODAL_X = 200;
constexpr int16_t MODAL_Y = 170;
constexpr int16_t MODAL_W = 720;
constexpr int16_t MODAL_H = 380;

constexpr int16_t BTN_FMT_OK_X = MODAL_X + 48;
constexpr int16_t BTN_FMT_OK_Y = MODAL_Y + 272;
constexpr int16_t BTN_FMT_OK_W = 330;
constexpr int16_t BTN_FMT_OK_H = 72;

constexpr int16_t BTN_FMT_SKIP_X = MODAL_X + 404;
constexpr int16_t BTN_FMT_SKIP_Y = MODAL_Y + 272;
constexpr int16_t BTN_FMT_SKIP_W = 268;
constexpr int16_t BTN_FMT_SKIP_H = 72;

uint32_t constellationColor(uint8_t gnssId) {
    switch (gnssId) {
        case GNSS_CONST_BEIDOU:  return ThemeColors::NEON_GREEN;
        case GNSS_CONST_GPS:     return ThemeColors::ICE_CYAN;
        case GNSS_CONST_GLONASS: return ThemeColors::RACING_YELLOW;
        case GNSS_CONST_GALILEO: return ThemeColors::AMBER_WARN;
        default:                 return ThemeColors::TEXT_WHITE;
    }
}

} // namespace

void PageInit::onEnter(const SystemStateSnapshot& state) {
    (void)state;
    _enterMs            = millis();
    _pressedBtn         = 0;
    _autoEnterTriggered = false;
}

bool PageInit::shouldShowFormatModal(const SystemStateSnapshot& state) const {
    if (_formatModalDismissed) {
        return false;
    }
    if (state.storage.isFormatting) {
        return true;
    }
    return (state.boot.tfCardFormatIncompatible || state.storage.formatIncompatible);
}

void PageInit::render(M5Canvas& canvas, const SystemStateSnapshot& state) {
    clearViewportCanvas(canvas);
    const uint32_t nowMs = millis();

    // Auto-enter PAGE_1_DASHBOARD upon valid 3D GNSS lock (>= 4 satellites) when modal is not blocking
    if (!_autoEnterTriggered &&
        !state.boot.autoEnteredDashboard &&
        !shouldShowFormatModal(state) &&
        state.gnss.gnssFixOk &&
        state.gnss.fixType >= FIX_3D &&
        state.gnss.numSats >= 4U) {
        _autoEnterTriggered = true;
        {
            StateGuard guard(SystemState::instance());
            if (guard.locked()) {
                SystemState::instance().data().boot.autoEnteredDashboard = true;
            }
        }
        playFeedbackTone(state, HapticConfig::TONE_CONFIRM_2_HZ, HapticConfig::TONE_CONFIRM_MS);
        requestNavigate(PAGE_1_DASHBOARD);
    }

    drawTopHeader(canvas, state);
    drawHardwareChecklist(canvas, state);
    drawRadarSkyplot(canvas, state, nowMs);
    drawRightControlPanel(canvas, state);

    if (shouldShowFormatModal(state)) {
        drawFormatFat32Modal(canvas, state, nowMs);
    }
}

void PageInit::drawTopHeader(M5Canvas& canvas, const SystemStateSnapshot& state) {
    canvas.fillRoundRect(18, 12, 1084, 52, 10, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(18, 12, 1084, 52, 10, ThemeColors::BORDER_SUBTLE);

    canvas.setTextDatum(textdatum_t::middle_left);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.drawString("M5Track-Tab5 车规硬件自检与快速搜星", 36, 38);

    // Progress bar + status text on right side of header
    const uint8_t pct = (state.boot.selfTestComplete || state.boot.progressPercent > 100U)
                            ? 100U
                            : state.boot.progressPercent;
    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextDatum(textdatum_t::middle_right);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString(state.boot.statusMessage, 895, 38);

    const int16_t barX = 910;
    const int16_t barY = 28;
    const int16_t barW = 120;
    const int16_t barH = 20;
    canvas.fillRoundRect(barX, barY, barW, barH, 6, 0x18222EU);
    const int16_t fillW = static_cast<int16_t>((static_cast<int32_t>(barW) * pct) / 100);
    if (fillW > 0) {
        canvas.fillRoundRect(barX, barY, fillW, barH, 6, ThemeColors::NEON_GREEN);
    }
    canvas.drawRoundRect(barX, barY, barW, barH, 6, ThemeColors::BORDER_SUBTLE);

    char pctBuf[12];
    snprintf(pctBuf, sizeof(pctBuf), "%u%%", static_cast<unsigned>(pct));
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextDatum(textdatum_t::middle_right);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString(pctBuf, 1088, 38);
}

void PageInit::drawHardwareChecklist(M5Canvas& canvas, const SystemStateSnapshot& state) {
    const int16_t cardX = 18;
    const int16_t cardY = 76;
    const int16_t cardW = 356;
    const int16_t cardH = 626;

    canvas.fillRoundRect(cardX, cardY, cardW, cardH, 12, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(cardX, cardY, cardW, cardH, 12, ThemeColors::BORDER_SUBTLE);

    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextDatum(textdatum_t::middle_left);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString("总线与外设自检清单 (8项)", cardX + 16, cardY + 26);
    canvas.drawFastHLine(cardX + 14, cardY + 46, cardW - 28, ThemeColors::BORDER_SUBTLE);

    struct HwRow {
        const char* nameCn;
        char        busTag[28];
        bool        ok;
        bool        warn;
        const char* stateLabel;
    };

    const bool imuMasterOk = state.boot.imuMasterOnline || state.imu.masterOnline;
    const bool imuSlaveOk  = state.boot.imuSlaveOnline  || state.imu.slaveOnline;
    const bool baroOk      = state.boot.baroOnline      || state.baro.isOnline;
    const bool rtcOk       = state.boot.rtcOnline;
    const bool inaOk       = state.boot.ina226Online    || state.power.isAvailable;
    const bool gnssOk      = state.boot.gnssUartOnline  || state.gnss.activeSatCount > 0;
    const bool tfBadFmt    = state.boot.tfCardFormatIncompatible || state.storage.formatIncompatible;
    const bool tfOk        = (state.boot.tfCardMounted  || state.storage.isMounted) && !tfBadFmt;
    const bool c6Ok        = state.boot.wifiCoProcOnline || state.network.staConnected || state.network.softApActive;

    HwRow rows[8];
    rows[0] = { "Tab5 主六轴 IMU", "I2C 0x68 BMI270", imuMasterOk, false, imuMasterOk ? "ONLINE" : "WAIT" };
    rows[1] = { "M135 副六轴 IMU", "I2C 0x69 BMI270", imuSlaveOk,  false, imuSlaveOk  ? "ONLINE" : "WAIT" };
    rows[2] = { "M135 气压高度计", "I2C 0x76 BMP280", baroOk,      false, baroOk      ? "ONLINE" : "WAIT" };
    rows[3] = { "Tab5 硬件实时钟", "I2C 0x32 RX8130CE", rtcOk,     state.time.rtcVoltageLowVlf,
                rtcOk ? (state.time.rtcVoltageLowVlf ? "VLF!" : "ONLINE") : "WAIT" };
    rows[4] = { "Tab5 电源库仑计", "I2C 0x41 INA226 5m\xCE\xA9", inaOk, false, inaOk ? "ONLINE" : "WAIT" };

    snprintf(rows[5].busTag, sizeof(rows[5].busTag), "UART RX%d/TX%d",
             static_cast<int>(state.boot.gnssActiveRxPin),
             static_cast<int>(state.boot.gnssActiveTxPin));
    rows[5].nameCn     = "NEO-M9N 四星定位";
    rows[5].ok         = gnssOk;
    rows[5].warn       = false;
    rows[5].stateLabel = gnssOk ? "115200" : "PROBE";

    snprintf(rows[6].busTag, sizeof(rows[6].busTag), "SPI %uMHz CS42",
             static_cast<unsigned>(SdCardConfig::SPI_FREQ_MHZ));
    rows[6].nameCn     = "MicroSD 轨迹存储";
    rows[6].ok         = tfOk;
    rows[6].warn       = tfBadFmt;
    rows[6].stateLabel = tfBadFmt ? "非FAT32" : (tfOk ? "FAT32" : "NO CARD");

    rows[7] = { "ESP32-C6 协处理器", "SDIO Wi-Fi 6", c6Ok, false, c6Ok ? "READY" : "INIT" };

    const int16_t startY = cardY + 58;
    const int16_t rowH   = 69;

    for (int i = 0; i < 8; ++i) {
        const int16_t ry = startY + i * rowH;
        canvas.fillRoundRect(cardX + 12, ry, cardW - 24, rowH - 7, 8, 0x111922U);

        uint32_t dotColor = ThemeColors::TEXT_MUTED;
        if (rows[i].warn) {
            dotColor = ThemeColors::AMBER_WARN;
        } else if (rows[i].ok) {
            dotColor = ThemeColors::NEON_GREEN;
        } else if (state.boot.selfTestComplete) {
            dotColor = ThemeColors::ALERT_RED;
        }

        // Status indicator icon
        const int16_t icx = cardX + 30;
        const int16_t icy = ry + (rowH - 7) / 2;
        canvas.fillSmoothCircle(icx, icy, 8, dotColor);
        if (rows[i].ok && !rows[i].warn) {
            canvas.drawWideLine(icx - 4, icy, icx - 1, icy + 3, 2.0f, TFT_BLACK);
            canvas.drawWideLine(icx - 1, icy + 3, icx + 4, icy - 3, 2.0f, TFT_BLACK);
        } else if (rows[i].warn || state.boot.selfTestComplete) {
            canvas.fillRect(icx - 1, icy - 4, 2, 5, TFT_BLACK);
            canvas.fillRect(icx - 1, icy + 3, 2, 2, TFT_BLACK);
        }

        canvas.setFont(&fonts::efontCN_16_b);
        canvas.setTextDatum(textdatum_t::top_left);
        canvas.setTextColor(ThemeColors::TEXT_WHITE);
        canvas.drawString(rows[i].nameCn, cardX + 48, ry + 8);

        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextColor(ThemeColors::TEXT_MUTED);
        canvas.drawString(rows[i].busTag, cardX + 48, ry + 34);

        // Right pill badge
        const int16_t pillX = cardX + cardW - 98;
        const int16_t pillY = ry + 16;
        canvas.drawRoundRect(pillX, pillY, 78, 28, 6, dotColor);
        canvas.setFont(&fonts::efontCN_12_b);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.setTextColor(dotColor);
        canvas.drawString(rows[i].stateLabel, pillX + 39, pillY + 14);
    }
}

void PageInit::drawRadarSkyplot(M5Canvas& canvas, const SystemStateSnapshot& state, uint32_t nowMs) {
    const int16_t cardX = 388;
    const int16_t cardY = 76;
    const int16_t cardW = 368;
    const int16_t cardH = 626;

    canvas.fillRoundRect(cardX, cardY, cardW, cardH, 12, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(cardX, cardY, cardW, cardH, 12, ThemeColors::BORDER_SUBTLE);

    // Header
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextDatum(textdatum_t::middle_left);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.drawString("四星并发动态搜星雷达", cardX + 16, cardY + 26);

    const bool locked3d = state.gnss.gnssFixOk && state.gnss.fixType >= FIX_3D;
    canvas.setTextDatum(textdatum_t::middle_right);
    canvas.setTextColor(locked3d ? ThemeColors::NEON_GREEN : ThemeColors::RACING_YELLOW);
    canvas.drawString(locked3d ? "3D 已锁定" : "正在搜星...", cardX + cardW - 16, cardY + 26);
    canvas.drawFastHLine(cardX + 14, cardY + 46, cardW - 28, ThemeColors::BORDER_SUBTLE);

    // Polar radar center & radius
    const int16_t cx   = cardX + cardW / 2;
    const int16_t cy   = cardY + 236;
    const int16_t maxR = 148;

    canvas.fillSmoothCircle(cx, cy, maxR + 4, 0x060B10U);

    // Rotating radar sweep trail (90 deg/sec)
    const float sweepDeg = fmodf(static_cast<float>(nowMs) * 0.09f, 360.0f);
    // Convert compass azimuth (0=N=top, clockwise) to LGFXfillArc angle (0=3 o'clock, clockwise): lgfx = azim - 90
    const float headLgfx = sweepDeg - 90.0f;
    canvas.fillArc(cx, cy, maxR - 2, 6, headLgfx - 42.0f, headLgfx - 26.0f, 0x041D16U);
    canvas.fillArc(cx, cy, maxR - 2, 6, headLgfx - 26.0f, headLgfx - 12.0f, 0x073628U);
    canvas.fillArc(cx, cy, maxR - 2, 6, headLgfx - 12.0f, headLgfx,         0x0B5C43U);

    // Concentric elevation rings (0, 30, 60 deg) + crosshairs
    canvas.drawCircle(cx, cy, maxR, ThemeColors::BORDER_SUBTLE);
    canvas.drawCircle(cx, cy, (maxR * 2) / 3, 0x1A2634U);
    canvas.drawCircle(cx, cy, maxR / 3, 0x1A2634U);
    canvas.drawFastHLine(cx - maxR, cy, maxR * 2, 0x1A2634U);
    canvas.drawFastVLine(cx, cy - maxR, maxR * 2, 0x1A2634U);

    // Leading radar beam line
    const float beamRad = sweepDeg * (static_cast<float>(M_PI) / 180.0f);
    const int16_t bx = cx + static_cast<int16_t>(lroundf(sinf(beamRad) * maxR));
    const int16_t by = cy - static_cast<int16_t>(lroundf(cosf(beamRad) * maxR));
    canvas.drawWideLine(cx, cy, bx, by, 2.2f, ThemeColors::NEON_GREEN);
    canvas.fillSmoothCircle(cx, cy, 4, ThemeColors::NEON_GREEN);

    // Cardinal labels
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextColor(ThemeColors::ALERT_RED);
    canvas.drawString("N", cx, cy - maxR - 12);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("S", cx, cy + maxR + 12);
    canvas.drawString("W", cx - maxR - 12, cy);
    canvas.drawString("E", cx + maxR + 12, cy);

    // Plot live tracked satellites from UBX-NAV-SAT snapshot
    const uint8_t count = (state.gnss.activeSatCount <= GnssData::MAX_TRACKED_SATS)
                              ? state.gnss.activeSatCount
                              : static_cast<uint8_t>(GnssData::MAX_TRACKED_SATS);
    for (uint8_t i = 0; i < count; ++i) {
        const SatInfo& sat = state.gnss.sats[i];
        const float elevClamped = fminf(90.0f, fmaxf(0.0f, static_cast<float>(sat.elev)));
        const float r = (1.0f - (elevClamped / 90.0f)) * static_cast<float>(maxR - 10);
        const float azRad = static_cast<float>(sat.azim) * (static_cast<float>(M_PI) / 180.0f);
        const int16_t sx = cx + static_cast<int16_t>(lroundf(sinf(azRad) * r));
        const int16_t sy = cy - static_cast<int16_t>(lroundf(cosf(azRad) * r));
        const uint32_t col = constellationColor(sat.gnssId);

        if (sat.used) {
            canvas.fillSmoothCircle(sx, sy, 6, col);
            canvas.drawCircle(sx, sy, 8, ThemeColors::TEXT_WHITE);
        } else {
            canvas.drawCircle(sx, sy, 5, col);
        }
    }

    // Telemetry readout below radar
    const int16_t statY = cardY + 414;
    canvas.fillRoundRect(cardX + 14, statY, cardW - 28, 92, 10, 0x111922U);

    char satBuf[32];
    snprintf(satBuf, sizeof(satBuf), "%u / %u",
             static_cast<unsigned>(state.gnss.numSats),
             static_cast<unsigned>(state.gnss.satsInView));
    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("解算 / 可见星数", cardX + 28, statY + 12);
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextColor(locked3d ? ThemeColors::NEON_GREEN : ThemeColors::TEXT_WHITE);
    canvas.drawString(satBuf, cardX + 28, statY + 38);

    char hdopBuf[24];
    if (state.gnss.hdop < 50.0f) {
        snprintf(hdopBuf, sizeof(hdopBuf), "%.1f", static_cast<double>(state.gnss.hdop));
    } else {
        snprintf(hdopBuf, sizeof(hdopBuf), "--.-");
    }
    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextDatum(textdatum_t::top_right);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("水平精度因子 HDOP", cardX + cardW - 28, statY + 12);
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString(hdopBuf, cardX + cardW - 28, statY + 38);

    // 4-Constellation breakdown pills
    struct ConstBadge {
        const char* tag;
        uint8_t     used;
        uint8_t     view;
        uint32_t    color;
    };
    const ConstBadge badges[4] = {
        { "BDS 北斗", state.gnss.numBdsSats, state.gnss.viewBdsSats, ThemeColors::NEON_GREEN },
        { "GPS",      state.gnss.numGpsSats, state.gnss.viewGpsSats, ThemeColors::ICE_CYAN },
        { "GLONASS",  state.gnss.numGloSats, state.gnss.viewGloSats, ThemeColors::RACING_YELLOW },
        { "Galileo",  state.gnss.numGalSats, state.gnss.viewGalSats, ThemeColors::AMBER_WARN }
    };

    const int16_t gridY = statY + 104;
    const int16_t cellW = (cardW - 36) / 2;
    const int16_t cellH = 44;

    for (int i = 0; i < 4; ++i) {
        const int16_t gx = cardX + 14 + (i % 2) * (cellW + 8);
        const int16_t gy = gridY + (i / 2) * (cellH + 8);
        canvas.fillRoundRect(gx, gy, cellW, cellH, 8, 0x111922U);
        canvas.fillSmoothCircle(gx + 14, gy + cellH / 2, 5, badges[i].color);

        canvas.setFont(&fonts::efontCN_14);
        canvas.setTextDatum(textdatum_t::middle_left);
        canvas.setTextColor(ThemeColors::TEXT_WHITE);
        canvas.drawString(badges[i].tag, gx + 26, gy + cellH / 2);

        char cnt[16];
        snprintf(cnt, sizeof(cnt), "%u/%u",
                 static_cast<unsigned>(badges[i].used),
                 static_cast<unsigned>(badges[i].view));
        canvas.setFont(&fonts::FreeSansBold9pt7b);
        canvas.setTextDatum(textdatum_t::middle_right);
        canvas.setTextColor(badges[i].color);
        canvas.drawString(cnt, gx + cellW - 10, gy + cellH / 2);
    }
}

void PageInit::drawRightControlPanel(M5Canvas& canvas, const SystemStateSnapshot& state) {
    const int16_t cardX = 770;
    const int16_t cardY = 76;
    const int16_t cardW = 332;
    const int16_t cardH = 626;

    canvas.fillRoundRect(cardX, cardY, cardW, cardH, 12, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(cardX, cardY, cardW, cardH, 12, ThemeColors::BORDER_SUBTLE);

    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextDatum(textdatum_t::middle_left);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString("无线连接与快速星历注入", cardX + 16, cardY + 26);
    canvas.drawFastHLine(cardX + 14, cardY + 46, cardW - 28, ThemeColors::BORDER_SUBTLE);

    // Wi-Fi status box
    const int16_t wfY = cardY + 62;
    canvas.fillRoundRect(cardX + 14, wfY, cardW - 28, 152, 10, 0x111922U);

    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("Wi-Fi 6 状态", cardX + 28, wfY + 14);

    canvas.setFont(&fonts::efontCN_24_b);
    if (state.network.staConnected) {
        canvas.setTextColor(ThemeColors::NEON_GREEN);
        canvas.drawString(state.network.connectedSsid[0] ? state.network.connectedSsid : "已连接热点",
                          cardX + 28, wfY + 40);

        char ipLine[48];
        snprintf(ipLine, sizeof(ipLine), "IP: %s  (%ddBm)",
                 state.network.staIp, static_cast<int>(state.network.rssiDbm));
        canvas.setFont(&fonts::efontCN_14);
        canvas.setTextColor(ThemeColors::TEXT_WHITE);
        canvas.drawString(ipLine, cardX + 28, wfY + 80);
    } else if (state.network.softApActive) {
        canvas.setTextColor(ThemeColors::RACING_YELLOW);
        canvas.drawString(state.network.apSsid, cardX + 28, wfY + 40);
        canvas.setFont(&fonts::efontCN_14);
        canvas.setTextColor(ThemeColors::TEXT_WHITE);
        canvas.drawString("配网热点运行中 (192.168.4.1)", cardX + 28, wfY + 80);
    } else {
        canvas.setTextColor(ThemeColors::AMBER_WARN);
        canvas.drawString("未连接 Wi-Fi", cardX + 28, wfY + 40);
        canvas.setFont(&fonts::efontCN_14);
        canvas.setTextColor(ThemeColors::TEXT_MUTED);
        canvas.drawString(state.network.statusText, cardX + 28, wfY + 80);
    }

    // AGPS & Time calibration status
    const char* agpsStr = "离线温启动待命";
    uint32_t agpsCol    = ThemeColors::TEXT_MUTED;
    if (state.network.agpsStatus == AGPS_STATE_DOWNLOADING) {
        agpsStr = "AssistNow 星历下载中...";
        agpsCol = ThemeColors::ICE_CYAN;
    } else if (state.network.agpsStatus == AGPS_STATE_INJECTED) {
        agpsStr = "AssistNow 星历已注入";
        agpsCol = ThemeColors::NEON_GREEN;
    } else if (state.network.agpsStatus == AGPS_STATE_ERROR) {
        agpsStr = "AGPS 失败 (已回退离线星历)";
        agpsCol = ThemeColors::AMBER_WARN;
    }
    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(agpsCol);
    canvas.drawString(agpsStr, cardX + 28, wfY + 114);

    // Power & RTC quick summary box
    const int16_t pwrY = wfY + 168;
    canvas.fillRoundRect(cardX + 14, pwrY, cardW - 28, 138, 10, 0x111922U);

    const uint32_t pwrColor = state.power.isExternalPower
                                  ? ThemeColors::POWER_EXT_GREEN
                                  : ThemeColors::POWER_BATT_ONLY_RED;
    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("车载供电与 NP-F550 电池", cardX + 28, pwrY + 14);

    char pwrLine[48];
    snprintf(pwrLine, sizeof(pwrLine), "%s  %.0f%% (%.2fV)",
             state.power.isExternalPower ? "外接供电正常" : "纯电池放电中",
             static_cast<double>(state.power.socPercent),
             static_cast<double>(state.power.busVoltageV));
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextColor(pwrColor);
    canvas.drawString(pwrLine, cardX + 28, pwrY + 44);

    char hintLine[64];
    snprintf(hintLine, sizeof(hintLine), "锁星自动进入条件: 3D Fix 且 >= 4 颗卫星");
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString(hintLine, cardX + 28, pwrY + 86);
    canvas.drawString("您也可随时扫码配网加速搜星或手动跳过", cardX + 28, pwrY + 108);

    // Button 1: Scan QR Wi-Fi Setup
    const bool wifiPressed = (_pressedBtn == 1);
    canvas.fillRoundRect(BTN_WIFI_X, BTN_WIFI_Y, BTN_WIFI_W, BTN_WIFI_H, 12,
                         wifiPressed ? ThemeColors::BG_CARD_HIGHLIGHT : 0x122636U);
    canvas.drawRoundRect(BTN_WIFI_X, BTN_WIFI_Y, BTN_WIFI_W, BTN_WIFI_H, 12, ThemeColors::ICE_CYAN);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString("扫码极速配网", BTN_WIFI_X + BTN_WIFI_W / 2, BTN_WIFI_Y + BTN_WIFI_H / 2);

    // Button 2: Skip & Enter Dashboard
    const bool skipPressed = (_pressedBtn == 2);
    canvas.fillRoundRect(BTN_SKIP_X, BTN_SKIP_Y, BTN_SKIP_W, BTN_SKIP_H, 12,
                         skipPressed ? 0x00C988U : ThemeColors::NEON_GREEN);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextColor(TFT_BLACK);
    canvas.drawString("进入主仪表盘  ->", BTN_SKIP_X + BTN_SKIP_W / 2, BTN_SKIP_Y + BTN_SKIP_H / 2);
}

void PageInit::drawFormatFat32Modal(M5Canvas& canvas, const SystemStateSnapshot& state, uint32_t nowMs) {
    // Dim backdrop over 1120x720 viewport
    canvas.fillRectAlpha(0, 0, Viewport::SAFE_W, Viewport::SAFE_H, 190, TFT_BLACK);

    canvas.fillRoundRect(MODAL_X, MODAL_Y, MODAL_W, MODAL_H, 16, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(MODAL_X, MODAL_Y, MODAL_W, MODAL_H, 16, ThemeColors::AMBER_WARN);
    canvas.drawRoundRect(MODAL_X + 1, MODAL_Y + 1, MODAL_W - 2, MODAL_H - 2, 15, ThemeColors::AMBER_WARN);

    // Warning triangle header
    const int16_t tx = MODAL_X + 54;
    const int16_t ty = MODAL_Y + 46;
    canvas.fillTriangle(tx, ty - 18, tx - 20, ty + 16, tx + 20, ty + 16, ThemeColors::AMBER_WARN);
    canvas.fillRect(tx - 2, ty - 7, 4, 12, TFT_BLACK);
    canvas.fillRect(tx - 2, ty + 8, 4, 4, TFT_BLACK);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextDatum(textdatum_t::middle_left);
    canvas.setTextColor(ThemeColors::AMBER_WARN);
    canvas.drawString("检测到 TF 卡文件系统不兼容 (非 FAT32)", MODAL_X + 88, ty);

    canvas.drawFastHLine(MODAL_X + 32, MODAL_Y + 82, MODAL_W - 64, ThemeColors::BORDER_SUBTLE);

    if (state.storage.isFormatting) {
        canvas.setFont(&fonts::efontCN_24_b);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.setTextColor(ThemeColors::NEON_GREEN);
        canvas.drawString("正在机内格式化为车规标准 FAT32 并创建目录...",
                          MODAL_X + MODAL_W / 2, MODAL_Y + 165);

        const float spinStart = fmodf(static_cast<float>(nowMs) * 0.36f, 360.0f);
        canvas.fillArc(MODAL_X + MODAL_W / 2, MODAL_Y + 255, 34, 26,
                       spinStart, spinStart + 240.0f, ThemeColors::NEON_GREEN);
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextColor(ThemeColors::TEXT_MUTED);
        canvas.drawString("请勿拔出 TF 卡或断电", MODAL_X + MODAL_W / 2, MODAL_Y + 325);
        return;
    }

    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString("大容量新卡默认多为 exFAT 格式。为保障极端断电安全与高吞吐落盘，", MODAL_X + 48, MODAL_Y + 106);
    canvas.drawString("本机支持免电脑一键重建标准 MBR + FAT32 分区及三层轨迹目录。", MODAL_X + 48, MODAL_Y + 140);

    canvas.setTextColor(ThemeColors::ALERT_RED);
    canvas.drawString("注意：格式化将清空该 TF 卡内所有现有文件，操作不可撤销！", MODAL_X + 48, MODAL_Y + 190);

    // Confirm One-Tap Format Button
    const bool okPressed = (_pressedBtn == 3);
    canvas.fillRoundRect(BTN_FMT_OK_X, BTN_FMT_OK_Y, BTN_FMT_OK_W, BTN_FMT_OK_H, 12,
                         okPressed ? 0xCC2E25U : ThemeColors::ALERT_RED);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString("一键格式化为 FAT32", BTN_FMT_OK_X + BTN_FMT_OK_W / 2, BTN_FMT_OK_Y + BTN_FMT_OK_H / 2);

    // Dismiss Button
    const bool skipPressed = (_pressedBtn == 4);
    canvas.fillRoundRect(BTN_FMT_SKIP_X, BTN_FMT_SKIP_Y, BTN_FMT_SKIP_W, BTN_FMT_SKIP_H, 12,
                         skipPressed ? ThemeColors::BG_CARD_HIGHLIGHT : 0x18222EU);
    canvas.drawRoundRect(BTN_FMT_SKIP_X, BTN_FMT_SKIP_Y, BTN_FMT_SKIP_W, BTN_FMT_SKIP_H, 12,
                         ThemeColors::BORDER_SUBTLE);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("暂不处理", BTN_FMT_SKIP_X + BTN_FMT_SKIP_W / 2, BTN_FMT_SKIP_Y + BTN_FMT_SKIP_H / 2);
}

bool PageInit::onTouch(const TouchEvent& event, const SystemStateSnapshot& state) {
    if (shouldShowFormatModal(state)) {
        if (state.storage.isFormatting) {
            return true; // Absorb all touches while formatting
        }
        if (event.type == TOUCH_EVT_DOWN || event.type == TOUCH_EVT_MOVE) {
            if (event.hitTestLocal(BTN_FMT_OK_X, BTN_FMT_OK_Y, BTN_FMT_OK_W, BTN_FMT_OK_H)) {
                _pressedBtn = 3;
            } else if (event.hitTestLocal(BTN_FMT_SKIP_X, BTN_FMT_SKIP_Y, BTN_FMT_SKIP_W, BTN_FMT_SKIP_H)) {
                _pressedBtn = 4;
            } else {
                _pressedBtn = 0;
            }
            return true;
        }
        if (event.type == TOUCH_EVT_TAP) {
            _pressedBtn = 0;
            if (event.hitTestLocal(BTN_FMT_OK_X, BTN_FMT_OK_Y, BTN_FMT_OK_W, BTN_FMT_OK_H)) {
                playFeedbackTone(state, HapticConfig::TONE_WARN_HZ, HapticConfig::TONE_WARN_MS);
                StateGuard guard(SystemState::instance());
                if (guard.locked()) {
                    SystemState::instance().data().storage.cmdFormatFat32 = true;
                }
                return true;
            }
            if (event.hitTestLocal(BTN_FMT_SKIP_X, BTN_FMT_SKIP_Y, BTN_FMT_SKIP_W, BTN_FMT_SKIP_H)) {
                playFeedbackTone(state);
                _formatModalDismissed = true;
                return true;
            }
            return true;
        }
        if (event.type == TOUCH_EVT_UP) {
            _pressedBtn = 0;
            return true;
        }
        return true;
    }

    if (event.type == TOUCH_EVT_DOWN || event.type == TOUCH_EVT_MOVE) {
        if (event.hitTestLocal(BTN_WIFI_X, BTN_WIFI_Y, BTN_WIFI_W, BTN_WIFI_H)) {
            _pressedBtn = 1;
            return true;
        }
        if (event.hitTestLocal(BTN_SKIP_X, BTN_SKIP_Y, BTN_SKIP_W, BTN_SKIP_H)) {
            _pressedBtn = 2;
            return true;
        }
        _pressedBtn = 0;
        return false;
    }

    if (event.type == TOUCH_EVT_TAP) {
        _pressedBtn = 0;
        if (event.hitTestLocal(BTN_WIFI_X, BTN_WIFI_Y, BTN_WIFI_W, BTN_WIFI_H)) {
            playFeedbackTone(state);
            {
                StateGuard guard(SystemState::instance());
                if (guard.locked()) {
                    SystemState::instance().data().network.cmdStartSetupAp = true;
                }
            }
            requestNavigate(PAGE_WIFI_CONFIG);
            return true;
        }
        if (event.hitTestLocal(BTN_SKIP_X, BTN_SKIP_Y, BTN_SKIP_W, BTN_SKIP_H)) {
            playFeedbackTone(state, HapticConfig::TONE_CONFIRM_1_HZ, HapticConfig::TONE_CONFIRM_MS);
            {
                StateGuard guard(SystemState::instance());
                if (guard.locked()) {
                    SystemState::instance().data().boot.autoEnteredDashboard = true;
                }
            }
            requestNavigate(PAGE_1_DASHBOARD);
            return true;
        }
    }

    if (event.type == TOUCH_EVT_UP) {
        _pressedBtn = 0;
    }

    return false;
}
