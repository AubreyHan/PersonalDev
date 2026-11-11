#include "LeftDock.h"
#include <stdio.h>
#include <math.h>

namespace {

int8_t rssiToBars(int8_t rssiDbm, bool connected) {
    if (!connected) return 0;
    if (rssiDbm >= -60) return 3;
    if (rssiDbm >= -75) return 2;
    return 1;
}

void copyTruncatedUtf8(char* dst, size_t dstSize, const char* src, size_t maxChars) {
    if (dstSize == 0) return;
    if (src == nullptr || src[0] == '\0') {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    size_t chars = 0;
    while (src[i] != '\0' && chars < maxChars) {
        uint8_t c = static_cast<uint8_t>(src[i]);
        size_t seqLen = 1;
        if ((c & 0xE0U) == 0xC0U) seqLen = 2;
        else if ((c & 0xF0U) == 0xE0U) seqLen = 3;
        else if ((c & 0xF8U) == 0xF0U) seqLen = 4;

        if (i + seqLen >= dstSize) break;
        bool validSeq = true;
        for (size_t k = 1; k < seqLen; ++k) {
            if (src[i + k] == '\0') {
                validSeq = false;
                break;
            }
        }
        if (!validSeq) break;
        i += seqLen;
        chars++;
    }
    memcpy(dst, src, i);
    dst[i] = '\0';
}

} // namespace

void LeftDock::drawZoneChrome(M5Canvas& canvas, int16_t y, int16_t h, bool isActive, bool isPressed, uint32_t accentRgb888) {
    if (isPressed) {
        canvas.fillRoundRect(4, y + 3, Viewport::DOCK_W - 10, h - 6, 10, ThemeColors::BG_CARD_HIGHLIGHT);
        canvas.drawRoundRect(4, y + 3, Viewport::DOCK_W - 10, h - 6, 10, accentRgb888);
    } else if (isActive) {
        canvas.fillRoundRect(4, y + 3, Viewport::DOCK_W - 10, h - 6, 10, ThemeColors::BG_CARD_DARK);
        canvas.fillRoundRect(0, y + 12, 5, h - 24, 2, accentRgb888);
    }

    if (y + h < Viewport::DOCK_H) {
        canvas.drawFastHLine(10, y + h - 1, Viewport::DOCK_W - 22, ThemeColors::BORDER_SUBTLE);
    }
}

void LeftDock::drawHomeIcon(M5Canvas& canvas, int16_t cx, int16_t cy, uint32_t color) {
    // Crisp roof + cabin + door
    canvas.fillTriangle(cx, cy - 13, cx - 15, cy - 1, cx + 15, cy - 1, color);
    canvas.fillRect(cx - 10, cy - 1, 20, 13, color);
    canvas.fillRect(cx - 3, cy + 4, 6, 8, TFT_BLACK);
}

void LeftDock::drawSatelliteIcon(M5Canvas& canvas, int16_t cx, int16_t cy, uint32_t color) {
    // Tilted satellite body + dual solar wings + radio waves
    canvas.drawWideLine(cx - 11, cy - 11, cx - 4, cy - 4, 3.0f, color);
    canvas.drawWideLine(cx + 4, cy + 4, cx + 11, cy + 11, 3.0f, color);
    canvas.fillSmoothCircle(cx, cy, 4, color);
    canvas.drawWideLine(cx - 4, cy + 4, cx + 4, cy - 4, 2.5f, color);
    canvas.drawArc(cx - 2, cy + 2, 10, 9, 105.0f, 165.0f, color);
    canvas.drawArc(cx - 2, cy + 2, 15, 14, 105.0f, 165.0f, color);
}

void LeftDock::drawWifiIcon(M5Canvas& canvas, int16_t cx, int16_t cy, int8_t bars, uint32_t color, bool apMode) {
    if (apMode) {
        // Hotspot broadcast tower + dual side arcs
        canvas.fillSmoothCircle(cx, cy - 2, 3, color);
        canvas.drawWideLine(cx, cy + 1, cx - 5, cy + 11, 1.8f, color);
        canvas.drawWideLine(cx, cy + 1, cx + 5, cy + 11, 1.8f, color);
        canvas.drawArc(cx, cy - 2, 9, 7, 140.0f, 220.0f, color);
        canvas.drawArc(cx, cy - 2, 9, 7, 320.0f, 40.0f, color);
        canvas.drawArc(cx, cy - 2, 14, 12, 140.0f, 220.0f, color);
        canvas.drawArc(cx, cy - 2, 14, 12, 320.0f, 40.0f, color);
        return;
    }

    const uint32_t dimColor = 0x2A3644U;
    canvas.fillSmoothCircle(cx, cy + 8, 3, (bars >= 1) ? color : dimColor);
    canvas.drawArc(cx, cy + 8, 9, 7, 225.0f, 315.0f, (bars >= 1) ? color : dimColor);
    canvas.drawArc(cx, cy + 8, 15, 13, 225.0f, 315.0f, (bars >= 2) ? color : dimColor);
    canvas.drawArc(cx, cy + 8, 21, 19, 225.0f, 315.0f, (bars >= 3) ? color : dimColor);
}

void LeftDock::drawBatteryDualColor(M5Canvas& canvas, int16_t cx, int16_t cy, float socPct, bool isExtPower, bool isCharging, uint32_t color) {
    (void)isCharging;
    const float clamped = fminf(100.0f, fmaxf(0.0f, socPct));

    // Battery shell (width 34, height 18) + positive terminal cap
    const int16_t bx = cx - 19;
    const int16_t by = cy - 9;
    canvas.drawRoundRect(bx, by, 34, 18, 3, color);
    canvas.drawRoundRect(bx + 1, by + 1, 32, 16, 2, color);
    canvas.fillRect(bx + 34, cy - 4, 3, 8, color);

    const int16_t fillMaxW = 26;
    const int16_t fillW = static_cast<int16_t>(lroundf((clamped / 100.0f) * fillMaxW));
    if (fillW > 0) {
        canvas.fillRect(bx + 4, by + 4, fillW, 10, color);
    }

    if (isExtPower) {
        // Crisp lightning bolt symbol right beside battery (⚡ mandate for external power)
        const int16_t lx = cx + 22;
        const int16_t ly = cy;
        canvas.fillTriangle(lx + 2, ly - 10, lx - 6, ly + 1, lx + 1, ly + 1, color);
        canvas.fillTriangle(lx - 1, ly - 1, lx + 6, ly - 1, lx - 2, ly + 10, color);
    }
}

void LeftDock::drawWeatherIcon(M5Canvas& canvas, int16_t cx, int16_t cy, uint8_t wmoCode, bool isDay, bool valid) {
    if (!valid) {
        canvas.fillSmoothCircle(cx - 4, cy + 2, 7, ThemeColors::TEXT_MUTED);
        canvas.fillSmoothCircle(cx + 5, cy + 3, 6, ThemeColors::TEXT_MUTED);
        canvas.fillRect(cx - 4, cy + 3, 10, 6, ThemeColors::TEXT_MUTED);
        return;
    }

    if (wmoCode <= 1) {
        // Clear Sun / Moon
        if (isDay) {
            canvas.fillSmoothCircle(cx, cy, 7, ThemeColors::RACING_YELLOW);
            for (int i = 0; i < 8; ++i) {
                float rad = static_cast<float>(i) * 0.785398f;
                int16_t x0 = cx + static_cast<int16_t>(roundf(cosf(rad) * 10.0f));
                int16_t y0 = cy + static_cast<int16_t>(roundf(sinf(rad) * 10.0f));
                int16_t x1 = cx + static_cast<int16_t>(roundf(cosf(rad) * 13.5f));
                int16_t y1 = cy + static_cast<int16_t>(roundf(sinf(rad) * 13.5f));
                canvas.drawWideLine(x0, y0, x1, y1, 1.3f, ThemeColors::RACING_YELLOW);
            }
        } else {
            canvas.fillSmoothCircle(cx, cy, 9, ThemeColors::ICE_CYAN);
            canvas.fillSmoothCircle(cx + 4, cy - 3, 7, TFT_BLACK);
        }
        return;
    }

    if (wmoCode == 2) {
        // Partly cloudy
        canvas.fillSmoothCircle(cx - 5, cy - 4, 6, ThemeColors::RACING_YELLOW);
    }

    const uint32_t cloudColor = (wmoCode >= 95) ? ThemeColors::ALERT_RED :
                                (wmoCode >= 51) ? ThemeColors::ICE_CYAN : 0xD0D8E2U;
    canvas.fillSmoothCircle(cx - 5, cy + 1, 6, cloudColor);
    canvas.fillSmoothCircle(cx + 3, cy - 1, 7, cloudColor);
    canvas.fillSmoothCircle(cx + 9, cy + 2, 5, cloudColor);
    canvas.fillRect(cx - 5, cy + 1, 14, 6, cloudColor);

    if (wmoCode >= 95) {
        // Thunderstorm bolt
        canvas.drawWideLine(cx + 1, cy + 7, cx - 2, cy + 12, 1.5f, ThemeColors::RACING_YELLOW);
        canvas.drawWideLine(cx - 2, cy + 12, cx + 2, cy + 12, 1.5f, ThemeColors::RACING_YELLOW);
    } else if (wmoCode >= 71 && wmoCode <= 86) {
        // Snowflakes
        canvas.fillCircle(cx - 4, cy + 11, 2, ThemeColors::TEXT_WHITE);
        canvas.fillCircle(cx + 4, cy + 11, 2, ThemeColors::TEXT_WHITE);
    } else if (wmoCode >= 51) {
        // Rain drops
        canvas.drawWideLine(cx - 4, cy + 8, cx - 6, cy + 13, 1.4f, ThemeColors::ICE_CYAN);
        canvas.drawWideLine(cx + 3, cy + 8, cx + 1, cy + 13, 1.4f, ThemeColors::ICE_CYAN);
    }
}

void LeftDock::drawGearIcon(M5Canvas& canvas, int16_t cx, int16_t cy, uint32_t color) {
    for (int i = 0; i < 8; ++i) {
        float rad = static_cast<float>(i) * 0.785398f;
        int16_t tx = cx + static_cast<int16_t>(roundf(cosf(rad) * 10.5f));
        int16_t ty = cy + static_cast<int16_t>(roundf(sinf(rad) * 10.5f));
        canvas.fillSmoothCircle(tx, ty, 3, color);
    }
    canvas.fillSmoothCircle(cx, cy, 9, color);
    canvas.fillSmoothCircle(cx, cy, 4, TFT_BLACK);
}

void LeftDock::render(M5Canvas& canvas, const SystemStateSnapshot& state, PageId activePage) {
    canvas.fillScreen(TFT_BLACK);

    // Subtle right divider separating 160px LeftDock from 1120px Main Viewport
    canvas.drawFastVLine(Viewport::DOCK_W - 2, 0, Viewport::DOCK_H, 0x162230U);
    canvas.drawFastVLine(Viewport::DOCK_W - 1, 0, Viewport::DOCK_H, ThemeColors::BORDER_SUBTLE);

    const int16_t cx = (Viewport::DOCK_W - 2) / 2;
    char buf[40];

    // -------------------------------------------------------------------------
    // 1. HOME / DASHBOARD [Y: 0..89, H: 90]
    // -------------------------------------------------------------------------
    const bool homeActive = (activePage == PAGE_1_DASHBOARD);
    drawZoneChrome(canvas, Viewport::DOCK_HOME_Y, Viewport::DOCK_HOME_H,
                   homeActive, _pressedZone == DOCK_ACTION_HOME, ThemeColors::NEON_GREEN);
    const uint32_t homeColor = homeActive ? ThemeColors::NEON_GREEN : ThemeColors::TEXT_WHITE;
    drawHomeIcon(canvas, cx - 28, Viewport::DOCK_HOME_Y + 45, homeColor);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(homeColor);
    canvas.drawString("仪表", cx - 6, Viewport::DOCK_HOME_Y + 45, &fonts::efontCN_24_b);

    // -------------------------------------------------------------------------
    // 2. CLOCK HH:MM:SS [Y: 90..199, H: 110] -> PAGE_7_WORLD_CLOCK
    // -------------------------------------------------------------------------
    const bool clockActive = (activePage == PAGE_7_WORLD_CLOCK || activePage == SUBPAGE_EDIT_CLOCK);
    drawZoneChrome(canvas, Viewport::DOCK_TIME_Y, Viewport::DOCK_TIME_H,
                   clockActive, _pressedZone == DOCK_ACTION_TIME, ThemeColors::ICE_CYAN);

    snprintf(buf, sizeof(buf), "%02u:%02u", state.time.localHour, state.time.localMinute);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(clockActive ? ThemeColors::ICE_CYAN : ThemeColors::TEXT_WHITE);
    canvas.drawString(buf, cx, Viewport::DOCK_TIME_Y + 40, &fonts::FreeSansBold18pt7b);

    const char* srcTag = "RTC";
    if (state.time.source == CLOCK_SRC_GNSS) srcTag = "GPS";
    else if (state.time.source == CLOCK_SRC_NTP) srcTag = "NTP";
    else if (state.time.source == CLOCK_SRC_UNSYNCED) srcTag = "INIT";

    snprintf(buf, sizeof(buf), ":%02u  %s", state.time.localSecond, srcTag);
    canvas.setTextColor(state.time.isCalibrated ? ThemeColors::NEON_GREEN : ThemeColors::TEXT_MUTED);
    canvas.drawString(buf, cx, Viewport::DOCK_TIME_Y + 82, &fonts::efontCN_16);

    // -------------------------------------------------------------------------
    // 3. GNSS SATELLITES / FIX / REC [Y: 200..304, H: 105] -> SUBPAGE_TOOL_GNSS
    // -------------------------------------------------------------------------
    const bool gnssActive = (activePage == SUBPAGE_TOOL_GNSS);
    drawZoneChrome(canvas, Viewport::DOCK_GNSS_Y, Viewport::DOCK_GNSS_H,
                   gnssActive, _pressedZone == DOCK_ACTION_GNSS, ThemeColors::NEON_GREEN);

    uint32_t fixColor = ThemeColors::TEXT_MUTED;
    const char* fixStr = "搜星";
    if (state.gnss.isInTunnel) {
        fixColor = ThemeColors::ICE_CYAN;
        fixStr = "隧道";
    } else if (state.gnss.isDeadReckoning || state.gnss.fixType == FIX_DEAD_REC) {
        fixColor = ThemeColors::AMBER_WARN;
        fixStr = "DR";
    } else if (state.gnss.fixType >= FIX_3D && state.gnss.gnssFixOk) {
        fixColor = ThemeColors::NEON_GREEN;
        fixStr = "3D";
    } else if (state.gnss.fixType == FIX_2D) {
        fixColor = ThemeColors::RACING_YELLOW;
        fixStr = "2D";
    }

    drawSatelliteIcon(canvas, 34, Viewport::DOCK_GNSS_Y + 38, fixColor);

    const uint8_t dispSats = (state.gnss.numSats > 0) ? state.gnss.numSats : state.gnss.satsInView;
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(dispSats));
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString(buf, 58, Viewport::DOCK_GNSS_Y + 36, &fonts::FreeSansBold12pt7b);

