#include "ToolSettings.h"
#include <stdio.h>
#include <math.h>
#include <type_traits>

static_assert(!std::is_abstract<ToolSettings>::value && std::is_default_constructible<ToolSettings>::value,
              "ToolSettings must be concrete and default-constructible");

namespace {

const char* standbyPhaseLabel(StandbyLifecyclePhase phase) {
    switch (phase) {
        case STANDBY_PHASE_SETTLING_TRIPS: return "正在闭卷结算行程";
        case STANDBY_PHASE_PRE_SLEEP_SYNC: return "休眠前抢传云盘";
        case STANDBY_PHASE_SLEEPING:       return "80MHz 浅睡守候中";
        case STANDBY_PHASE_WAKING_UP:      return "震动唤醒恢复中";
        case STANDBY_PHASE_ACTIVE:
        default:                           return "360MHz 全速活跃";
    }
}

} // namespace

void ToolSettings::onEnter(const SystemStateSnapshot& state) {
    (void)state;
    _draggingSlider  = false;
    _armedSubReset   = false;
    _armedSubResetMs = 0;
}

void ToolSettings::render(M5Canvas& canvas, const SystemStateSnapshot& state) {
    clearViewportCanvas(canvas);

    if (_armedSubReset && (millis() - _armedSubResetMs) > 3500UL) {
        _armedSubReset = false;
    }

    drawTopHeader(canvas);
    drawDisplayAndAudioCard(canvas, state);
    drawSubTripQuickCard(canvas, state);
    drawIna226DashboardCard(canvas, state);
    drawFirmwareSysInfoCard(canvas, state);
}

void ToolSettings::drawTopHeader(M5Canvas& canvas) const {
    canvas.fillRoundRect(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H, 10, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H, 10, ThemeColors::ICE_CYAN);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.setTextDatum(middle_center);
    canvas.drawString("< 工具箱", BTN_BACK_X + BTN_BACK_W / 2, BTN_BACK_Y + BTN_BACK_H / 2);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(middle_left);
    canvas.drawString("系统偏好与 INA226 复合库仑计监控", 224, 40);
}

