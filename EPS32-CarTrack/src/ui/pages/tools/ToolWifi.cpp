#include "ToolWifi.h"
#include <stdio.h>
#include <string.h>
#include <type_traits>

static_assert(!std::is_abstract<ToolWifi>::value && std::is_default_constructible<ToolWifi>::value,
              "ToolWifi must be concrete and default-constructible");

namespace {

const char* wifiModeText(WifiOperatingMode mode) {
    switch (mode) {
        case WIFI_OP_STA_CONNECTED:   return "ESP32-C6 Wi-Fi 6 已连接";
        case WIFI_OP_STA_CONNECTING:  return "正在漫游连接热点...";
        case WIFI_OP_STA_SCANNING:    return "正在扫描周边网络...";
        case WIFI_OP_AP_SETUP_PORTAL: return "配网门户热点运行中 (M5Track-Setup)";
        case WIFI_OP_AP_TRACK_SHARE:  return "轨迹导出热点运行中 (M5Track-Share)";
        default:                      return "无线模块待机 / 未连接";
    }
}

uint8_t rssiToBars(int8_t rssiDbm, bool connected) {
    if (!connected) return 0U;
    if (rssiDbm >= -55) return 4U;
    if (rssiDbm >= -67) return 3U;
    if (rssiDbm >= -78) return 2U;
    return 1U;
}

} // namespace

void ToolWifi::onEnter(const SystemStateSnapshot& state) {
    (void)state;
    _armedDeleteSlot = -1;
    _armedDeleteMs   = 0;
}

void ToolWifi::render(M5Canvas& canvas, const SystemStateSnapshot& state) {
    clearViewportCanvas(canvas);

    // Expire two-tap delete confirmation after 3.5s
    if (_armedDeleteSlot >= 0 && (millis() - _armedDeleteMs) > 3500UL) {
        _armedDeleteSlot = -1;
    }

    drawTopBar(canvas);
    drawCurrentConnectionCard(canvas, state);
    drawSavedSlotsCard(canvas, state);
}

