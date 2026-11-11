#include "Page6Weather.h"
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <type_traits>

static_assert(!std::is_abstract<Page6Weather>::value && std::is_default_constructible<Page6Weather>::value,
              "Page6Weather must be concrete and default-constructible");

namespace {

constexpr float kDegToRad = 0.01745329251994329577f;

void formatUtcToLocalHm(uint32_t epochUtc, int16_t tzOffsetMin, char* outBuf, size_t outLen) {
    if (epochUtc == 0U) {
        snprintf(outBuf, outLen, "尚无历史刷新记录");
        return;
    }
    const time_t localSec = static_cast<time_t>(epochUtc) + static_cast<time_t>(tzOffsetMin) * 60;
    struct tm tmLocal {};
    gmtime_r(&localSec, &tmLocal);
    snprintf(outBuf, outLen, "%02d-%02d %02d:%02d",
             tmLocal.tm_mon + 1, tmLocal.tm_mday, tmLocal.tm_hour, tmLocal.tm_min);
}

void drawCloudShape(M5Canvas& canvas, int16_t cx, int16_t cy, int16_t r, uint32_t color) {
    const int16_t rMain = static_cast<int16_t>(r * 0.52f);
    const int16_t rLeft = static_cast<int16_t>(r * 0.40f);
    const int16_t rRgt  = static_cast<int16_t>(r * 0.36f);
    canvas.fillSmoothCircle(cx - r * 0.30f, cy + r * 0.12f, rLeft, color);
    canvas.fillSmoothCircle(cx + r * 0.05f, cy - r * 0.08f, rMain, color);
    canvas.fillSmoothCircle(cx + r * 0.36f, cy + r * 0.15f, rRgt, color);
    canvas.fillRoundRect(cx - r * 0.62f, cy + r * 0.08f, static_cast<int32_t>(r * 1.28f),
                         static_cast<int32_t>(r * 0.44f), 5, color);
}

} // namespace

void Page6Weather::onEnter(const SystemStateSnapshot& state) {
    (void)state;
}

bool Page6Weather::isHighRiskDrivingWmo(uint8_t wmoCode) {
    // Rainstorm(65,82), Blizzard(75,86), Freezing Drizzle/Rain(56,57,66,67), Thunderstorm/Hail(95,96,99)
    switch (wmoCode) {
        case 56:
        case 57:
        case 65:
        case 66:
        case 67:
        case 75:
        case 82:
        case 86:
        case 95:
        case 96:
        case 99:
            return true;
        default:
            return false;
    }
}

const char* Page6Weather::wmoToZhDesc(uint8_t wmoCode) {
    switch (wmoCode) {
        case 0:  return "晴朗";
        case 1:  return "大部晴朗";
        case 2:  return "晴间多云";
        case 3:  return "阴天";
        case 45: return "浓雾";
        case 48: return "雾凇凝霜";
        case 51: return "小毛毛雨";
        case 53: return "毛毛雨";
        case 55: return "浓毛毛雨";
        case 56: return "冻毛毛雨";
        case 57: return "强冻毛毛雨";
        case 61: return "小雨";
        case 63: return "中雨";
        case 65: return "暴雨";
        case 66: return "冻雨";
        case 67: return "强冻雨";
        case 71: return "小雪";
        case 73: return "中雪";
        case 75: return "暴雪";
        case 77: return "雪粒";
        case 80: return "小阵雨";
        case 81: return "中阵雨";
        case 82: return "强阵雨/暴雨";
        case 85: return "小阵雪";
        case 86: return "强阵雪/暴雪";
        case 95: return "雷阵雨";
        case 96: return "雷暴伴冰雹";
        case 99: return "强雷暴冰雹";
        default: return "多云";
    }
}

const char* Page6Weather::windDegToZhDir(uint16_t deg) {
    const uint16_t norm = static_cast<uint16_t>((deg + 22U) % 360U) / 45U;
    static constexpr const char* kDirs[8] = {
        "北风", "东北风", "东风", "东南风", "南风", "西南风", "西风", "西北风"
    };
    return kDirs[norm & 0x07U];
}