    // Fix badge pill
    canvas.fillRoundRect(102, Viewport::DOCK_GNSS_Y + 23, 46, 26, 6, 0x13222EU);
    canvas.drawRoundRect(102, Viewport::DOCK_GNSS_Y + 23, 46, 26, 6, fixColor);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(fixColor);
    canvas.drawString(fixStr, 125, Viewport::DOCK_GNSS_Y + 36, &fonts::efontCN_16_b);

    // Bottom sub-line: Blinking REC dot if recording, else visible/used or antenna warning
    if (state.gnss.antennaStatus == ANT_SHORT || state.gnss.antennaStatus == ANT_OPEN) {
        canvas.setTextColor(ThemeColors::ALERT_RED);
        canvas.drawString(state.gnss.antennaStatus == ANT_SHORT ? "! 天线短路" : "! 天线开路",
                          cx, Viewport::DOCK_GNSS_Y + 80, &fonts::efontCN_16_b);
    } else if (state.storage.isRecording) {
        const bool blinkOn = ((millis() / 500UL) % 2UL) == 0UL;
        if (blinkOn) {
            canvas.fillSmoothCircle(38, Viewport::DOCK_GNSS_Y + 80, 6, ThemeColors::ALERT_RED);
        } else {
            canvas.drawCircle(38, Viewport::DOCK_GNSS_Y + 80, 6, ThemeColors::ALERT_RED);
        }
        canvas.setTextDatum(middle_left);
        canvas.setTextColor(ThemeColors::ALERT_RED);
        canvas.drawString("REC 录制中", 50, Viewport::DOCK_GNSS_Y + 80, &fonts::efontCN_16_b);
    } else {
        snprintf(buf, sizeof(buf), "可见%u HDOP%.1f",
                 static_cast<unsigned>(state.gnss.satsInView),
                 state.gnss.hdop < 50.0f ? state.gnss.hdop : 0.0f);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(ThemeColors::TEXT_MUTED);
        canvas.drawString(buf, cx, Viewport::DOCK_GNSS_Y + 80, &fonts::efontCN_14);
    }