void ToolSettings::drawDisplayAndAudioCard(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    const UserSettings& cfg = state.settings;
    const int16_t cx = 18;
    const int16_t cy = 80;
    const int16_t cw = 526;
    const int16_t ch = 296;

    canvas.fillRoundRect(cx, cy, cw, ch, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(cx, cy, cw, ch, 14, ThemeColors::BORDER_SUBTLE);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(top_left);
    canvas.drawString("屏幕背光与交互音效", cx + 20, cy + 16);

    const uint8_t brightPct = static_cast<uint8_t>((static_cast<uint32_t>(cfg.screenBrightness) * 100U) / 255U);
    char brtTxt[32];
    snprintf(brtTxt, sizeof(brtTxt), "%u%% (%u/255)",
             static_cast<unsigned>(brightPct),
             static_cast<unsigned>(cfg.screenBrightness));
    canvas.setFont(&fonts::DejaVu18);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.setTextDatum(top_right);
    canvas.drawString(brtTxt, cx + cw - 20, cy + 20);

    // Large Touch Backlight Slider
    const int16_t trackX = SLIDER_HIT_X + 10;
    const int16_t trackY = SLIDER_HIT_Y + 20;
    const int16_t trackW = SLIDER_HIT_W - 20;
    const int16_t trackH = 28;

    canvas.fillRoundRect(trackX, trackY, trackW, trackH, 14, 0x182432U);
    float ratio = static_cast<float>(cfg.screenBrightness - 10) / 245.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    const int16_t fillW = static_cast<int16_t>(ratio * static_cast<float>(trackW));
    if (fillW > 8) {
        canvas.fillRoundRect(trackX, trackY, fillW, trackH, 14, ThemeColors::NEON_GREEN);
    }
    canvas.drawRoundRect(trackX, trackY, trackW, trackH, 14, ThemeColors::BORDER_SUBTLE);

    const int16_t knobX = trackX + fillW;
    const int16_t knobY = trackY + trackH / 2;
    canvas.fillCircle(knobX, knobY, 20, TFT_WHITE);
    canvas.drawCircle(knobX, knobY, 21, ThemeColors::NEON_GREEN);

    // Auto Day/Night Backlight Toggle
    const bool autoBrt = cfg.autoDayNightBacklight;
    canvas.fillRoundRect(BTN_AUTOBRIGHT_X, BTN_AUTOBRIGHT_Y, BTN_AUTOBRIGHT_W, BTN_AUTOBRIGHT_H, 12,
                         autoBrt ? 0x0E2B22U : 0x131D28U);
    canvas.drawRoundRect(BTN_AUTOBRIGHT_X, BTN_AUTOBRIGHT_Y, BTN_AUTOBRIGHT_W, BTN_AUTOBRIGHT_H, 12,
                         autoBrt ? ThemeColors::NEON_GREEN : ThemeColors::BORDER_SUBTLE);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(autoBrt ? ThemeColors::NEON_GREEN : ThemeColors::TEXT_MUTED);
    canvas.setTextDatum(middle_left);
    canvas.drawString("自动日夜防眩目背光", BTN_AUTOBRIGHT_X + 20, BTN_AUTOBRIGHT_Y + BTN_AUTOBRIGHT_H / 2);
    canvas.setTextDatum(middle_right);
    canvas.drawString(autoBrt ? "已开启" : "已关闭",
                      BTN_AUTOBRIGHT_X + BTN_AUTOBRIGHT_W - 20,
                      BTN_AUTOBRIGHT_Y + BTN_AUTOBRIGHT_H / 2);

    // Key Beep Toggle (ES8388 + NS4150B Speaker)
    const bool beepOn = cfg.speakerBeepEnabled;
    canvas.fillRoundRect(BTN_BEEP_X, BTN_BEEP_Y, BTN_BEEP_W, BTN_BEEP_H, 12,
                         beepOn ? 0x0F2636U : 0x131D28U);
    canvas.drawRoundRect(BTN_BEEP_X, BTN_BEEP_Y, BTN_BEEP_W, BTN_BEEP_H, 12,
                         beepOn ? ThemeColors::ICE_CYAN : ThemeColors::BORDER_SUBTLE);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(beepOn ? ThemeColors::ICE_CYAN : ThemeColors::TEXT_MUTED);
    canvas.setTextDatum(middle_left);
    canvas.drawString("触控按键提示音 (ES8388)", BTN_BEEP_X + 20, BTN_BEEP_Y + BTN_BEEP_H / 2);
    canvas.setTextDatum(middle_right);
    canvas.drawString(beepOn ? "已开启" : "静音",
                      BTN_BEEP_X + BTN_BEEP_W - 20,
                      BTN_BEEP_Y + BTN_BEEP_H / 2);
}

void ToolSettings::drawSubTripQuickCard(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    const TripControlData& tc = state.tripControl;
    const TripStats& subStats = state.trips[TRIP_MANUAL];

    const int16_t cx = 18;
    const int16_t cy = 390;
    const int16_t cw = 526;
    const int16_t ch = 310;

    canvas.fillRoundRect(cx, cy, cw, ch, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(cx, cy, cw, ch, 14, ThemeColors::BORDER_SUBTLE);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::RACING_YELLOW);
    canvas.setTextDatum(top_left);
    canvas.drawString("手动小记 SUB 快捷切卷与清零", cx + 20, cy + 16);

    char subInfo[96];
    if (tc.manualTripActive) {
        snprintf(subInfo, sizeof(subInfo), "状态: 记录中 (sub%03u-%03u)  |  累计 %.2f km",
                 static_cast<unsigned>(tc.manualSubIndex),
                 static_cast<unsigned>(tc.manualSplitIndex),
                 subStats.tripDistanceKm);
    } else {
        snprintf(subInfo, sizeof(subInfo), "状态: 当前未开启跨日小记  |  上次累计 %.2f km",
                 subStats.tripDistanceKm);
    }
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString(subInfo, cx + 20, cy + 54);

    // Button 1: Start Manual SUB or Split / Break Segment
    canvas.fillRoundRect(BTN_SUB_ACT_X, BTN_SUB_ACT_Y, BTN_SUB_ACT_W, BTN_SUB_ACT_H, 12,
                         tc.manualTripActive ? 0x162A38U : 0x123224U);
    canvas.drawRoundRect(BTN_SUB_ACT_X, BTN_SUB_ACT_Y, BTN_SUB_ACT_W, BTN_SUB_ACT_H, 12,
                         tc.manualTripActive ? ThemeColors::ICE_CYAN : ThemeColors::NEON_GREEN);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(tc.manualTripActive ? ThemeColors::ICE_CYAN : ThemeColors::NEON_GREEN);
    canvas.setTextDatum(middle_center);
    canvas.drawString(tc.manualTripActive ? "SUB 手动切段" : "+ 开启小记 SUB",
                      BTN_SUB_ACT_X + BTN_SUB_ACT_W / 2,
                      BTN_SUB_ACT_Y + BTN_SUB_ACT_H / 2);

    // Button 2: Stop & Zero SUB Trip
    canvas.fillRoundRect(BTN_SUB_RESET_X, BTN_SUB_RESET_Y, BTN_SUB_RESET_W, BTN_SUB_RESET_H, 12,
                         _armedSubReset ? ThemeColors::ALERT_RED : 0x2A1216U);
    canvas.drawRoundRect(BTN_SUB_RESET_X, BTN_SUB_RESET_Y, BTN_SUB_RESET_W, BTN_SUB_RESET_H, 12,
                         ThemeColors::ALERT_RED);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(_armedSubReset ? TFT_WHITE : ThemeColors::ALERT_RED);
    canvas.setTextDatum(middle_center);
    canvas.drawString(_armedSubReset ? "再点确认清零" : "结束并清零 SUB",
                      BTN_SUB_RESET_X + BTN_SUB_RESET_W / 2,
                      BTN_SUB_RESET_Y + BTN_SUB_RESET_H / 2);

    canvas.drawFastHLine(cx + 20, cy + 182, cw - 40, 0x1B2634U);
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.setTextDatum(top_left);
    canvas.drawString("· 开启小记后主页将自动显示 [小记 SUB] 专属统计页签", cx + 20, cy + 198);
    canvas.drawString("· 单卷满 10.0 MiB 自动无缝分卷，亦可随时点击手动切段", cx + 20, cy + 228);
    canvas.drawString("· 结束清零将安全闭合当前 GPX 文件并归零小记里程", cx + 20, cy + 258);
}

void ToolSettings::drawIna226DashboardCard(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    const PowerData& pwr = state.power;
    const int16_t cx = 560;
    const int16_t cy = 80;
    const int16_t cw = 542;
    const int16_t ch = 414;

    // Mandatory PRD Dual-Color Power Rule: External Power = NEON_GREEN, Battery Only = ALERT_RED
    const uint32_t pwrAccent = pwr.isExternalPower ? ThemeColors::POWER_EXT_GREEN : ThemeColors::POWER_BATT_ONLY_RED;

    canvas.fillRoundRect(cx, cy, cw, ch, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(cx, cy, cw, ch, 14, pwrAccent);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(top_left);
    canvas.drawString("INA226 复合库仑计", cx + 20, cy + 14);

    canvas.setFont(&fonts::DejaVu18);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.setTextDatum(top_right);
    canvas.drawString("I2C 0x41 · 5mOhm", cx + cw - 20, cy + 18);

    // Power Source Banner
    canvas.fillRoundRect(cx + 20, cy + 52, cw - 40, 44, 10, pwr.isExternalPower ? 0x0E2E22U : 0x341214U);
    canvas.drawRoundRect(cx + 20, cy + 52, cw - 40, 44, 10, pwrAccent);
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextColor(pwrAccent);
    canvas.setTextDatum(middle_center);
    const char* srcStr = pwr.isExternalPower
                             ? (pwr.isCharging ? "外接车载供电在线 (USB-C / HVIN) · 正在充电"
                                               : "外接车载供电在线 (USB-C / HVIN) · 满电浮充")
                             : "外接电源已断开 · 纯 NP-F550 锂电池放电中";
    canvas.drawString(srcStr, cx + cw / 2, cy + 74);

    // SoC & Remaining mAh Bar
    float soc = pwr.socPercent;
    if (soc < 0.0f)   soc = 0.0f;
    if (soc > 100.0f) soc = 100.0f;

    const int16_t barX = cx + 20;
    const int16_t barY = cy + 110;
    const int16_t barW = cw - 40;
    const int16_t barH = 30;
    canvas.fillRoundRect(barX, barY, barW, barH, 8, 0x162230U);
    const int16_t socFill = static_cast<int16_t>((soc / 100.0f) * static_cast<float>(barW));
    if (socFill > 6) {
        canvas.fillRoundRect(barX, barY, socFill, barH, 8, pwrAccent);
    }
    canvas.drawRoundRect(barX, barY, barW, barH, 8, ThemeColors::BORDER_SUBTLE);

    char capLabel[80];
    snprintf(capLabel, sizeof(capLabel), "剩余容量 %.0f / %.0f mAh  (SoC %.1f%%)",
             pwr.remainingMah, CoulombCounterConfig::BATTERY_RATED_MAH, soc);
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(middle_center);
    canvas.drawString(capLabel, barX + barW / 2, barY + barH / 2);

    // 2x2 Electrical Telemetry Matrix
    const int16_t cellW = 244;
    const int16_t cellH = 92;
    const int16_t gX0   = cx + 20;
    const int16_t gY0   = cy + 154;

    struct MetricBox {
        const char* label;
        char        val[24];
        char        sub[36];
        uint32_t    color;
    };
    MetricBox boxes[4] = {};

    boxes[0].label = "2S 总线电压 Vbus";
    snprintf(boxes[0].val, sizeof(boxes[0].val), "%.2f V", pwr.busVoltageV);
    snprintf(boxes[0].sub, sizeof(boxes[0].sub), "分流压降 %+.2f mV", pwr.shuntVoltageMv);
    boxes[0].color = pwr.lowBattCutoffActive ? ThemeColors::ALERT_RED : ThemeColors::NEON_GREEN;

    boxes[1].label = "充放电电流 Ishunt";
    snprintf(boxes[1].val, sizeof(boxes[1].val), "%+.0f mA", pwr.currentMa);
    snprintf(boxes[1].sub, sizeof(boxes[1].sub), "30s 均值 %+.0f mA", pwr.avgCurrentMa);
    boxes[1].color = (pwr.currentMa >= 0.0f) ? ThemeColors::NEON_GREEN : ThemeColors::AMBER_WARN;

    boxes[2].label = "实时瞬态功耗 P";
    snprintf(boxes[2].val, sizeof(boxes[2].val), "%.0f mW", fabs(pwr.powerMw));
    snprintf(boxes[2].sub, sizeof(boxes[2].sub), "约 %.2f W 整机负载", fabs(pwr.powerMw) / 1000.0f);
    boxes[2].color = ThemeColors::ICE_CYAN;

    const uint32_t maxStandstill = MotionPowerConfig::STANDSTILL_SLEEP_TIMEOUT_MS / 1000UL; // 1200s
    const uint32_t remSec = (pwr.standstillSeconds < maxStandstill) ? (maxStandstill - pwr.standstillSeconds) : 0U;
    boxes[3].label = "静止自动休眠倒计时";
    snprintf(boxes[3].val, sizeof(boxes[3].val), "%02u:%02u",
             static_cast<unsigned>(remSec / 60U),
             static_cast<unsigned>(remSec % 60U));
    snprintf(boxes[3].sub, sizeof(boxes[3].sub), "%s", standbyPhaseLabel(pwr.standbyPhase));
    boxes[3].color = (remSec < 120U) ? ThemeColors::RACING_YELLOW : ThemeColors::TEXT_WHITE;

    for (int i = 0; i < 4; ++i) {
        const int16_t bx = gX0 + (i % 2) * (cellW + 14);
        const int16_t by = gY0 + (i / 2) * (cellH + 12);

        canvas.fillRoundRect(bx, by, cellW, cellH, 10, 0x121C28U);
        canvas.drawRoundRect(bx, by, cellW, cellH, 10, 0x203040U);

        canvas.setFont(&fonts::efontCN_14);
        canvas.setTextColor(ThemeColors::TEXT_MUTED);
        canvas.setTextDatum(top_left);
        canvas.drawString(boxes[i].label, bx + 12, by + 8);

        canvas.setFont(&fonts::DejaVu24);
        canvas.setTextColor(boxes[i].color);
        canvas.drawString(boxes[i].val, bx + 12, by + 32);

        canvas.setFont(&fonts::efontCN_14);
        canvas.setTextColor(ThemeColors::TEXT_MUTED);
        canvas.drawString(boxes[i].sub, bx + 12, by + 66);
    }

    // INA226 Sensor health footer
    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(pwr.isAvailable ? ThemeColors::TEXT_MUTED : ThemeColors::AMBER_WARN);
    canvas.setTextDatum(middle_left);
    canvas.drawString(pwr.isAvailable
                          ? "库仑积分 + 11点2S开路电压(OCV)静态漂移双引擎校准已激活"
                          : "INA226 (0x41) 离线：当前显示默认保护估算值",
                      cx + 20, cy + ch - 22);
}

void ToolSettings::drawFirmwareSysInfoCard(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    const int16_t cx = 560;
    const int16_t cy = 508;
    const int16_t cw = 542;
    const int16_t ch = 192;

    canvas.fillRoundRect(cx, cy, cw, ch, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(cx, cy, cw, ch, 14, ThemeColors::BORDER_SUBTLE);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(top_left);
    canvas.drawString("M5Track-Tab5 固件与硬件基线", cx + 20, cy + 14);

    canvas.setFont(&fonts::DejaVu18);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.setTextDatum(top_right);
    canvas.drawString("v2.0-P4", cx + cw - 20, cy + 18);

    const float freePsramMb  = static_cast<float>(ESP.getFreePsram()) / (1024.0f * 1024.0f);
    const float totalPsramMb = static_cast<float>(ESP.getPsramSize()) / (1024.0f * 1024.0f);
    const float freeHeapKb   = static_cast<float>(ESP.getFreeHeap()) / 1024.0f;

    char line1[96];
    snprintf(line1, sizeof(line1), "SoC: ESP32-P4 RISC-V @ %uMHz  |  PSRAM %.1f/%.0fMB  |  SRAM %.0fKB",
             static_cast<unsigned>(ESP.getCpuFreqMHz()),
             freePsramMb, totalPsramMb, freeHeapKb);

    char line2[96];
    snprintf(line2, sizeof(line2), "传感器: BMI270(0x68:%s / 0x69:%s) · BMP280(0x76:%s) · RTC(0x32:%s)",
             state.imu.masterOnline ? "OK" : "--",
             state.imu.slaveOnline  ? "OK" : "--",
             state.baro.isOnline    ? "OK" : "--",
             state.boot.rtcOnline   ? "OK" : "--");

    char line3[96];
    snprintf(line3, sizeof(line3), "隧道轴线库: %s (%u 条高速隧道 · 版本 %s)",
             state.tunnelDb.loadedInPsram ? "PSRAM 已常驻" : "待加载",
             static_cast<unsigned>(state.tunnelDb.tunnelCount),
             state.tunnelDb.version);

    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(top_left);
    canvas.drawString(line1, cx + 20, cy + 56);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString(line2, cx + 20, cy + 96);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString(line3, cx + 20, cy + 136);
}

void ToolSettings::updateBrightnessFromTouchX(int16_t localX, bool persistNvs) {
    const int16_t trackX = SLIDER_HIT_X + 10;
    const int16_t trackW = SLIDER_HIT_W - 20;

    float ratio = static_cast<float>(localX - trackX) / static_cast<float>(trackW);
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    const uint8_t newBright = static_cast<uint8_t>(roundf(10.0f + ratio * 245.0f));

    // Delegate actual hardware M5.Display.setBrightness() exclusively to UiManager::updateBacklightAndAudio()
    StateGuard lock(SystemState::instance());
    if (lock.locked()) {
        auto& cfg            = SystemState::instance().data().settings;
        cfg.screenBrightness = newBright;
        if (persistNvs) {
            cfg.cmdSaveSettingsToNvs = true;
        }
    }
}

bool ToolSettings::onTouch(const TouchEvent& event, const SystemStateSnapshot& state) {
    if (event.type == TOUCH_EVT_DOWN) {
        if (event.hitTestLocal(SLIDER_HIT_X, SLIDER_HIT_Y, SLIDER_HIT_W, SLIDER_HIT_H)) {
            _draggingSlider = true;
            updateBrightnessFromTouchX(event.localX, false);
            return true;
        }
        return true;
    }

    if (event.type == TOUCH_EVT_MOVE) {
        if (_draggingSlider) {
            updateBrightnessFromTouchX(event.localX, false);
        }
        return true;
    }

    if (event.type == TOUCH_EVT_UP) {
        if (_draggingSlider) {
            _draggingSlider = false;
            updateBrightnessFromTouchX(event.localX, true);
        }
        return true;
    }

    if (event.type == TOUCH_EVT_TAP) {
        if (_draggingSlider) {
            _draggingSlider = false;
            updateBrightnessFromTouchX(event.localX, true);
            playFeedbackTone(state);
            return true;
        }

        if (event.hitTestLocal(SLIDER_HIT_X, SLIDER_HIT_Y, SLIDER_HIT_W, SLIDER_HIT_H)) {
            updateBrightnessFromTouchX(event.localX, true);
            playFeedbackTone(state);
            return true;
        }

        if (event.hitTestLocal(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H)) {
            _armedSubReset = false;
            playFeedbackTone(state);
            requestNavigate(PAGE_TOOLS_GRID);
            return true;
        }

        if (event.hitTestLocal(BTN_AUTOBRIGHT_X, BTN_AUTOBRIGHT_Y, BTN_AUTOBRIGHT_W, BTN_AUTOBRIGHT_H)) {
            {
                StateGuard lock(SystemState::instance());
                if (lock.locked()) {
                    auto& cfg                  = SystemState::instance().data().settings;
                    cfg.autoDayNightBacklight  = !cfg.autoDayNightBacklight;
                    cfg.cmdSaveSettingsToNvs   = true;
                }
            }
            playFeedbackTone(state);
            return true;
        }

        if (event.hitTestLocal(BTN_BEEP_X, BTN_BEEP_Y, BTN_BEEP_W, BTN_BEEP_H)) {
            bool enabledAfter = false;
            {
                StateGuard lock(SystemState::instance());
                if (lock.locked()) {
                    auto& cfg                = SystemState::instance().data().settings;
                    cfg.speakerBeepEnabled   = !cfg.speakerBeepEnabled;
                    cfg.cmdSaveSettingsToNvs = true;
                    enabledAfter             = cfg.speakerBeepEnabled;
                }
            }
            if (enabledAfter && M5.Speaker.isEnabled()) {
                M5.Speaker.tone(HapticConfig::TONE_TAP_HZ, HapticConfig::TONE_TAP_MS);
            }
            return true;
        }

        // Manual SUB Start or SUB-only Segment Break (decoupled from SINGLE trip reset)
        if (event.hitTestLocal(BTN_SUB_ACT_X, BTN_SUB_ACT_Y, BTN_SUB_ACT_W, BTN_SUB_ACT_H)) {
            _armedSubReset = false;
            {
                StateGuard lock(SystemState::instance());
                if (lock.locked()) {
                    auto& tc = SystemState::instance().data().tripControl;
                    if (tc.manualTripActive) {
                        tc.cmdSplitManualTrip = true;
                    } else {
                        tc.cmdStartManualTrip = true;
                        tc.activeViewCategory = TRIP_MANUAL;
                    }
                }
            }
            playFeedbackTone(state, HapticConfig::TONE_CONFIRM_1_HZ, HapticConfig::TONE_CONFIRM_MS);
            return true;
        }

        // Manual SUB Stop & Settle (2-step anti-mistouch; TrackLogger settles CSV/GPX before zeroing)
        if (event.hitTestLocal(BTN_SUB_RESET_X, BTN_SUB_RESET_Y, BTN_SUB_RESET_W, BTN_SUB_RESET_H)) {
            if (_armedSubReset) {
                _armedSubReset = false;
                {
                    StateGuard lock(SystemState::instance());
                    if (lock.locked()) {
                        auto& tc = SystemState::instance().data().tripControl;
                        tc.cmdStopManualTrip = true;
                        if (tc.activeViewCategory == TRIP_MANUAL) {
                            tc.activeViewCategory = TRIP_SINGLE;
                        }
                    }
                }
                playFeedbackTone(state, HapticConfig::TONE_TRIP_RESET_HZ, HapticConfig::TONE_TRIP_RESET_MS);
            } else {
                _armedSubReset   = true;
                _armedSubResetMs = millis();
                playFeedbackTone(state, HapticConfig::TONE_WARN_HZ, HapticConfig::TONE_WARN_MS);
            }
            return true;
        }

        _armedSubReset = false;
    }

    return true;
}