void ToolWifi::drawTopBar(M5Canvas& canvas) const {
    canvas.fillRoundRect(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H, 10, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H, 10, ThemeColors::ICE_CYAN);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.setTextDatum(middle_center);
    canvas.drawString("< 工具箱", BTN_BACK_X + BTN_BACK_W / 2, BTN_BACK_Y + BTN_BACK_H / 2);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(middle_left);
    canvas.drawString("Wi-Fi 6 网络管理", 224, 40);

    // Reconnect Roaming Button
    canvas.fillRoundRect(BTN_RECONN_X, BTN_RECONN_Y, BTN_RECONN_W, BTN_RECONN_H, 10, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(BTN_RECONN_X, BTN_RECONN_Y, BTN_RECONN_W, BTN_RECONN_H, 10, ThemeColors::BORDER_SUBTLE);
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(middle_center);
    canvas.drawString("立即扫描漫游", BTN_RECONN_X + BTN_RECONN_W / 2, BTN_RECONN_Y + BTN_RECONN_H / 2);

    // Scan QR to Add New Network Hero Button
    canvas.fillRoundRect(BTN_ADD_QR_X, BTN_ADD_QR_Y, BTN_ADD_QR_W, BTN_ADD_QR_H, 10, 0x0E2E24U);
    canvas.drawRoundRect(BTN_ADD_QR_X, BTN_ADD_QR_Y, BTN_ADD_QR_W, BTN_ADD_QR_H, 10, ThemeColors::NEON_GREEN);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.setTextDatum(middle_center);
    canvas.drawString("+ 扫码添加网络", BTN_ADD_QR_X + BTN_ADD_QR_W / 2, BTN_ADD_QR_Y + BTN_ADD_QR_H / 2);
}

void ToolWifi::drawSignalBars(M5Canvas& canvas, int16_t x, int16_t y, int8_t rssiDbm, bool connected) {
    const uint8_t bars   = rssiToBars(rssiDbm, connected);
    const uint32_t color = (bars >= 3U) ? ThemeColors::NEON_GREEN :
                           (bars == 2U) ? ThemeColors::RACING_YELLOW :
                           (bars == 1U) ? ThemeColors::AMBER_WARN :
                                          0x2A3848U;

    for (uint8_t i = 0; i < 4U; ++i) {
        const int16_t bh = static_cast<int16_t>(14 + i * 10);
        const int16_t bx = x + static_cast<int16_t>(i) * 16;
        const int16_t by = y + (44 - bh);
        const uint32_t c = (i < bars) ? color : 0x223040U;
        canvas.fillRoundRect(bx, by, 11, bh, 3, c);
    }
}

void ToolWifi::drawCurrentConnectionCard(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    const NetworkData& net = state.network;
    const int16_t cx = 18;
    const int16_t cy = 80;
    const int16_t cw = 1084;
    const int16_t ch = 144;

    canvas.fillRoundRect(cx, cy, cw, ch, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(cx, cy, cw, ch, 14, net.staConnected ? ThemeColors::NEON_GREEN : ThemeColors::BORDER_SUBTLE);

    drawSignalBars(canvas, cx + 26, cy + 26, net.rssiDbm, net.staConnected);

    const uint32_t statusCol = net.staConnected ? ThemeColors::NEON_GREEN :
                               (net.softApActive ? ThemeColors::ICE_CYAN : ThemeColors::AMBER_WARN);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(statusCol);
    canvas.setTextDatum(top_left);
    canvas.drawString(wifiModeText(net.mode), cx + 108, cy + 18);

    char ssidLine[96];
    if (net.staConnected) {
        snprintf(ssidLine, sizeof(ssidLine), "当前热点: %s    |    IPv4: %s    |    信号强度: %d dBm",
                 net.connectedSsid, net.staIp, static_cast<int>(net.rssiDbm));
    } else if (net.softApActive) {
        snprintf(ssidLine, sizeof(ssidLine), "车载热点: %s    |    网关: %s    |    已连接终端: %u 台",
                 net.apSsid, net.apIp, static_cast<unsigned>(net.apStationCount));
    } else {
        snprintf(ssidLine, sizeof(ssidLine), "当前未接入互联网 (%s) · 点击右上角【+ 扫码添加网络】极速配网",
                 net.statusText);
    }
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString(ssidLine, cx + 108, cy + 56);

    // Bottom hardware / service telemetry strip
    canvas.drawFastHLine(cx + 22, cy + 92, cw - 44, 0x1B2634U);
    char hwLine[128];
    snprintf(hwLine, sizeof(hwLine),
             "协处理器: ESP32-C6 SDIO (%s)   |   多源 NTP 授时: %s   |   AssistNow AGPS: %s",
             state.boot.wifiCoProcOnline ? "在线" : "初始化中",
             net.ntpSynced ? "已同步" : "等待联网",
             (net.agpsStatus == AGPS_STATE_INJECTED) ? "星历已注入" : "待注入");
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.setTextDatum(middle_left);
    canvas.drawString(hwLine, cx + 26, cy + 116);
}

void ToolWifi::drawSavedSlotsCard(M5Canvas& canvas, const SystemStateSnapshot& state) {
    const NetworkData& net = state.network;

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(middle_left);
    char hdr[80];
    snprintf(hdr, sizeof(hdr), "已保存的漫游网络槽位 (%u / %u)",
             static_cast<unsigned>(net.savedCount),
             static_cast<unsigned>(NetConfig::MAX_SAVED_NETWORKS));
    canvas.drawString(hdr, 22, 244);

    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.setTextDatum(middle_right);
    canvas.drawString("支持手机个人热点 / 车载 CPE / 家用路由器多槽位自动无缝切换", 1100, 244);

    for (uint8_t idx = 0; idx < NetConfig::MAX_SAVED_NETWORKS; ++idx) {
        const int16_t rx = SLOT_LIST_X;
        const int16_t ry = SLOT_LIST_Y0 + static_cast<int16_t>(idx) * (SLOT_ROW_H + SLOT_ROW_GAP);

        const SavedWifiCredential& cred = net.savedList[idx];
        const bool occupied = cred.inUse && (cred.ssid[0] != '\0');
        const bool isCurrent = occupied && net.staConnected && (strcmp(cred.ssid, net.connectedSsid) == 0);
        const bool isArmed   = occupied && (_armedDeleteSlot == static_cast<int8_t>(idx));

        canvas.fillRoundRect(rx, ry, SLOT_ROW_W, SLOT_ROW_H, 12,
                             isCurrent ? 0x102822U : ThemeColors::BG_CARD_DARK);
        canvas.drawRoundRect(rx, ry, SLOT_ROW_W, SLOT_ROW_H, 12,
                             isCurrent ? ThemeColors::NEON_GREEN : ThemeColors::BORDER_SUBTLE);

        // Slot index badge
        char slotNum[12];
        snprintf(slotNum, sizeof(slotNum), "#%u", static_cast<unsigned>(idx + 1U));
        canvas.fillRoundRect(rx + 16, ry + 17, 52, 44, 8, occupied ? 0x182838U : 0x10161EU);
        canvas.setFont(&fonts::DejaVu18);
        canvas.setTextColor(occupied ? ThemeColors::ICE_CYAN : ThemeColors::TEXT_MUTED);
        canvas.setTextDatum(middle_center);
        canvas.drawString(slotNum, rx + 42, ry + 39);

        if (occupied) {
            canvas.setFont(&fonts::DejaVu24);
            canvas.setTextColor(ThemeColors::TEXT_WHITE);
            canvas.setTextDatum(middle_left);
            canvas.drawString(cred.ssid, rx + 86, ry + 39);

            if (isCurrent) {
                canvas.fillRoundRect(rx + 460, ry + 22, 118, 34, 8, 0x0E3B2CU);
                canvas.drawRoundRect(rx + 460, ry + 22, 118, 34, 8, ThemeColors::NEON_GREEN);
                canvas.setFont(&fonts::efontCN_16_b);
                canvas.setTextColor(ThemeColors::NEON_GREEN);
                canvas.setTextDatum(middle_center);
                canvas.drawString("当前已连接", rx + 519, ry + 39);
            } else {
                canvas.setFont(&fonts::efontCN_16);
                canvas.setTextColor(ThemeColors::TEXT_MUTED);
                canvas.setTextDatum(middle_left);
                canvas.drawString(cred.password[0] ? "WPA2/WPA3 加密凭据已保存" : "开放式网络凭据", rx + 460, ry + 39);
            }

            // Delete slot button
            const int16_t dbX = rx + SLOT_ROW_W - DEL_BTN_W - 18;
            const int16_t dbY = ry + (SLOT_ROW_H - DEL_BTN_H) / 2;
            canvas.fillRoundRect(dbX, dbY, DEL_BTN_W, DEL_BTN_H, 10, isArmed ? ThemeColors::ALERT_RED : 0x2A1216U);
            canvas.drawRoundRect(dbX, dbY, DEL_BTN_W, DEL_BTN_H, 10, ThemeColors::ALERT_RED);
            canvas.setFont(&fonts::efontCN_16_b);
            canvas.setTextColor(isArmed ? TFT_WHITE : ThemeColors::ALERT_RED);
            canvas.setTextDatum(middle_center);
            canvas.drawString(isArmed ? "再点确认删除" : "删除槽位", dbX + DEL_BTN_W / 2, dbY + DEL_BTN_H / 2);
        } else {
            canvas.setFont(&fonts::efontCN_24);
            canvas.setTextColor(0x465464U);
            canvas.setTextDatum(middle_left);
            canvas.drawString("空闲漫游槽位 — 点击右上角【+ 扫码添加网络】写入", rx + 86, ry + 39);
        }
    }
}

bool ToolWifi::onTouch(const TouchEvent& event, const SystemStateSnapshot& state) {
    if (event.type != TOUCH_EVT_TAP) {
        return true;
    }

    if (event.hitTestLocal(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H)) {
        _armedDeleteSlot = -1;
        playFeedbackTone(state);
        requestNavigate(PAGE_TOOLS_GRID);
        return true;
    }

    if (event.hitTestLocal(BTN_RECONN_X, BTN_RECONN_Y, BTN_RECONN_W, BTN_RECONN_H)) {
        _armedDeleteSlot = -1;
        {
            StateGuard lock(SystemState::instance());
            if (lock.locked()) {
                auto& net           = SystemState::instance().data().network;
                net.cmdScanWifi     = true;
                net.cmdReconnectSta = true;
            }
        }
        playFeedbackTone(state);
        return true;
    }

    if (event.hitTestLocal(BTN_ADD_QR_X, BTN_ADD_QR_Y, BTN_ADD_QR_W, BTN_ADD_QR_H)) {
        _armedDeleteSlot = -1;
        {
            StateGuard lock(SystemState::instance());
            if (lock.locked()) {
                SystemState::instance().data().network.cmdStartSetupAp = true;
            }
        }
        playFeedbackTone(state, HapticConfig::TONE_CONFIRM_1_HZ, HapticConfig::TONE_CONFIRM_MS);
        requestNavigate(PAGE_WIFI_CONFIG);
        return true;
    }

    // Check per-slot Delete buttons
    for (uint8_t idx = 0; idx < NetConfig::MAX_SAVED_NETWORKS; ++idx) {
        const SavedWifiCredential& cred = state.network.savedList[idx];
        if (!cred.inUse || cred.ssid[0] == '\0') {
            continue;
        }
        const int16_t ry  = SLOT_LIST_Y0 + static_cast<int16_t>(idx) * (SLOT_ROW_H + SLOT_ROW_GAP);
        const int16_t dbX = SLOT_LIST_X + SLOT_ROW_W - DEL_BTN_W - 18;
        const int16_t dbY = ry + (SLOT_ROW_H - DEL_BTN_H) / 2;

        if (event.hitTestLocal(dbX, dbY, DEL_BTN_W, DEL_BTN_H)) {
            if (_armedDeleteSlot == static_cast<int8_t>(idx)) {
                _armedDeleteSlot = -1;
                {
                    StateGuard lock(SystemState::instance());
                    if (lock.locked()) {
                        SystemState::instance().data().network.cmdDeleteSavedIdx = static_cast<int8_t>(idx);
                    }
                }
                playFeedbackTone(state, HapticConfig::TONE_WARN_HZ, HapticConfig::TONE_WARN_MS);
            } else {
                _armedDeleteSlot = static_cast<int8_t>(idx);
                _armedDeleteMs   = millis();
                playFeedbackTone(state);
            }
            return true;
        }
    }

    _armedDeleteSlot = -1;
    return true;
}