    // -------------------------------------------------------------------------
    // 4. WI-FI / HOTSPOT STATUS [Y: 305..409, H: 105] -> SUBPAGE_TOOL_WIFI
    // -------------------------------------------------------------------------
    const bool wifiActive = (activePage == SUBPAGE_TOOL_WIFI || activePage == PAGE_WIFI_CONFIG);
    drawZoneChrome(canvas, Viewport::DOCK_WIFI_Y, Viewport::DOCK_WIFI_H,
                   wifiActive, _pressedZone == DOCK_ACTION_WIFI, ThemeColors::ICE_CYAN);

    uint32_t wifiColor = ThemeColors::TEXT_MUTED;
    char wifiLabel[32] = "未连接";
    int8_t bars = 0;
    bool isAp = false;

    if (state.network.softApActive) {
        isAp = true;
        wifiColor = state.network.isShareHotspot ? ThemeColors::RACING_YELLOW : ThemeColors::ICE_CYAN;
        snprintf(wifiLabel, sizeof(wifiLabel), "%s", state.network.isShareHotspot ? "轨迹导出AP" : "扫码配网AP");
    } else if (state.network.staConnected) {
        wifiColor = ThemeColors::NEON_GREEN;
        bars = rssiToBars(state.network.rssiDbm, true);
        if (state.network.connectedSsid[0] != '\0') {
            copyTruncatedUtf8(wifiLabel, sizeof(wifiLabel), state.network.connectedSsid, 9);
        } else {
            snprintf(wifiLabel, sizeof(wifiLabel), "已连接");
        }
    } else if (state.network.mode == WIFI_OP_STA_CONNECTING || state.network.mode == WIFI_OP_STA_SCANNING) {
        wifiColor = ThemeColors::AMBER_WARN;
        bars = 1;
        snprintf(wifiLabel, sizeof(wifiLabel), "连接中...");
    }

