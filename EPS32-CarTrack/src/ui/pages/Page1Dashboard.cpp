#include "Page1Dashboard.h"
#include <stdio.h>
#include <math.h>

namespace {

// Compass geometry: strict 360px diameter (R = 180)
constexpr int16_t COMPASS_CX = 280;
constexpr int16_t COMPASS_CY = 266;
constexpr int16_t COMPASS_R  = 180;

// Bottom-left mini G-force / attitude card
constexpr int16_t DYN_CARD_X = 28;
constexpr int16_t DYN_CARD_Y = 536;
constexpr int16_t DYN_CARD_W = 516;
constexpr int16_t DYN_CARD_H = 144;

// Right column cards (aligned X = 580..1080, width = 500)
constexpr int16_t RIGHT_X    = TripResetConfig::HITBOX_LOCAL_X; // 580
constexpr int16_t RIGHT_W    = TripResetConfig::HITBOX_W;       // 500

constexpr int16_t SPEED_BOX_X = 568;
constexpr int16_t SPEED_BOX_Y = 66;
constexpr int16_t SPEED_BOX_W = 524;
constexpr int16_t SPEED_BOX_H = 212;

constexpr int16_t ALT_CARD_X  = RIGHT_X;
constexpr int16_t ALT_CARD_Y  = 288;
constexpr int16_t ALT_CARD_W  = RIGHT_W;
constexpr int16_t ALT_CARD_H  = 96;

constexpr int16_t TAB_BAR_X   = RIGHT_X;
constexpr int16_t TAB_BAR_Y   = 396;
constexpr int16_t TAB_BAR_W   = RIGHT_W;
constexpr int16_t TAB_BAR_H   = 50;

// Sprite dimensions for 120pt anti-aliased speed digits (Font8 75px * 1.65 = ~124px)
constexpr int16_t SPD_SPR_W   = 256;
constexpr int16_t SPD_SPR_H   = 88;
constexpr float   SPD_ZOOM    = 1.64f;

} // namespace

Page1Dashboard::~Page1Dashboard() {
    if (_speedSpriteReady) {
        _speedSprite.deleteSprite();
        _speedSpriteReady = false;
    }
}

void Page1Dashboard::ensureSpeedSprite() {
    if (_speedSpriteReady) {
        return;
    }
    _speedSprite.setPsram(true); // MUST precede createSprite
    _speedSprite.setColorDepth(16);
    if (_speedSprite.createSprite(SPD_SPR_W, SPD_SPR_H) != nullptr) {
        _speedSpriteReady = true;
    }
}

void Page1Dashboard::onEnter(const SystemStateSnapshot& state) {
    (void)state;
    ensureSpeedSprite();
    _tripHoldActive     = false;
    _tripResetTriggered = false;
    _tripHoldElapsedMs  = 0;
}

void Page1Dashboard::onExit() {
    _tripHoldActive     = false;
    _tripResetTriggered = false;
    _tripHoldElapsedMs  = 0;
}

TripCategory Page1Dashboard::effectiveCategory(const SystemStateSnapshot& state) {
    TripCategory cat = state.tripControl.activeViewCategory;
    if (cat >= TRIP_CATEGORY_COUNT) {
        return TRIP_SINGLE;
    }
    if (cat == TRIP_MANUAL && !state.tripControl.manualTripActive) {
        return TRIP_SINGLE;
    }
    return cat;
}

const char* Page1Dashboard::headingToCn8(float deg) {
    float norm = fmodf(deg, 360.0f);
    if (norm < 0.0f) norm += 360.0f;
    const int idx = static_cast<int>(floorf((norm + 22.5f) / 45.0f)) & 7;
    static constexpr const char* DIRS_CN[8] = {
        "北", "东北", "东", "东南", "南", "西南", "西", "西北"
    };
    return DIRS_CN[idx];
}

const char* Page1Dashboard::headingToEn8(float deg) {
    float norm = fmodf(deg, 360.0f);
    if (norm < 0.0f) norm += 360.0f;
    const int idx = static_cast<int>(floorf((norm + 22.5f) / 45.0f)) & 7;
    static constexpr const char* DIRS_EN[8] = {
        "N", "NE", "E", "SE", "S", "SW", "W", "NW"
    };
    return DIRS_EN[idx];
}

void Page1Dashboard::render(M5Canvas& canvas, const SystemStateSnapshot& state) {
    clearViewportCanvas(canvas);
    ensureSpeedSprite();

    const uint32_t nowMs = millis();

    // Emit non-blocking 2nd note of rising dual-tone confirmation
    if (_pendingSecondTone && nowMs >= _secondToneDueMs) {
        _pendingSecondTone = false;
        playFeedbackTone(state, HapticConfig::TONE_CONFIRM_2_HZ, HapticConfig::TONE_TRIP_RESET_MS);
    }

    drawTopAlertAndTunnelHud(canvas, state, nowMs);
    drawCompass360(canvas, state);
    drawMiniDynamicsCard(canvas, state);
    drawHeroSpeedometer(canvas, state);
    drawAltitudeAndSlopeCard(canvas, state);
    drawTripDimensionBar(canvas, state);
    drawTripCardAndResetRing(canvas, state, nowMs);
}