void Page6Weather::drawWmoVectorIcon(M5Canvas& canvas, int16_t cx, int16_t cy, int16_t radius,
                                     uint8_t wmoCode, bool isDay) {
    if (wmoCode == 0 || wmoCode == 1) {
        if (isDay) {
            const int16_t sunR = static_cast<int16_t>(radius * 0.48f);
            for (int deg = 0; deg < 360; deg += 45) {
                const float rad = static_cast<float>(deg) * kDegToRad;
                const int16_t x0 = static_cast<int16_t>(lroundf(cx + cosf(rad) * (sunR + 3)));
                const int16_t y0 = static_cast<int16_t>(lroundf(cy + sinf(rad) * (sunR + 3)));
                const int16_t x1 = static_cast<int16_t>(lroundf(cx + cosf(rad) * (radius * 0.88f)));
                const int16_t y1 = static_cast<int16_t>(lroundf(cy + sinf(rad) * (radius * 0.88f)));
                canvas.drawWideLine(x0, y0, x1, y1, 1.8f, ThemeColors::RACING_YELLOW);
            }
            canvas.fillSmoothCircle(cx, cy, sunR, ThemeColors::RACING_YELLOW);
        } else {
            const int16_t moonR = static_cast<int16_t>(radius * 0.58f);
            canvas.fillSmoothCircle(cx, cy, moonR, ThemeColors::RACING_YELLOW);
            canvas.fillSmoothCircle(cx + static_cast<int16_t>(moonR * 0.45f),
                                    cy - static_cast<int16_t>(moonR * 0.35f),
                                    static_cast<int16_t>(moonR * 0.82f), ThemeColors::BG_CARD_DARK);
        }
        if (wmoCode == 1) {
            drawCloudShape(canvas, cx + radius * 0.20f, cy + radius * 0.25f, static_cast<int16_t>(radius * 0.55f), 0xCFD8DCU);
        }
        return;
    }

    if (wmoCode == 2) {
        if (isDay) {
            canvas.fillSmoothCircle(cx - radius * 0.28f, cy - radius * 0.25f,
                                    static_cast<int16_t>(radius * 0.40f), ThemeColors::RACING_YELLOW);
        } else {
            canvas.fillSmoothCircle(cx - radius * 0.28f, cy - radius * 0.25f,
                                    static_cast<int16_t>(radius * 0.34f), ThemeColors::ICE_CYAN);
        }
        drawCloudShape(canvas, cx + radius * 0.08f, cy + radius * 0.08f, static_cast<int16_t>(radius * 0.82f), 0xECEFF1U);
        return;
    }

    if (wmoCode == 3) {
        drawCloudShape(canvas, cx - radius * 0.12f, cy - radius * 0.14f, static_cast<int16_t>(radius * 0.72f), 0x78909CU);
        drawCloudShape(canvas, cx + radius * 0.08f, cy + radius * 0.08f, static_cast<int16_t>(radius * 0.85f), 0xCFD8DCU);
        return;
    }

    if (wmoCode == 45 || wmoCode == 48) {
        drawCloudShape(canvas, cx, cy - radius * 0.18f, static_cast<int16_t>(radius * 0.75f), 0xB0BEC5U);
        for (int i = 0; i < 3; ++i) {
            const int16_t fy = static_cast<int16_t>(cy + radius * 0.28f + i * (radius * 0.22f));
            canvas.drawWideLine(cx - radius * 0.65f, fy, cx + radius * 0.65f, fy, 1.8f, ThemeColors::AMBER_WARN);
        }
        return;
    }

    // Thunderstorms & Hail (95, 96, 99)
    if (wmoCode >= 95) {
        drawCloudShape(canvas, cx, cy - radius * 0.22f, static_cast<int16_t>(radius * 0.82f), 0x78909CU);
        // Lightning polygon
        const int16_t lx = cx - 2;
        const int16_t ly = static_cast<int16_t>(cy + radius * 0.05f);
        canvas.fillTriangle(lx + 4, ly, lx - 7, ly + static_cast<int16_t>(radius * 0.42f), lx + 2, ly + static_cast<int16_t>(radius * 0.42f), ThemeColors::RACING_YELLOW);
        canvas.fillTriangle(lx - 1, ly + static_cast<int16_t>(radius * 0.34f), lx + 8, ly + static_cast<int16_t>(radius * 0.34f), lx - 4, ly + static_cast<int16_t>(radius * 0.82f), ThemeColors::RACING_YELLOW);
        if (wmoCode >= 96) {
            // Hailstones
            canvas.fillCircle(cx - static_cast<int16_t>(radius * 0.42f), cy + static_cast<int16_t>(radius * 0.58f), 3, ThemeColors::ALERT_RED);
            canvas.fillCircle(cx + static_cast<int16_t>(radius * 0.42f), cy + static_cast<int16_t>(radius * 0.58f), 3, ThemeColors::ALERT_RED);
        }
        return;
    }

    // Snow (71, 73, 75, 77, 85, 86)
    if ((wmoCode >= 71 && wmoCode <= 77) || wmoCode == 85 || wmoCode == 86) {
        drawCloudShape(canvas, cx, cy - radius * 0.22f, static_cast<int16_t>(radius * 0.80f), 0xCFD8DCU);
        const int count = (wmoCode == 75 || wmoCode == 86) ? 3 : 2;
        for (int i = 0; i < count; ++i) {
            const int16_t sx = static_cast<int16_t>(cx + (i - (count - 1) * 0.5f) * radius * 0.48f);
            const int16_t sy = static_cast<int16_t>(cy + radius * 0.52f);
            const int16_t sr = static_cast<int16_t>(fmaxf(3.0f, radius * 0.18f));
            canvas.drawLine(sx - sr, sy, sx + sr, sy, ThemeColors::TEXT_WHITE);
            canvas.drawLine(sx, sy - sr, sx, sy + sr, ThemeColors::TEXT_WHITE);
            canvas.drawLine(sx - sr + 1, sy - sr + 1, sx + sr - 1, sy + sr - 1, ThemeColors::ICE_CYAN);
            canvas.drawLine(sx - sr + 1, sy + sr - 1, sx + sr - 1, sy - sr + 1, ThemeColors::ICE_CYAN);
        }
        return;
    }

    // Rain / Drizzle / Freezing Rain (51..67, 80..82)
    const bool freezing = (wmoCode == 56 || wmoCode == 57 || wmoCode == 66 || wmoCode == 67);
    const bool heavy    = (wmoCode == 55 || wmoCode == 65 || wmoCode == 67 || wmoCode == 82);
    drawCloudShape(canvas, cx, cy - radius * 0.22f, static_cast<int16_t>(radius * 0.82f),
                   heavy ? 0x90A4AEU : 0xCFD8DCU);

    const int drops = heavy ? 3 : 2;
    const uint32_t dropCol = freezing ? ThemeColors::ALERT_RED : ThemeColors::ICE_CYAN;
    for (int i = 0; i < drops; ++i) {
        const int16_t rx0 = static_cast<int16_t>(cx + (i - (drops - 1) * 0.5f) * radius * 0.42f);
        const int16_t ry0 = static_cast<int16_t>(cy + radius * 0.28f);
        const int16_t rx1 = static_cast<int16_t>(rx0 - radius * 0.18f);
        const int16_t ry1 = static_cast<int16_t>(cy + radius * 0.72f);
        canvas.drawWideLine(rx0, ry0, rx1, ry1, 1.8f, dropCol);
    }
}