    drawWifiIcon(canvas, cx, Viewport::DOCK_WIFI_Y + 34, bars, wifiColor, isAp);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(state.network.staConnected || state.network.softApActive ? ThemeColors::TEXT_WHITE : ThemeColors::TEXT_MUTED);
    canvas.drawString(wifiLabel, cx, Viewport::DOCK_WIFI_Y + 78, &fonts::efontCN_16);

    // -------------------------------------------------------------------------
    // 5. BATTERY & EXTERNAL POWER DUAL-COLOR MANDATE [Y: 410..514, H: 105]
    //    STRICT IRON RULE:
    //    - isExternalPower == true  -> NEON GREEN (#00FFAA) + ⚡ XX%
    //    - isExternalPower == false -> ALERT RED  (#FF3B30) + 🔋 XX%
    // -------------------------------------------------------------------------
    const bool pwrActive = (activePage == SUBPAGE_TOOL_SETTINGS);
    const uint32_t pwrColor = state.power.isExternalPower
                                  ? ThemeColors::POWER_EXT_GREEN
                                  : ThemeColors::POWER_BATT_ONLY_RED;

    drawZoneChrome(canvas, Viewport::DOCK_BATTERY_Y, Viewport::DOCK_BATTERY_H,
                   pwrActive, _pressedZone == DOCK_ACTION_BATTERY, pwrColor);