void Page1Dashboard::drawTopAlertAndTunnelHud(M5Canvas& canvas, const SystemStateSnapshot& state, uint32_t nowMs) {
    const int16_t barX = 24;
    const int16_t barY = 10;
    const int16_t barW = 1072;
    const int16_t barH = 46;

    // 1. Check Full-Link Trip Reset completion toast (2.5s visibility)
    const bool showResetToast = (state.tripControl.lastResetBannerMillis > 0U) &&
                                ((nowMs - state.tripControl.lastResetBannerMillis) < 2500UL);
    if (showResetToast) {
        canvas.fillRoundRect(barX, barY, barW, barH, 10, 0x093426U);
        canvas.drawRoundRect(barX, barY, barW, barH, 10, ThemeColors::NEON_GREEN);
        canvas.setFont(&fonts::efontCN_16_b);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.setTextColor(ThemeColors::NEON_GREEN);
        canvas.drawString("单次行程已全链路清零 — 里程 / 最高速 / 图表 / 轨迹起点已同步重置",
                          barX + barW / 2, barY + barH / 2);
        return;
    }

    // 2. Check UBX-MON-RF Antenna Fault (ANT_SHORT=3, ANT_OPEN=4; ANT_OK=2 is normal!) & RF Jamming
    const bool antShort = (state.gnss.antennaStatus == ANT_SHORT);
    const bool antOpen  = (state.gnss.antennaStatus == ANT_OPEN);
    const bool jamCrit  = (state.gnss.jammingState == JAMMING_CRIT);
    const bool jamWarn  = (state.gnss.jammingState == JAMMING_WARN);

    if (antShort || antOpen || jamCrit || jamWarn) {
        const bool isCritical = (antShort || antOpen || jamCrit);
        const bool blinkOn    = ((nowMs / 400UL) % 2UL) == 0UL;
        const uint32_t col    = isCritical ? ThemeColors::ALERT_RED : ThemeColors::AMBER_WARN;
        const uint32_t bgCol  = isCritical ? (blinkOn ? 0x3A0D0DU : 0x220808U) : 0x2E1F05U;

        const int16_t alertW = state.gnss.isDeadReckoning ? 830 : barW;
        canvas.fillRoundRect(barX, barY, alertW, barH, 10, bgCol);
        canvas.drawRoundRect(barX, barY, alertW, barH, 10, col);

        char msg[112];
        if (antShort) {
            snprintf(msg, sizeof(msg), "GNSS 有源天线短路告警 (UBX-MON-RF: ANT SHORT) — 请检查 M135 外置天线馈线");
        } else if (antOpen) {
            snprintf(msg, sizeof(msg), "GNSS 天线开路/未接入告警 (UBX-MON-RF: ANT OPEN) — 请检查 SMA 天线接头");
        } else if (jamCrit) {
            snprintf(msg, sizeof(msg), "严重射频干扰告警 (RF JAMMING CRIT | 噪声 %u) — 卫星定位受阻",
                     static_cast<unsigned>(state.gnss.rfNoiseLevel));
        } else {
            snprintf(msg, sizeof(msg), "射频干扰预警 (RF JAMMING WARN | CW %u) — 抗多径与抗干扰滤波已启用",
                     static_cast<unsigned>(state.gnss.cwJammingIndicator));
        }

        canvas.setFont(&fonts::efontCN_16_b);
        canvas.setTextDatum(textdatum_t::middle_left);
        canvas.setTextColor(col);
        canvas.drawString(msg, barX + 18, barY + barH / 2);
    } else if (state.gnss.isInTunnel) {
        // 3. Highway Tunnel Axis Progress HUD
        const int16_t tunW = state.gnss.isDeadReckoning ? 830 : barW;
        canvas.fillRoundRect(barX, barY, tunW, barH, 10, 0x0B2230U);
        canvas.drawRoundRect(barX, barY, tunW, barH, 10, ThemeColors::ICE_CYAN);

        const float totalKm = fmaxf(0.1f, state.gnss.tunnelLengthKm);
        const float progKm  = fminf(totalKm, fmaxf(0.0f, state.gnss.tunnelProgressKm));
        const float remKm   = fmaxf(0.0f, totalKm - progKm);
        const float ratio   = progKm / totalKm;

        char tunTitle[64];
        snprintf(tunTitle, sizeof(tunTitle), "隧道轴线导航: %s",
                 (state.gnss.tunnelName[0] != '\0') ? state.gnss.tunnelName : "特长高速隧道");
        canvas.setFont(&fonts::efontCN_16_b);
        canvas.setTextDatum(textdatum_t::middle_left);
        canvas.setTextColor(ThemeColors::ICE_CYAN);
        canvas.drawString(tunTitle, barX + 16, barY + barH / 2);

        const int16_t pbx = barX + 340;
        const int16_t pby = barY + 14;
        const int16_t pbw = tunW - 550;
        if (pbw > 80) {
            canvas.fillRoundRect(pbx, pby, pbw, 18, 6, 0x142D3EU);
            const int16_t fw = static_cast<int16_t>(lroundf(ratio * pbw));
            if (fw > 0) {
                canvas.fillRoundRect(pbx, pby, fw, 18, 6, ThemeColors::ICE_CYAN);
            }
            canvas.drawRoundRect(pbx, pby, pbw, 18, 6, ThemeColors::BORDER_SUBTLE);
        }

        char remBuf[40];
        snprintf(remBuf, sizeof(remBuf), "剩余 %.2f / %.1f km",
                 static_cast<double>(remKm), static_cast<double>(totalKm));
        canvas.setFont(&fonts::efontCN_14_b);
        canvas.setTextDatum(textdatum_t::middle_right);
        canvas.setTextColor(ThemeColors::TEXT_WHITE);
        canvas.drawString(remBuf, barX + tunW - 16, barY + barH / 2);
    } else {
        // Normal subtle telemetry status strip
        canvas.setFont(&fonts::efontCN_14);
        canvas.setTextDatum(textdatum_t::middle_left);
        canvas.setTextColor(ThemeColors::TEXT_MUTED);

        char topInfo[96];
        snprintf(topInfo, sizeof(topInfo), "NEO-M9N %uHz  |  HDOP %.1f  |  %s",
                 static_cast<unsigned>(GnssConfig::NAV_FREQ_HZ),
                 static_cast<double>(state.gnss.hdop < 50.0f ? state.gnss.hdop : 99.9f),
                 state.gnss.stage2TightFilter ? "Stage-2 抗多径强滤波" : "Stage-1 高灵敏捕获");
        canvas.drawString(topInfo, barX + 6, barY + barH / 2);
    }

    // 4. 6-Axis Dead Reckoning (DR) Corner Badge whenever in blind-zone DR
    if (state.gnss.isDeadReckoning) {
        const int16_t drW = 228;
        const int16_t drX = barX + barW - drW;
        canvas.fillRoundRect(drX, barY, drW, barH, 10, 0x2C1E04U);
        canvas.drawRoundRect(drX, barY, drW, barH, 10, ThemeColors::AMBER_WARN);

        char drText[40];
        snprintf(drText, sizeof(drText), "6轴盲区推算 DR %lus",
                 static_cast<unsigned long>(state.gnss.drDurationSec));
        canvas.setFont(&fonts::efontCN_16_b);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.setTextColor(ThemeColors::AMBER_WARN);
        canvas.drawString(drText, drX + drW / 2, barY + barH / 2);
    }
}