int16_t Page6Weather::currentToggleY(const SystemStateSnapshot& state) const {
    const bool severeTop = state.weather.hasValidData &&
                           (isHighRiskDrivingWmo(state.weather.currentWmoCode) || state.weather.hasHazardWarning);
    return severeTop ? 150 : 126;
}

void Page6Weather::render(M5Canvas& canvas, const SystemStateSnapshot& state) {
    clearViewportCanvas(canvas);
    canvas.setTextSize(1.0f);
    canvas.setTextWrap(false, false);

    const bool severeTop = state.weather.hasValidData &&
                           (isHighRiskDrivingWmo(state.weather.currentWmoCode) || state.weather.hasHazardWarning);

    if (severeTop) {
        drawSevereTopAlertBar(canvas, state, 8, 48);
        drawCurrentWeatherCard(canvas, state, 62, 162);
        if (state.weather.hasValidData) {
            drawHourlyMatrix(canvas, state, 232, 396);
        } else {
            drawOfflinePlaceholder(canvas, state, 232, 396);
        }
        drawBottomTrendFooter(canvas, state, 636, 46);
    } else {
        drawCurrentWeatherCard(canvas, state, 12, 190);
        if (state.weather.hasValidData) {
            drawHourlyMatrix(canvas, state, 212, 412);
        } else {
            drawOfflinePlaceholder(canvas, state, 212, 412);
        }
        drawBottomTrendFooter(canvas, state, 632, 50);
    }
}