    const int socInt = static_cast<int>(lroundf(fminf(100.0f, fmaxf(0.0f, state.power.socPercent))));
    drawBatteryDualColor(canvas, cx - 2, Viewport::DOCK_BATTERY_Y + 30,
                         state.power.socPercent, state.power.isExternalPower, state.power.isCharging, pwrColor);

    // Percentage readout in mandatory dual-color
    snprintf(buf, sizeof(buf), "%d%%", socInt);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(pwrColor);
    canvas.drawString(buf, cx, Viewport::DOCK_BATTERY_Y + 64, &fonts::FreeSansBold12pt7b);

    // Sub-status tag: External Power vs Battery Discharge + Voltage
    snprintf(buf, sizeof(buf), "%s %.1fV",
             state.power.isExternalPower ? "外电供电" : "纯电池",
             state.power.busVoltageV);
    canvas.setTextColor(pwrColor);
    canvas.drawString(buf, cx, Viewport::DOCK_BATTERY_Y + 90, &fonts::efontCN_12);

    // -------------------------------------------------------------------------
    // 6. OUTDOOR TEMPERATURE & WEATHER [Y: 515..619, H: 105] -> PAGE_6_WEATHER
    // -------------------------------------------------------------------------
    const bool weatherActive = (activePage == PAGE_6_WEATHER);
    drawZoneChrome(canvas, Viewport::DOCK_WEATHER_Y, Viewport::DOCK_WEATHER_H,
                   weatherActive, _pressedZone == DOCK_ACTION_WEATHER, ThemeColors::RACING_YELLOW);