void Page1Dashboard::drawCompass360(M5Canvas& canvas, const SystemStateSnapshot& state) {
    float heading = state.gnss.isValid ? state.gnss.headingDeg : state.imu.fusedHeadingDeg;
    heading = fmodf(heading, 360.0f);
    if (heading < 0.0f) heading += 360.0f;

    // Outer bezel of 360px diameter compass (R = 180)
    canvas.fillSmoothCircle(COMPASS_CX, COMPASS_CY, COMPASS_R, ThemeColors::BG_CARD_DARK);
    canvas.drawArc(COMPASS_CX, COMPASS_CY, COMPASS_R, COMPASS_R - 2, 0.0f, 360.0f, ThemeColors::BORDER_SUBTLE);
    canvas.drawArc(COMPASS_CX, COMPASS_CY, COMPASS_R - 14, COMPASS_R - 15, 0.0f, 360.0f, 0x182330U);

    // Rotating precision scale: dial angle theta on compass appears at screen angle (theta - heading)
    const float degToRad = static_cast<float>(M_PI) / 180.0f;

    for (int deg = 0; deg < 360; deg += 5) {
        const float relDeg = static_cast<float>(deg) - heading;
        const float rad    = relDeg * degToRad;
        const float s      = sinf(rad);
        const float c      = cosf(rad);

        const bool isCardinal = (deg % 90) == 0;
        const bool isMajor30  = (deg % 30) == 0;
        const bool isMed10    = (deg % 10) == 0;

        int16_t rOuter = COMPASS_R - 6;
        int16_t rInner = COMPASS_R - 14;
        uint32_t tickCol = 0x2B3B4EU;
        float thickness  = 1.2f;

        if (isCardinal) {
            rInner    = COMPASS_R - 26;
            tickCol   = (deg == 0) ? ThemeColors::ALERT_RED : ThemeColors::NEON_GREEN;
            thickness = 3.0f;
        } else if (isMajor30) {
            rInner    = COMPASS_R - 22;
            tickCol   = ThemeColors::TEXT_WHITE;
            thickness = 2.0f;
        } else if (isMed10) {
            rInner    = COMPASS_R - 17;
            tickCol   = 0x5B6E82U;
            thickness = 1.5f;
        }

        const int16_t x1 = COMPASS_CX + static_cast<int16_t>(lroundf(s * rOuter));
        const int16_t y1 = COMPASS_CY - static_cast<int16_t>(lroundf(c * rOuter));
        const int16_t x2 = COMPASS_CX + static_cast<int16_t>(lroundf(s * rInner));
        const int16_t y2 = COMPASS_CY - static_cast<int16_t>(lroundf(c * rInner));

        if (thickness >= 2.0f) {
            canvas.drawWideLine(x1, y1, x2, y2, thickness, tickCol);
        } else {
            canvas.drawLine(x1, y1, x2, y2, tickCol);
        }

        // Labels at 30-degree intervals & Cardinals
        if (isCardinal) {
            const float rLbl = static_cast<float>(COMPASS_R - 46);
            const int16_t lx = COMPASS_CX + static_cast<int16_t>(lroundf(s * rLbl));
            const int16_t ly = COMPASS_CY - static_cast<int16_t>(lroundf(c * rLbl));

            const char* cardStr = (deg == 0) ? "N" : (deg == 90) ? "E" : (deg == 180) ? "S" : "W";
            canvas.setFont(&fonts::FreeSansBold18pt7b);
            canvas.setTextDatum(textdatum_t::middle_center);
            canvas.setTextColor((deg == 0) ? ThemeColors::ALERT_RED : ThemeColors::TEXT_WHITE);
            canvas.drawString(cardStr, lx, ly);

            // Red North pointer triangle on rotating ring
            if (deg == 0) {
                const int16_t nx = COMPASS_CX + static_cast<int16_t>(lroundf(s * (COMPASS_R - 4)));
                const int16_t ny = COMPASS_CY - static_cast<int16_t>(lroundf(c * (COMPASS_R - 4)));
                canvas.fillSmoothCircle(nx, ny, 4, ThemeColors::ALERT_RED);
            }
        } else if (isMajor30) {
            const float rLbl = static_cast<float>(COMPASS_R - 42);
            const int16_t lx = COMPASS_CX + static_cast<int16_t>(lroundf(s * rLbl));
            const int16_t ly = COMPASS_CY - static_cast<int16_t>(lroundf(c * rLbl));
            char dBuf[8];
            snprintf(dBuf, sizeof(dBuf), "%d", deg);
            canvas.setFont(&fonts::FreeSansBold9pt7b);
            canvas.setTextDatum(textdatum_t::middle_center);
            canvas.setTextColor(ThemeColors::TEXT_MUTED);
            canvas.drawString(dBuf, lx, ly);
        }
    }

    // Inner instrument hub ring
    canvas.fillSmoothCircle(COMPASS_CX, COMPASS_CY, 96, 0x070C12U);
    canvas.drawArc(COMPASS_CX, COMPASS_CY, 96, 94, 0.0f, 360.0f, ThemeColors::BORDER_SUBTLE);

    // Fixed top 12-o'clock Lubber Triangle (current vehicle heading line)
    canvas.fillTriangle(COMPASS_CX, COMPASS_CY - COMPASS_R + 16,
                        COMPASS_CX - 11, COMPASS_CY - COMPASS_R - 4,
                        COMPASS_CX + 11, COMPASS_CY - COMPASS_R - 4,
                        ThemeColors::RACING_YELLOW);

    // Center crisp vehicle / aviation heading chevron pointing UP + digital readout inside hub
    const int16_t arrowY = COMPASS_CY - 44;
    canvas.fillTriangle(COMPASS_CX, arrowY - 26,
                        COMPASS_CX - 18, arrowY + 12,
                        COMPASS_CX, arrowY + 3,
                        ThemeColors::NEON_GREEN);
    canvas.fillTriangle(COMPASS_CX, arrowY - 26,
                        COMPASS_CX + 18, arrowY + 12,
                        COMPASS_CX, arrowY + 3,
                        0x00C484U);

    // Digital degree inside center hub
    char degNum[16];
    snprintf(degNum, sizeof(degNum), "%03d", static_cast<int>(lroundf(heading)) % 360);
    canvas.setFont(&fonts::FreeSansBold24pt7b);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString(degNum, COMPASS_CX - 6, COMPASS_CY + 16);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.drawString("°", COMPASS_CX + 46, COMPASS_CY + 6);

    // Static hold / Gyro+GPS fusion tag inside hub bottom
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextColor(state.gnss.staticHoldLocked ? ThemeColors::ICE_CYAN : ThemeColors::NEON_GREEN);
    canvas.drawString(state.gnss.staticHoldLocked ? "静止航向锁定" : "陀螺+GPS融合",
                      COMPASS_CX, COMPASS_CY + 58);

    // Prominent 8-Direction Chinese + Degree Banner right below 360px dial (Y = 460..524)
    const int16_t hdgBarX = 96;
    const int16_t hdgBarY = 462;
    const int16_t hdgBarW = 368;
    const int16_t hdgBarH = 58;
    canvas.fillRoundRect(hdgBarX, hdgBarY, hdgBarW, hdgBarH, 12, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(hdgBarX, hdgBarY, hdgBarW, hdgBarH, 12, ThemeColors::BORDER_SUBTLE);

    char fullHdg[40];
    snprintf(fullHdg, sizeof(fullHdg), "%03d°  %s  %s",
             static_cast<int>(lroundf(heading)) % 360,
             headingToCn8(heading),
             headingToEn8(heading));
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextColor(ThemeColors::RACING_YELLOW);
    canvas.drawString(fullHdg, hdgBarX + hdgBarW / 2, hdgBarY + hdgBarH / 2);
}

void Page1Dashboard::drawMiniDynamicsCard(M5Canvas& canvas, const SystemStateSnapshot& state) {
    canvas.fillRoundRect(DYN_CARD_X, DYN_CARD_Y, DYN_CARD_W, DYN_CARD_H, 12, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(DYN_CARD_X, DYN_CARD_Y, DYN_CARD_W, DYN_CARD_H, 12, ThemeColors::BORDER_SUBTLE);

    // Mini G-Force bullseye on left
    const int16_t gcx = DYN_CARD_X + 74;
    const int16_t gcy = DYN_CARD_Y + DYN_CARD_H / 2;
    const int16_t gr  = 52;

    canvas.fillSmoothCircle(gcx, gcy, gr, 0x060B10U);
    canvas.drawCircle(gcx, gcy, gr, ThemeColors::BORDER_SUBTLE);
    canvas.drawCircle(gcx, gcy, gr / 2, 0x1A2634U);
    canvas.drawFastHLine(gcx - gr, gcy, gr * 2, 0x1A2634U);
    canvas.drawFastVLine(gcx, gcy - gr, gr * 2, 0x1A2634U);

    const float clampedLat = fminf(1.0f, fmaxf(-1.0f, state.imu.lateralG));
    const float clampedLon = fminf(1.0f, fmaxf(-1.0f, state.imu.longitudinalG));
    const int16_t gx = gcx + static_cast<int16_t>(lroundf(clampedLat * (gr - 8)));
    const int16_t gy = gcy - static_cast<int16_t>(lroundf(clampedLon * (gr - 8)));

    const uint32_t gCol = (state.imu.combinedG >= 0.45f)
                              ? ThemeColors::ALERT_RED
                              : ((state.imu.combinedG >= 0.25f) ? ThemeColors::RACING_YELLOW : ThemeColors::NEON_GREEN);
    canvas.fillSmoothCircle(gx, gy, 7, gCol);

    // Telemetry text on right of mini bullseye
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString("车身动态 G 值与姿态", DYN_CARD_X + 146, DYN_CARD_Y + 14);

    // Dual-IMU redundancy status pill
    const char* imuBadge = state.imu.failoverActive
                               ? "副IMU热备接管"
                               : (state.imu.isDualFused ? "双IMU冗余融合" : "单IMU运行");
    const uint32_t imuCol = state.imu.failoverActive ? ThemeColors::AMBER_WARN : ThemeColors::NEON_GREEN;
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextDatum(textdatum_t::top_right);
    canvas.setTextColor(imuCol);
    canvas.drawString(imuBadge, DYN_CARD_X + DYN_CARD_W - 16, DYN_CARD_Y + 16);

    char gBuf[32];
    snprintf(gBuf, sizeof(gBuf), "%.2f G", static_cast<double>(state.imu.combinedG));
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(gCol);
    canvas.drawString(gBuf, DYN_CARD_X + 146, DYN_CARD_Y + 48);

    char attBuf[64];
    snprintf(attBuf, sizeof(attBuf), "俯仰 %+.1f°   横滚 %+.1f°",
             static_cast<double>(state.imu.pitchDeg),
             static_cast<double>(state.imu.rollDeg));
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString(attBuf, DYN_CARD_X + 146, DYN_CARD_Y + 96);

    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextDatum(textdatum_t::bottom_right);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("点击详情 >", DYN_CARD_X + DYN_CARD_W - 14, DYN_CARD_Y + DYN_CARD_H - 12);
}

void Page1Dashboard::drawHeroSpeedometer(M5Canvas& canvas, const SystemStateSnapshot& state) {
    const float spd = fmaxf(0.0f, state.gnss.speedKmh);
    const int spdInt = static_cast<int>(lroundf(spd));

    uint32_t spdColor = ThemeColors::NEON_GREEN;
    if (spd >= 120.0f) {
        spdColor = ThemeColors::ALERT_RED;
    } else if (spd >= 95.0f) {
        spdColor = ThemeColors::RACING_YELLOW;
    }

    char spdStr[16];
    snprintf(spdStr, sizeof(spdStr), "%d", spdInt);

    // Render 120pt-class silky anti-aliased digits via PSRAM sprite + pushRotateZoomWithAA
    const float dstCenterX = 776.0f;
    const float dstCenterY = 152.0f;

    if (_speedSpriteReady) {
        _speedSprite.fillScreen(TFT_BLACK);
        _speedSprite.setFont(&fonts::Font8);
        _speedSprite.setTextDatum(textdatum_t::middle_right);
        _speedSprite.setTextColor(spdColor, TFT_BLACK);
        _speedSprite.drawString(spdStr, SPD_SPR_W - 6, SPD_SPR_H / 2 + 2);
        _speedSprite.pushRotateZoomWithAA(&canvas, dstCenterX, dstCenterY, 0.0f, SPD_ZOOM, SPD_ZOOM, TFT_BLACK);
    } else {
        canvas.setFont(&fonts::DejaVu72);
        canvas.setTextDatum(textdatum_t::middle_right);
        canvas.setTextColor(spdColor);
        canvas.drawString(spdStr, 960, 152);
    }

    // Crisp unit & live GPS status on the right of hero digits
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextDatum(textdatum_t::bottom_left);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString("km/h", 992, 206);

    canvas.setFont(&fonts::efontCN_14_b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(state.gnss.gnssFixOk ? ThemeColors::NEON_GREEN : ThemeColors::AMBER_WARN);
    canvas.drawString(state.gnss.gnssFixOk ? "GPS 实时" : "待定位", 994, 102);

    // Dynamic speed progress bar (0 .. 160 km/h)
    const int16_t barX = RIGHT_X;
    const int16_t barY = 248;
    const int16_t barW = RIGHT_W;
    const int16_t barH = 16;
    canvas.fillRoundRect(barX, barY, barW, barH, 6, ThemeColors::BG_CARD_DARK);
    const float ratio = fminf(1.0f, spd / 160.0f);
    const int16_t fillW = static_cast<int16_t>(lroundf(ratio * barW));
    if (fillW > 0) {
        canvas.fillRoundRect(barX, barY, fillW, barH, 6, spdColor);
    }
    canvas.drawRoundRect(barX, barY, barW, barH, 6, ThemeColors::BORDER_SUBTLE);
}

void Page1Dashboard::drawAltitudeAndSlopeCard(M5Canvas& canvas, const SystemStateSnapshot& state) {
    const int16_t halfW = (ALT_CARD_W - 12) / 2; // 244px each

    // Left Half: Fused Altitude (-> Page2Altitude)
    canvas.fillRoundRect(ALT_CARD_X, ALT_CARD_Y, halfW, ALT_CARD_H, 12, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(ALT_CARD_X, ALT_CARD_Y, halfW, ALT_CARD_H, 12, ThemeColors::BORDER_SUBTLE);

    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("融合海拔 ALT", ALT_CARD_X + 16, ALT_CARD_Y + 12);

    char altBuf[24];
    snprintf(altBuf, sizeof(altBuf), "%.1f", static_cast<double>(state.baro.fusedAltitudeM));
    canvas.setFont(&fonts::FreeSansBold24pt7b);
    canvas.setTextDatum(textdatum_t::bottom_left);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString(altBuf, ALT_CARD_X + 16, ALT_CARD_Y + ALT_CARD_H - 12);

    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextDatum(textdatum_t::bottom_right);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("m", ALT_CARD_X + halfW - 16, ALT_CARD_Y + ALT_CARD_H - 14);

    // Right Half: Real-time Road Slope % (-> Page5Dynamics)
    const int16_t slpX = ALT_CARD_X + halfW + 12;
    const bool steep = state.baro.steepSlopeWarning ||
                       (fabsf(state.baro.roadSlopePercent) >= BaroFusionConfig::STEEP_SLOPE_WARN_PERCENT);
    const uint32_t slpBorder = steep ? ThemeColors::ALERT_RED : ThemeColors::BORDER_SUBTLE;
    const uint32_t slpBg     = steep ? 0x2A0E0EU : ThemeColors::BG_CARD_DARK;
    const uint32_t slpCol    = steep ? ThemeColors::ALERT_RED : ThemeColors::RACING_YELLOW;

    canvas.fillRoundRect(slpX, ALT_CARD_Y, halfW, ALT_CARD_H, 12, slpBg);
    canvas.drawRoundRect(slpX, ALT_CARD_Y, halfW, ALT_CARD_H, 12, slpBorder);

    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(steep ? ThemeColors::ALERT_RED : ThemeColors::TEXT_MUTED);
    canvas.drawString(steep ? "道路坡度 (陡坡告警!)" : "实时坡度 SLOPE", slpX + 16, ALT_CARD_Y + 12);

    char slpBuf[24];
    snprintf(slpBuf, sizeof(slpBuf), "%+.1f%%", static_cast<double>(state.baro.roadSlopePercent));
    canvas.setFont(&fonts::FreeSansBold24pt7b);
    canvas.setTextDatum(textdatum_t::bottom_left);
    canvas.setTextColor(slpCol);
    canvas.drawString(slpBuf, slpX + 16, ALT_CARD_Y + ALT_CARD_H - 12);
}

void Page1Dashboard::drawTripDimensionBar(M5Canvas& canvas, const SystemStateSnapshot& state) {
    const TripCategory activeCat = effectiveCategory(state);
    const bool subActive         = state.tripControl.manualTripActive;

    // Layout:
    // If !subActive: [单次 TRIP] (166px) | [当日 DAY] (166px) | [+ 小记开始] (152px)
    // If  subActive: [单次 TRIP] (114px) | [当日 DAY] (114px) | [小记 SUB] (114px) | [■ 结束小记] (134px)
    if (!subActive) {
        const int16_t tabW = 164;
        const TripCategory cats[2] = { TRIP_SINGLE, TRIP_DAILY };
        const char* labels[2]      = { "单次 TRIP", "当日 DAY" };

        for (int i = 0; i < 2; ++i) {
            const int16_t tx = TAB_BAR_X + i * (tabW + 8);
            const bool sel   = (activeCat == cats[i]);
            canvas.fillRoundRect(tx, TAB_BAR_Y, tabW, TAB_BAR_H, 10,
                                 sel ? ThemeColors::NEON_GREEN : ThemeColors::BG_CARD_DARK);
            if (!sel) {
                canvas.drawRoundRect(tx, TAB_BAR_Y, tabW, TAB_BAR_H, 10, ThemeColors::BORDER_SUBTLE);
            }
            canvas.setFont(&fonts::efontCN_16_b);
            canvas.setTextDatum(textdatum_t::middle_center);
            canvas.setTextColor(sel ? TFT_BLACK : ThemeColors::TEXT_WHITE);
            canvas.drawString(labels[i], tx + tabW / 2, TAB_BAR_Y + TAB_BAR_H / 2);
        }

        // [+ 小记开始] button
        const int16_t btnX = TAB_BAR_X + 2 * (tabW + 8);
        const int16_t btnW = TAB_BAR_W - 2 * (tabW + 8);
        canvas.fillRoundRect(btnX, TAB_BAR_Y, btnW, TAB_BAR_H, 10, 0x102634U);
        canvas.drawRoundRect(btnX, TAB_BAR_Y, btnW, TAB_BAR_H, 10, ThemeColors::ICE_CYAN);
        canvas.setFont(&fonts::efontCN_16_b);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.setTextColor(ThemeColors::ICE_CYAN);
        canvas.drawString("+ 小记开始", btnX + btnW / 2, TAB_BAR_Y + TAB_BAR_H / 2);
    } else {
        const int16_t tabW = 114;
        const TripCategory cats[3] = { TRIP_SINGLE, TRIP_DAILY, TRIP_MANUAL };
        const char* labels[3]      = { "单次 TRIP", "当日 DAY", "小记 SUB" };

        for (int i = 0; i < 3; ++i) {
            const int16_t tx = TAB_BAR_X + i * (tabW + 6);
            const bool sel   = (activeCat == cats[i]);
            const uint32_t selBg = (cats[i] == TRIP_MANUAL) ? ThemeColors::RACING_YELLOW : ThemeColors::NEON_GREEN;
            canvas.fillRoundRect(tx, TAB_BAR_Y, tabW, TAB_BAR_H, 10,
                                 sel ? selBg : ThemeColors::BG_CARD_DARK);
            if (!sel) {
                canvas.drawRoundRect(tx, TAB_BAR_Y, tabW, TAB_BAR_H, 10, ThemeColors::BORDER_SUBTLE);
            }
            canvas.setFont(&fonts::efontCN_16_b);
            canvas.setTextDatum(textdatum_t::middle_center);
            canvas.setTextColor(sel ? TFT_BLACK : ThemeColors::TEXT_WHITE);
            canvas.drawString(labels[i], tx + tabW / 2, TAB_BAR_Y + TAB_BAR_H / 2);
        }

        // [■ 结束小记] button
        const int16_t btnX = TAB_BAR_X + 3 * (tabW + 6);
        const int16_t btnW = TAB_BAR_W - 3 * (tabW + 6);
        canvas.fillRoundRect(btnX, TAB_BAR_Y, btnW, TAB_BAR_H, 10, 0x331212U);
        canvas.drawRoundRect(btnX, TAB_BAR_Y, btnW, TAB_BAR_H, 10, ThemeColors::ALERT_RED);
        canvas.setFont(&fonts::efontCN_16_b);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.setTextColor(ThemeColors::ALERT_RED);
        canvas.drawString("■ 结束小记", btnX + btnW / 2, TAB_BAR_Y + TAB_BAR_H / 2);
    }
}

void Page1Dashboard::drawTripCardAndResetRing(M5Canvas& canvas, const SystemStateSnapshot& state, uint32_t nowMs) {
    (void)nowMs;
    const int16_t x = TripResetConfig::HITBOX_LOCAL_X; // 580
    const int16_t y = TripResetConfig::HITBOX_LOCAL_Y; // 460
    const int16_t w = TripResetConfig::HITBOX_W;       // 500
    const int16_t h = TripResetConfig::HITBOX_H;       // 220

    const TripCategory cat   = effectiveCategory(state);
    const TripStats&   stats = state.trips[cat];

    const bool showHoldOverlay = _tripHoldActive && (_tripHoldElapsedMs >= 140UL) && !_tripResetTriggered;
    const uint32_t borderCol   = showHoldOverlay
                                     ? ThemeColors::RACING_YELLOW
                                     : ((cat == TRIP_MANUAL) ? ThemeColors::RACING_YELLOW : ThemeColors::BORDER_SUBTLE);

    canvas.fillRoundRect(x, y, w, h, 14, _tripHoldActive ? ThemeColors::BG_CARD_HIGHLIGHT : ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(x, y, w, h, 14, borderCol);

    // Header row inside 500x220 TRIP card
    const char* catTitle = (cat == TRIP_SINGLE)
                               ? "单次行程里程 TRIP"
                               : ((cat == TRIP_DAILY) ? "当日累计里程 DAY" : "手动小记行程 SUB");
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor((cat == TRIP_MANUAL) ? ThemeColors::RACING_YELLOW : ThemeColors::NEON_GREEN);
    canvas.drawString(catTitle, x + 20, y + 14);

    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextDatum(textdatum_t::top_right);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("长按 2 秒清零单次行程  |  轻触看轨迹", x + w - 18, y + 17);

    // Large distance readout
    char distBuf[24];
    if (stats.tripDistanceKm < 1000.0f) {
        snprintf(distBuf, sizeof(distBuf), "%.1f", static_cast<double>(stats.tripDistanceKm));
    } else {
        snprintf(distBuf, sizeof(distBuf), "%.0f", static_cast<double>(stats.tripDistanceKm));
    }
    canvas.setFont(&fonts::DejaVu56);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString(distBuf, x + 20, y + 48);

    const int16_t distPx = static_cast<int16_t>(canvas.textWidth(distBuf));
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextDatum(textdatum_t::bottom_left);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.drawString("km", x + 28 + distPx, y + 102);

    // Driving duration on right of distance
    const uint32_t hrs  = stats.movingDurationSec / 3600UL;
    const uint32_t mins = (stats.movingDurationSec % 3600UL) / 60UL;
    const uint32_t secs = stats.movingDurationSec % 60UL;
    char timeBuf[24];
    snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu:%02lu",
             static_cast<unsigned long>(hrs),
             static_cast<unsigned long>(mins),
             static_cast<unsigned long>(secs));
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextDatum(textdatum_t::top_right);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("行驶用时", x + w - 20, y + 48);

    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString(timeBuf, x + w - 20, y + 70);

    canvas.drawFastHLine(x + 18, y + 124, w - 36, ThemeColors::BORDER_SUBTLE);

    // Bottom 3 secondary metrics: AVG speed | MAX speed | Cumulative Climb
    const int16_t colW = (w - 36) / 3;
    char avgBuf[24], maxBuf[24], clbBuf[24];
    snprintf(avgBuf, sizeof(avgBuf), "%.1f km/h", static_cast<double>(stats.avgSpeedKmh));
    snprintf(maxBuf, sizeof(maxBuf), "%.1f km/h", static_cast<double>(stats.maxSpeedKmh));
    snprintf(clbBuf, sizeof(clbBuf), "+%.0f m", static_cast<double>(stats.cumulativeClimbM));

    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("平均车速 AVG", x + 20, y + 136);
    canvas.drawString("最高车速 MAX", x + 20 + colW, y + 136);
    canvas.drawString("累计爬升 CLIMB", x + 20 + colW * 2, y + 136);

    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString(avgBuf, x + 20, y + 168);
    canvas.setTextColor(ThemeColors::AMBER_WARN);
    canvas.drawString(maxBuf, x + 20 + colW, y + 168);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.drawString(clbBuf, x + 20 + colW * 2, y + 168);

    // 2.0-Second Long-Press Circular Progress Ring Overlay
    if (showHoldOverlay) {
        canvas.fillRectAlpha(x + 2, y + 2, w - 4, h - 4, 205, TFT_BLACK);
        canvas.drawRoundRect(x, y, w, h, 14, ThemeColors::RACING_YELLOW);

        const float progress = fminf(1.0f, static_cast<float>(_tripHoldElapsedMs) /
                                               static_cast<float>(TripResetConfig::LONG_PRESS_MS));
        const int16_t rcx = x + 110;
        const int16_t rcy = y + h / 2;

        // Track ring + clockwise sweep from 12 o'clock (-90 deg)
        canvas.fillArc(rcx, rcy, 68, 52, 0.0f, 360.0f, 0x1E2A38U);
        canvas.fillArc(rcx, rcy, 68, 52, -90.0f, -90.0f + progress * 360.0f, ThemeColors::NEON_GREEN);

        char pctStr[16];
        snprintf(pctStr, sizeof(pctStr), "%d%%", static_cast<int>(lroundf(progress * 100.0f)));
        canvas.setFont(&fonts::FreeSansBold18pt7b);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.setTextColor(ThemeColors::TEXT_WHITE);
        canvas.drawString(pctStr, rcx, rcy);

        canvas.setFont(&fonts::efontCN_24_b);
        canvas.setTextDatum(textdatum_t::top_left);
        canvas.setTextColor(ThemeColors::RACING_YELLOW);
        canvas.drawString("继续按住清零单次行程...", x + 204, y + 66);

        const float remSec = fmaxf(0.0f, (static_cast<float>(TripResetConfig::LONG_PRESS_MS) -
                                          static_cast<float>(_tripHoldElapsedMs)) / 1000.0f);
        char remStr[64];
        snprintf(remStr, sizeof(remStr), "松手取消  |  还需 %.1f 秒同步重置全车图表",
                 static_cast<double>(remSec));
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextColor(ThemeColors::TEXT_WHITE);
        canvas.drawString(remStr, x + 204, y + 118);
    }
}

bool Page1Dashboard::onTouch(const TouchEvent& event, const SystemStateSnapshot& state) {
    // 1. Handle 500x220 Bottom-Right TRIP Hitbox (Screen 740..1240 x 460..680, Local 580..1080 x 460..680)
    const bool inTripHitbox = event.hitTestLocal(
        TripResetConfig::HITBOX_LOCAL_X,
        TripResetConfig::HITBOX_LOCAL_Y,
        TripResetConfig::HITBOX_W,
        TripResetConfig::HITBOX_H
    );

    const bool startedInTripHitbox =
        (event.startLocalX >= TripResetConfig::HITBOX_LOCAL_X) &&
        (event.startLocalX < (TripResetConfig::HITBOX_LOCAL_X + TripResetConfig::HITBOX_W)) &&
        (event.startLocalY >= TripResetConfig::HITBOX_LOCAL_Y) &&
        (event.startLocalY < (TripResetConfig::HITBOX_LOCAL_Y + TripResetConfig::HITBOX_H));

    if (event.type == TOUCH_EVT_DOWN && inTripHitbox) {
        _tripHoldActive     = true;
        _tripResetTriggered = false;
        _tripHoldElapsedMs  = 0;
        return true;
    }

    if (_tripHoldActive && (event.type == TOUCH_EVT_HOLD || event.type == TOUCH_EVT_MOVE)) {
        if (!inTripHitbox || !startedInTripHitbox) {
            _tripHoldActive    = false;
            _tripHoldElapsedMs = 0;
            return false;
        }
        _tripHoldElapsedMs = event.holdDurationMs;
        if (!_tripResetTriggered && _tripHoldElapsedMs >= TripResetConfig::LONG_PRESS_MS) {
            _tripResetTriggered = true;
            _tripHoldActive     = false;

            // Trigger full-link trip reset on SystemState
            SystemState::instance().requestFullTripReset();

            // Rising dual-tone tactile confirmation (non-blocking)
            playFeedbackTone(state, HapticConfig::TONE_CONFIRM_1_HZ, HapticConfig::TONE_CONFIRM_MS);
            _pendingSecondTone = true;
            _secondToneDueMs   = millis() + 65UL;
        }
        return true;
    }

    if (event.type == TOUCH_EVT_UP) {
        const bool wasHolding = _tripHoldActive || _tripResetTriggered;
        _tripHoldActive       = false;
        _tripHoldElapsedMs    = 0;
        if (wasHolding) {
            return true;
        }
    }

    // 2. Short taps on interactive cards & [TRIP / DAY / SUB] switcher
    if (event.type == TOUCH_EVT_TAP) {
        const bool wasReset = _tripResetTriggered;
        _tripHoldActive     = false;
        _tripResetTriggered = false;
        _tripHoldElapsedMs  = 0;
        if (wasReset) {
            return true;
        }

        // 2a. Adaptive [TRIP / DAY / (SUB)] dimension switcher + [+ 小记开始] / [■ 结束小记]
        if (event.hitTestLocal(TAB_BAR_X, TAB_BAR_Y, TAB_BAR_W, TAB_BAR_H)) {
            const bool subActive = state.tripControl.manualTripActive;
            if (!subActive) {
                const int16_t tabW = 164;
                if (event.hitTestLocal(TAB_BAR_X, TAB_BAR_Y, tabW, TAB_BAR_H)) {
                    playFeedbackTone(state);
                    StateGuard guard(SystemState::instance());
                    if (guard.locked()) {
                        SystemState::instance().data().tripControl.activeViewCategory = TRIP_SINGLE;
                    }
                    return true;
                }
                if (event.hitTestLocal(TAB_BAR_X + tabW + 8, TAB_BAR_Y, tabW, TAB_BAR_H)) {
                    playFeedbackTone(state);
                    StateGuard guard(SystemState::instance());
                    if (guard.locked()) {
                        SystemState::instance().data().tripControl.activeViewCategory = TRIP_DAILY;
                    }
                    return true;
                }
                // [+ 小记开始] (TrackLogger owns manualTripActive transition & reset)
                playFeedbackTone(state, HapticConfig::TONE_CONFIRM_1_HZ, HapticConfig::TONE_CONFIRM_MS);
                {
                    StateGuard guard(SystemState::instance());
                    if (guard.locked()) {
                        auto& tc              = SystemState::instance().data().tripControl;
                        tc.cmdStartManualTrip = true;
                        tc.activeViewCategory = TRIP_MANUAL;
                    }
                }
                return true;
            } else {
                const int16_t tabW = 114;
                const TripCategory cats[3] = { TRIP_SINGLE, TRIP_DAILY, TRIP_MANUAL };
                for (int i = 0; i < 3; ++i) {
                    if (event.hitTestLocal(TAB_BAR_X + i * (tabW + 6), TAB_BAR_Y, tabW, TAB_BAR_H)) {
                        playFeedbackTone(state);
                        StateGuard guard(SystemState::instance());
                        if (guard.locked()) {
                            SystemState::instance().data().tripControl.activeViewCategory = cats[i];
                        }
                        return true;
                    }
                }
                // [■ 结束小记] (TrackLogger settles CSV/GPX before clearing manualTripActive & zeroing)
                playFeedbackTone(state, HapticConfig::TONE_WARN_HZ, HapticConfig::TONE_CONFIRM_MS);
                {
                    StateGuard guard(SystemState::instance());
                    if (guard.locked()) {
                        auto& tc             = SystemState::instance().data().tripControl;
                        tc.cmdStopManualTrip = true;
                        if (tc.activeViewCategory == TRIP_MANUAL) {
                            tc.activeViewCategory = TRIP_SINGLE;
                        }
                    }
                }
                return true;
            }
        }

        // 2b. Quick tap on TRIP card -> open Page4Trail
        if (inTripHitbox) {
            playFeedbackTone(state);
            requestNavigate(PAGE_4_TRAIL);
            return true;
        }

        // 2c. Tap on Hero Speedometer -> open Page3Speed
        if (event.hitTestLocal(SPEED_BOX_X, SPEED_BOX_Y, SPEED_BOX_W, SPEED_BOX_H)) {
            playFeedbackTone(state);
            requestNavigate(PAGE_3_SPEED);
            return true;
        }

        // 2d. Tap on Altitude half -> Page2Altitude; Slope half -> Page5Dynamics
        const int16_t halfW = (ALT_CARD_W - 12) / 2;
        if (event.hitTestLocal(ALT_CARD_X, ALT_CARD_Y, halfW, ALT_CARD_H)) {
            playFeedbackTone(state);
            requestNavigate(PAGE_2_ALTITUDE);
            return true;
        }
        if (event.hitTestLocal(ALT_CARD_X + halfW + 12, ALT_CARD_Y, halfW, ALT_CARD_H)) {
            playFeedbackTone(state);
            requestNavigate(PAGE_5_DYNAMICS);
            return true;
        }

        // 2e. Tap on Mini G-Force / Attitude card -> Page5Dynamics
        if (event.hitTestLocal(DYN_CARD_X, DYN_CARD_Y, DYN_CARD_W, DYN_CARD_H)) {
            playFeedbackTone(state);
            requestNavigate(PAGE_5_DYNAMICS);
            return true;
        }
    }

    return false;
}