void Page6Weather::drawSevereTopAlertBar(M5Canvas& canvas, const SystemStateSnapshot& state,
                                         int16_t y, int16_t h) const {
    constexpr int16_t x = 14;
    constexpr int16_t w = 1092;
    const bool flash    = ((millis() / 400U) % 2U) == 0U;

    canvas.fillRoundRect(x, y, w, h, 10, flash ? ThemeColors::ALERT_RED : 0x8C1812U);
    canvas.drawRoundRect(x, y, w, h, 10, ThemeColors::TEXT_WHITE);

    // Warning triangle icon
    canvas.fillTriangle(x + 28, y + 10, x + 14, y + h - 10, x + 42, y + h - 10, ThemeColors::RACING_YELLOW);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(TFT_BLACK);
    canvas.drawString("!", x + 28, y + h / 2 + 3, &fonts::efontCN_16_b);

    char alertBuf[160];
    if (isHighRiskDrivingWmo(state.weather.currentWmoCode)) {
        snprintf(alertBuf, sizeof(alertBuf),
                 "行车高危气象预警：当前【%s】恶劣天气！请控速加大跟车距离并开启雾灯",
                 wmoToZhDesc(state.weather.currentWmoCode));
    } else if (state.weather.warningMessage[0] != '\0') {
        snprintf(alertBuf, sizeof(alertBuf), "行车高危气象预警：%s", state.weather.warningMessage);
    } else {
        snprintf(alertBuf, sizeof(alertBuf), "行车高危气象预警：前方暴雨/暴雪/冻雨/冰雹风险，请谨慎驾驶！");
    }

    canvas.setTextDatum(middle_left);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString(alertBuf, x + 54, y + h / 2, &fonts::efontCN_24_b);
}