    drawWeatherIcon(canvas, cx, Viewport::DOCK_WEATHER_Y + 34,
                    state.weather.currentWmoCode, state.weather.isDay, state.weather.hasValidData);

    if (state.weather.hasValidData) {
        snprintf(buf, sizeof(buf), "%.1f°C", state.weather.currentTempC);
        canvas.setTextColor(state.weather.hasHazardWarning ? ThemeColors::ALERT_RED : ThemeColors::TEXT_WHITE);
    } else {
        snprintf(buf, sizeof(buf), "--.-°C");
        canvas.setTextColor(ThemeColors::TEXT_MUTED);
    }
    canvas.setTextDatum(middle_center);
    canvas.drawString(buf, cx, Viewport::DOCK_WEATHER_Y + 78, &fonts::FreeSansBold12pt7b);

    // -------------------------------------------------------------------------
    // 7. SYSTEM TOOLBOX [Y: 620..719, H: 100] -> PAGE_TOOLS_GRID
    // -------------------------------------------------------------------------
    const bool toolsActive = (activePage == PAGE_TOOLS_GRID ||
                              activePage == SUBPAGE_TOOL_TF_MANAGER ||
                              activePage == SUBPAGE_TOOL_ONEDRIVE);
    drawZoneChrome(canvas, Viewport::DOCK_TOOLS_Y, Viewport::DOCK_TOOLS_H,
                   toolsActive, _pressedZone == DOCK_ACTION_TOOLS, ThemeColors::NEON_GREEN);
    const uint32_t toolsColor = toolsActive ? ThemeColors::NEON_GREEN : ThemeColors::TEXT_WHITE;
    drawGearIcon(canvas, cx - 28, Viewport::DOCK_TOOLS_Y + 50, toolsColor);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(toolsColor);
    canvas.drawString("工具", cx - 6, Viewport::DOCK_TOOLS_Y + 50, &fonts::efontCN_24_b);
}