void Page6Weather::drawCurrentWeatherCard(M5Canvas& canvas, const SystemStateSnapshot& state,
                                          int16_t y, int16_t h) const {
    constexpr int16_t x = 14;
    constexpr int16_t w = 1092;

    canvas.fillRoundRect(x, y, w, h, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(x, y, w, h, 14, ThemeColors::BORDER_SUBTLE);

    // Left: Vector Weather Icon + Chinese WMO Description + Current Temp
    const uint8_t wmo   = state.weather.hasValidData ? state.weather.currentWmoCode : 2U;
    const bool    isDay = state.weather.hasValidData ? state.weather.isDay : true;
    drawWmoVectorIcon(canvas, x + 64, y + h / 2 - 6, 44, wmo, isDay);

    if (state.weather.hasValidData) {
        char tempStr[24];
        snprintf(tempStr, sizeof(tempStr), "%.1f", state.weather.currentTempC);
        canvas.setTextDatum(top_left);
        canvas.setTextColor(ThemeColors::NEON_GREEN);
        canvas.drawString(tempStr, x + 128, y + 18, &fonts::DejaVu56);
        const int32_t tw = canvas.textWidth(tempStr, &fonts::DejaVu56);
        canvas.drawString("°C", x + 134 + tw, y + 26, &fonts::efontCN_24_b);

        canvas.setTextColor(ThemeColors::TEXT_WHITE);
        canvas.drawString(wmoToZhDesc(wmo), x + 130, y + 86, &fonts::efontCN_24_b);

        // Apparent Temp / Humidity / Wind
        char detailStr[96];
        snprintf(detailStr, sizeof(detailStr), "体感 %.1f°C   |   湿度 %u%%   |   %s %.1f km/h",
                 state.weather.apparentTempC,
                 static_cast<unsigned>(state.weather.humidityPct),
                 windDegToZhDir(state.weather.windDirectionDeg),
                 state.weather.windSpeedKmh);
        canvas.setTextColor(ThemeColors::ICE_CYAN);
        canvas.drawString(detailStr, x + 130, y + h - 34, &fonts::efontCN_16_b);
    } else {
        canvas.setTextDatum(top_left);
        canvas.setTextColor(ThemeColors::TEXT_MUTED);
        canvas.drawString("--.- °C", x + 128, y + 24, &fonts::DejaVu40);
        canvas.setTextColor(ThemeColors::AMBER_WARN);
        canvas.drawString(state.weather.isFetching ? "正在联网拉取实时气象..." : "等待网络与 GPS 坐标获取气象",
                          x + 128, y + 86, &fonts::efontCN_24_b);
    }

    // Top-right metadata: Coordinates + Online/Offline Cache Status + Last Refresh Time
    char locStr[96];
    const bool hasCoord = (fabs(state.weather.fetchedLatitude) > 1e-5 || fabs(state.weather.fetchedLongitude) > 1e-5);
    if (hasCoord) {
        snprintf(locStr, sizeof(locStr), "GPS 反查坐标: %.3f°, %.3f°",
                 state.weather.fetchedLatitude, state.weather.fetchedLongitude);
    } else if (state.gnss.isValid) {
        snprintf(locStr, sizeof(locStr), "当前车辆坐标: %.3f°, %.3f°",
                 state.gnss.latitude, state.gnss.longitude);
    } else {
        snprintf(locStr, sizeof(locStr), "定位状态: 等待有效 GPS 坐标");
    }
    canvas.setTextDatum(top_right);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString(locStr, x + w - 20, y + 14, &fonts::efontCN_16);

    char syncBuf[64];
    formatUtcToLocalHm(state.weather.lastFetchUtc, state.time.localTzOffsetMin, syncBuf, sizeof(syncBuf));
    char statusLine[96];
    const bool offline = !state.network.staConnected || state.weather.isFromOfflineCache;
    snprintf(statusLine, sizeof(statusLine), "%s | 上次刷新: %s",
             state.weather.isFetching ? "刷新中..." : (offline ? "离线缓存" : "在线同步"),
             syncBuf);
    canvas.setTextColor(offline ? ThemeColors::AMBER_WARN : ThemeColors::NEON_GREEN);
    canvas.drawString(statusLine, x + w - 20, y + 40, &fonts::efontCN_14);

    // Refresh Button + Prominent [前 12 小时 (0~11h) / 后 12 小时 (12~23h)] Segmented Toggle Button
    const int16_t btnY = currentToggleY(state);

    // Manual Refresh Button
    const bool refRecent = (_lastRefreshMs != 0U && (millis() - _lastRefreshMs) < 1200U) || state.weather.isFetching;
    canvas.fillRoundRect(BTN_REFRESH_X, btnY, BTN_REFRESH_W, BTN_REFRESH_H, 12, refRecent ? 0x0F3328U : 0x13202EU);
    canvas.drawRoundRect(BTN_REFRESH_X, btnY, BTN_REFRESH_W, BTN_REFRESH_H, 12,
                         refRecent ? ThemeColors::NEON_GREEN : ThemeColors::BORDER_SUBTLE);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(refRecent ? ThemeColors::NEON_GREEN : ThemeColors::TEXT_WHITE);
    canvas.drawString(refRecent ? "刷新中..." : "刷新气象",
                      BTN_REFRESH_X + BTN_REFRESH_W / 2, btnY + BTN_REFRESH_H / 2, &fonts::efontCN_16_b);

    // Dual-Segment 12h Toggle Button (BTN_TOGGLE_X..+380)
    canvas.fillRoundRect(BTN_TOGGLE_X, btnY, BTN_TOGGLE_W, BTN_TOGGLE_H, 12, 0x091018U);
    canvas.drawRoundRect(BTN_TOGGLE_X, btnY, BTN_TOGGLE_W, BTN_TOGGLE_H, 12, ThemeColors::ICE_CYAN);

    const int16_t halfW = (BTN_TOGGLE_W - 8) / 2;
    const int16_t seg0X = BTN_TOGGLE_X + 4;
    const int16_t seg1X = BTN_TOGGLE_X + 4 + halfW;

    if (!_showBack12h) {
        canvas.fillRoundRect(seg0X, btnY + 4, halfW, BTN_TOGGLE_H - 8, 9, ThemeColors::NEON_GREEN);
    } else {
        canvas.fillRoundRect(seg1X, btnY + 4, halfW, BTN_TOGGLE_H - 8, 9, ThemeColors::RACING_YELLOW);
    }

    canvas.setTextDatum(middle_center);
    canvas.setTextColor(!_showBack12h ? TFT_BLACK : ThemeColors::TEXT_MUTED);
    canvas.drawString("前12小时 (0~11h)", seg0X + halfW / 2, btnY + BTN_TOGGLE_H / 2, &fonts::efontCN_16_b);

    canvas.setTextColor(_showBack12h ? TFT_BLACK : ThemeColors::TEXT_MUTED);
    canvas.drawString("后12小时 (12~23h)", seg1X + halfW / 2, btnY + BTN_TOGGLE_H / 2, &fonts::efontCN_16_b);
}

void Page6Weather::drawHourlyMatrix(M5Canvas& canvas, const SystemStateSnapshot& state,
                                    int16_t y, int16_t h) const {
    // 12 columns * 93px = 1116px across 1120px canvas (X: 2..1118)
    constexpr int16_t startX = 2;
    constexpr int16_t colW   = 93;
    constexpr int     kCols  = 12;

    const size_t baseSlot = _showBack12h ? 12U : 0U;

    // Find min/max temperature across these 12 hours to scale trend polyline smoothly
    float minT =  100.0f;
    float maxT = -100.0f;
    for (int i = 0; i < kCols; ++i) {
        const float t = state.weather.hourly[baseSlot + static_cast<size_t>(i)].tempC;
        if (t < minT) minT = t;
        if (t > maxT) maxT = t;
    }
    if (maxT - minT < 3.0f) {
        const float mid = 0.5f * (minT + maxT);
        minT = mid - 1.5f;
        maxT = mid + 1.5f;
    }

    const int16_t curveTopY = y + static_cast<int16_t>(h * 0.53f);
    const int16_t curveBotY = y + static_cast<int16_t>(h * 0.69f);
    int16_t ptX[kCols]      = {};
    int16_t ptY[kCols]      = {};

    // Draw 12 columns (each 93px wide)
    for (int col = 0; col < kCols; ++col) {
        const HourlyWeatherItem& item = state.weather.hourly[baseSlot + static_cast<size_t>(col)];
        const int16_t colX  = startX + col * colW;
        const int16_t colCx = colX + colW / 2;

        const bool colHazard = isHighRiskDrivingWmo(item.wmoCode) || (item.precipProbPct >= 75U);
        const uint32_t cellBg = colHazard ? 0x1D1014U : ((col % 2 == 0) ? 0x0A1017U : 0x0E151EU);
        canvas.fillRoundRect(colX + 1, y, colW - 2, h, 8, cellBg);
        canvas.drawRoundRect(colX + 1, y, colW - 2, h, 8, colHazard ? 0x5C2222U : 0x1A2634U);

        // 1) Hour label (HH:00)
        char hrStr[12];
        snprintf(hrStr, sizeof(hrStr), "%02u:00", static_cast<unsigned>(item.hour24 % 24U));
        canvas.setTextDatum(top_center);
        canvas.setTextColor((col == 0 && !_showBack12h) ? ThemeColors::NEON_GREEN : ThemeColors::TEXT_WHITE);
        canvas.drawString(hrStr, colCx, y + 10, &fonts::efontCN_16_b);

        // 2) Vector WMO Icon
        drawWmoVectorIcon(canvas, colCx, y + 66, 22, item.wmoCode, item.isDay);

        // Short WMO label
        canvas.setTextDatum(top_center);
        canvas.setTextColor(colHazard ? ThemeColors::ALERT_RED : ThemeColors::TEXT_MUTED);
        canvas.drawString(wmoToZhDesc(item.wmoCode), colCx, y + 98, &fonts::efontCN_14);

        // 3) Large Temperature (e.g. 25°)
        char tStr[16];
        snprintf(tStr, sizeof(tStr), "%.0f°", item.tempC);
        canvas.setTextColor(item.tempC <= 0.0f ? ThemeColors::ICE_CYAN : ThemeColors::RACING_YELLOW);
        canvas.drawString(tStr, colCx, y + 126, &fonts::DejaVu24);

        const float normT = (item.tempC - minT) / (maxT - minT);
        ptX[col] = colCx;
        ptY[col] = static_cast<int16_t>(lroundf(curveBotY - normT * (curveBotY - curveTopY)));

        // 4) Precipitation Probability % + Bar at bottom of column
        const uint8_t pop = (item.precipProbPct > 100U) ? 100U : item.precipProbPct;
        const int16_t barMaxH = 54;
        const int16_t barH    = static_cast<int16_t>((pop * barMaxH) / 100U);
        const int16_t barBase = y + h - 12;
        const int16_t barW    = colW - 26;

        canvas.fillRoundRect(colX + 13, barBase - barMaxH, barW, barMaxH, 4, 0x070B10U);
        if (barH > 0) {
            const uint32_t popCol = (pop >= 70U) ? ThemeColors::ALERT_RED
                                                 : ((pop >= 40U) ? ThemeColors::ICE_CYAN : 0x1A6985U);
            canvas.fillRoundRect(colX + 13, barBase - barH, barW, barH, 4, popCol);
        }

        char popStr[12];
        snprintf(popStr, sizeof(popStr), "%u%%", static_cast<unsigned>(pop));
        canvas.setTextDatum(bottom_center);
        canvas.setTextColor((pop >= 50U) ? ThemeColors::ICE_CYAN : ThemeColors::TEXT_MUTED);
        canvas.drawString(popStr, colCx, barBase - barMaxH - 6, &fonts::efontCN_16_b);
    }

    // Draw continuous smooth temperature polyline across all 12 columns
    for (int col = 0; col < kCols - 1; ++col) {
        canvas.drawWideLine(ptX[col], ptY[col], ptX[col + 1], ptY[col + 1], 2.2f, ThemeColors::NEON_GREEN);
    }
    for (int col = 0; col < kCols; ++col) {
        canvas.fillSmoothCircle(ptX[col], ptY[col], 4, ThemeColors::TEXT_WHITE);
        canvas.fillSmoothCircle(ptX[col], ptY[col], 2, ThemeColors::NEON_GREEN);
    }
}

void Page6Weather::drawOfflinePlaceholder(M5Canvas& canvas, const SystemStateSnapshot& state,
                                          int16_t y, int16_t h) const {
    constexpr int16_t x = 14;
    constexpr int16_t w = 1092;

    canvas.fillRoundRect(x, y, w, h, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(x, y, w, h, 14, ThemeColors::BORDER_SUBTLE);

    const int16_t cx = x + w / 2;
    const int16_t cy = y + h / 2 - 20;

    drawWmoVectorIcon(canvas, cx, cy - 58, 42, 2, true);

    canvas.setTextDatum(middle_center);
    canvas.setTextColor(ThemeColors::AMBER_WARN);
    if (!state.network.staConnected && !state.gnss.isValid) {
        canvas.drawString("当前未连接 Wi-Fi 且尚未锁定 GPS 坐标", cx, cy + 16, &fonts::efontCN_24_b);
    } else if (!state.network.staConnected) {
        canvas.drawString("当前处于离线状态 (Wi-Fi 未连接)，暂无本地气象缓存", cx, cy + 16, &fonts::efontCN_24_b);
    } else if (!state.gnss.isValid) {
        canvas.drawString("Wi-Fi 已就绪，正在等待有效 GPS/NVS 经纬度反查天气...", cx, cy + 16, &fonts::efontCN_24_b);
    } else {
        canvas.drawString("正在向 Open-Meteo 请求未来 24 小时逐时气象预报...", cx, cy + 16, &fonts::efontCN_24_b);
    }

    char syncBuf[64];
    formatUtcToLocalHm(state.weather.lastFetchUtc, state.time.localTzOffsetMin, syncBuf, sizeof(syncBuf));
    char subText[128];
    snprintf(subText, sizeof(subText), "上次成功刷新时间：%s  |  连网并获取定位后将每 15 分钟自动更新并缓存", syncBuf);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString(subText, cx, cy + 60, &fonts::efontCN_16);

    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString("可点击左侧边栏 [Wi-Fi] 连接车载热点，或点击右上角 [刷新气象] 立即重试", cx, cy + 96, &fonts::efontCN_16);
}

void Page6Weather::drawBottomTrendFooter(M5Canvas& canvas, const SystemStateSnapshot& state,
                                         int16_t y, int16_t h) const {
    constexpr int16_t x = 14;
    constexpr int16_t w = 1092;

    char footerMsg[160];
    uint32_t accentCol = ThemeColors::NEON_GREEN;

    if (state.weather.hasValidData) {
        int      rainIdx = -1;
        uint8_t  maxPop  = 0;
        uint8_t  rainWmo = 0;
        for (size_t i = 0; i < WeatherData::HOURLY_SLOTS; ++i) {
            const HourlyWeatherItem& it = state.weather.hourly[i];
            if (it.precipProbPct >= 50U || isHighRiskDrivingWmo(it.wmoCode)) {
                rainIdx = static_cast<int>(i);
                maxPop  = it.precipProbPct;
                rainWmo = it.wmoCode;
                break;
            }
        }

        if (rainIdx >= 0) {
            const uint8_t hr = state.weather.hourly[rainIdx].hour24;
            accentCol        = isHighRiskDrivingWmo(rainWmo) ? ThemeColors::ALERT_RED : ThemeColors::AMBER_WARN;
            snprintf(footerMsg, sizeof(footerMsg),
                     "前瞻行车提示：预计约 %d 小时后 (%02u:00) 出现【%s】(降水概率 %u%%)，请注意路面湿滑减速慢行",
                     rainIdx, static_cast<unsigned>(hr), wmoToZhDesc(rainWmo), static_cast<unsigned>(maxPop));
        } else if (state.weather.warningMessage[0] != '\0' && state.weather.hasHazardWarning) {
            accentCol = ThemeColors::AMBER_WARN;
            snprintf(footerMsg, sizeof(footerMsg), "前瞻行车提示：%s", state.weather.warningMessage);
        } else {
            snprintf(footerMsg, sizeof(footerMsg),
                     "前瞻行车提示：未来 24 小时沿途无明显强降水或结冰预警，当前显示【%s】段逐时预报",
                     _showBack12h ? "后 12 小时 (12~23h)" : "前 12 小时 (0~11h)");
        }
    } else {
        accentCol = ThemeColors::TEXT_MUTED;
        snprintf(footerMsg, sizeof(footerMsg),
                 "Open-Meteo 车载气象引擎：支持前后 12 小时 (93px/列) 大字秒切与暴雨/暴雪/冻雨/冰雹红底预警");
    }

    canvas.fillRoundRect(x, y, w, h, 10, 0x0B1219U);
    canvas.drawRoundRect(x, y, w, h, 10, accentCol);
    canvas.fillCircle(x + 20, y + h / 2, 5, accentCol);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(accentCol == ThemeColors::TEXT_MUTED ? ThemeColors::TEXT_MUTED : ThemeColors::TEXT_WHITE);
    canvas.drawString(footerMsg, x + 34, y + h / 2, &fonts::efontCN_16_b);
}

bool Page6Weather::onTouch(const TouchEvent& event, const SystemStateSnapshot& state) {
    if (event.type != TOUCH_EVT_TAP) {
        return false;
    }

    const int16_t btnY = currentToggleY(state);

    // 1. Toggle Front 12h (0..11) <-> Back 12h (12..23)
    if (event.hitTestLocal(BTN_TOGGLE_X, btnY - 6, BTN_TOGGLE_W, BTN_TOGGLE_H + 12)) {
        const uint32_t nowMs = millis();
        if (_lastToggleTapMs == 0U || (nowMs - _lastToggleTapMs) >= 120U) {
            const int16_t midX = BTN_TOGGLE_X + BTN_TOGGLE_W / 2;
            // If user taps inactive half, select that half; if user taps active half, toggle!
            if (!_showBack12h && event.localX >= midX) {
                _showBack12h = true;
            } else if (_showBack12h && event.localX < midX) {
                _showBack12h = false;
            } else {
                _showBack12h = !_showBack12h;
            }
            _lastToggleTapMs = nowMs;
            playFeedbackTone(state, HapticConfig::TONE_TAP_HZ, HapticConfig::TONE_TAP_MS);
        }
        return true;
    }

    // 2. Manual Weather Refresh Button
    if (event.hitTestLocal(BTN_REFRESH_X, btnY - 6, BTN_REFRESH_W, BTN_REFRESH_H + 12)) {
        const uint32_t nowMs = millis();
        if (_lastRefreshMs == 0U || (nowMs - _lastRefreshMs) >= 300U) {
            {
                StateGuard lock(SystemState::instance());
                SystemState::instance().data().weather.cmdRefreshWeather = true;
            }
            _lastRefreshMs = nowMs;
            playFeedbackTone(state, HapticConfig::TONE_CONFIRM_1_HZ, HapticConfig::TONE_CONFIRM_MS);
        }
        return true;
    }

    return false;
}
