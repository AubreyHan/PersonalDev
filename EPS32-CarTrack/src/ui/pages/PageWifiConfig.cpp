#include "PageWifiConfig.h"
#include <stdio.h>

namespace {

constexpr int16_t BTN_BACK_X = 24;
constexpr int16_t BTN_BACK_Y = 14;
constexpr int16_t BTN_BACK_W = 160;
constexpr int16_t BTN_BACK_H = 52;

constexpr int16_t LEFT_CARD_X  = 34;
constexpr int16_t RIGHT_CARD_X = 576;
constexpr int16_t QR_CARD_Y    = 84;
constexpr int16_t QR_CARD_W    = 510;
constexpr int16_t QR_CARD_H    = 516;
constexpr int16_t QR_BOX_SIZE  = 292;

} // namespace

void PageWifiConfig::onEnter(const SystemStateSnapshot& state) {
    _backPressed         = false;
    _autoCloseScheduled  = false;
    _provisionedDetectMs = 0;
    _returnPage          = state.boot.autoEnteredDashboard ? PAGE_1_DASHBOARD : PAGE_INIT;

    StateGuard guard(SystemState::instance());
    if (guard.locked()) {
        auto& net               = SystemState::instance().data().network;
        net.portalProvisionedOk = false;
        net.cmdStartSetupAp     = true;
    }
}

void PageWifiConfig::onExit() {
    StateGuard guard(SystemState::instance());
    if (guard.locked()) {
        auto& net = SystemState::instance().data().network;
        if (!net.isShareHotspot) {
            net.cmdStopAp = true;
        }
    }
}

void PageWifiConfig::render(M5Canvas& canvas, const SystemStateSnapshot& state) {
    clearViewportCanvas(canvas);
    const uint32_t nowMs = millis();

    // Handle 3-second auto-return once portal provisioning succeeds
    if (state.network.portalProvisionedOk) {
        if (!_autoCloseScheduled) {
            _autoCloseScheduled  = true;
            _provisionedDetectMs = (state.network.portalOkMillis > 0) ? state.network.portalOkMillis : nowMs;
            playFeedbackTone(state, HapticConfig::TONE_CONFIRM_2_HZ, HapticConfig::TONE_CONFIRM_MS);
        } else if ((nowMs - _provisionedDetectMs) >= 3000UL) {
            requestNavigate(PAGE_1_DASHBOARD);
        }
    }

    // Top bar with Back Button & Title
    canvas.fillRoundRect(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H, 10,
                         _backPressed ? ThemeColors::BG_CARD_HIGHLIGHT : ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H, 10, ThemeColors::BORDER_SUBTLE);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString("<- 返回", BTN_BACK_X + BTN_BACK_W / 2, BTN_BACK_Y + BTN_BACK_H / 2);

    canvas.setTextDatum(textdatum_t::middle_left);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.drawString("M5Track-Setup 手机双码极速配网", 206, 40);

    const char* apSsid = (state.network.apSsid[0] != '\0' && !state.network.isShareHotspot)
                             ? state.network.apSsid
                             : NetConfig::AP_SETUP_SSID;
    const char* qrWifi = (state.network.qrWifiConnect[0] != '\0' && !state.network.isShareHotspot)
                             ? state.network.qrWifiConnect
                             : NetConfig::QR_SETUP_WIFI_PAYLOAD;
    const char* qrUrl  = (state.network.qrWebPortalUrl[0] != '\0' && !state.network.isShareHotspot)
                             ? state.network.qrWebPortalUrl
                             : NetConfig::QR_SETUP_URL_PAYLOAD;

    // --- Left Card: Step 1 Scan to Connect SoftAP ---
    canvas.fillRoundRect(LEFT_CARD_X, QR_CARD_Y, QR_CARD_W, QR_CARD_H, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(LEFT_CARD_X, QR_CARD_Y, QR_CARD_W, QR_CARD_H, 14, ThemeColors::NEON_GREEN);

    canvas.fillRoundRect(LEFT_CARD_X + 20, QR_CARD_Y + 18, 110, 34, 8, ThemeColors::NEON_GREEN);
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextColor(TFT_BLACK);
    canvas.drawString("STEP 1", LEFT_CARD_X + 75, QR_CARD_Y + 35);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextDatum(textdatum_t::middle_left);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString("扫码自动连接本机热点", LEFT_CARD_X + 144, QR_CARD_Y + 35);

    // High-contrast crisp white pad + QR code
    const int16_t qr1X = LEFT_CARD_X + (QR_CARD_W - QR_BOX_SIZE) / 2;
    const int16_t qr1Y = QR_CARD_Y + 74;
    canvas.fillRoundRect(qr1X - 10, qr1Y - 10, QR_BOX_SIZE + 20, QR_BOX_SIZE + 20, 10, TFT_WHITE);
    canvas.qrcode(qrWifi, qr1X, qr1Y, QR_BOX_SIZE, 1, true);

    char ssidCaption[64];
    snprintf(ssidCaption, sizeof(ssidCaption), "热点名称: %s  (免密直连)", apSsid);
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.drawString(ssidCaption, LEFT_CARD_X + QR_CARD_W / 2, QR_CARD_Y + 414);

    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("连接后手机一般会自动弹出配网认证页", LEFT_CARD_X + QR_CARD_W / 2, QR_CARD_Y + 452);
    canvas.drawString("支持保存最多 5 组热点、AGPS 与 OneDrive 参数", LEFT_CARD_X + QR_CARD_W / 2, QR_CARD_Y + 480);

    // --- Right Card: Step 2 Scan to Open Captive Portal URL ---
    canvas.fillRoundRect(RIGHT_CARD_X, QR_CARD_Y, QR_CARD_W, QR_CARD_H, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(RIGHT_CARD_X, QR_CARD_Y, QR_CARD_W, QR_CARD_H, 14, ThemeColors::ICE_CYAN);

    canvas.fillRoundRect(RIGHT_CARD_X + 20, QR_CARD_Y + 18, 110, 34, 8, ThemeColors::ICE_CYAN);
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextColor(TFT_BLACK);
    canvas.drawString("STEP 2", RIGHT_CARD_X + 75, QR_CARD_Y + 35);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextDatum(textdatum_t::middle_left);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString("扫码打开配网配置门户", RIGHT_CARD_X + 144, QR_CARD_Y + 35);

    const int16_t qr2X = RIGHT_CARD_X + (QR_CARD_W - QR_BOX_SIZE) / 2;
    const int16_t qr2Y = QR_CARD_Y + 74;
    canvas.fillRoundRect(qr2X - 10, qr2Y - 10, QR_BOX_SIZE + 20, QR_BOX_SIZE + 20, 10, TFT_WHITE);
    canvas.qrcode(qrUrl, qr2X, qr2Y, QR_BOX_SIZE, 1, true);

    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString("http://192.168.4.1", RIGHT_CARD_X + QR_CARD_W / 2, QR_CARD_Y + 414);

    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("若手机未自动弹窗，请连上热点后扫此码", RIGHT_CARD_X + QR_CARD_W / 2, QR_CARD_Y + 452);
    canvas.drawString("或在浏览器直接访问 http://192.168.4.1/setup", RIGHT_CARD_X + QR_CARD_W / 2, QR_CARD_Y + 480);

    // --- Bottom Live Status Bar ---
    const int16_t barX = 34;
    const int16_t barY = 616;
    const int16_t barW = 1052;
    const int16_t barH = 84;

    uint32_t barBorder = ThemeColors::BORDER_SUBTLE;
    if (state.network.portalProvisionedOk) {
        barBorder = ThemeColors::NEON_GREEN;
    } else if (state.network.apStationCount > 0) {
        barBorder = ThemeColors::ICE_CYAN;
    } else if (state.network.softApActive) {
        barBorder = ThemeColors::RACING_YELLOW;
    }

    canvas.fillRoundRect(barX, barY, barW, barH, 12, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(barX, barY, barW, barH, 12, barBorder);

    canvas.fillSmoothCircle(barX + 34, barY + barH / 2, 9, barBorder);

    char statusMain[160];
    char statusSub[160];

    if (state.network.portalProvisionedOk) {
        const uint32_t elapsed = (nowMs >= _provisionedDetectMs) ? (nowMs - _provisionedDetectMs) : 0UL;
        const uint32_t remSec  = (elapsed < 3000UL) ? ((3000UL - elapsed + 999UL) / 1000UL) : 0UL;
        snprintf(statusMain, sizeof(statusMain), "配网成功！已连接 %s (%s)",
                 state.network.connectedSsid, state.network.staIp);
        snprintf(statusSub, sizeof(statusSub), "%lu 秒后自动关闭配网热点并返回主仪表盘...",
                 static_cast<unsigned long>(remSec));
    } else if (state.network.softApActive) {
        if (state.network.apStationCount > 0) {
            snprintf(statusMain, sizeof(statusMain), "已有 %u 台手机接入热点 [%s]，请在手机网页提交 Wi-Fi 密码",
                     static_cast<unsigned>(state.network.apStationCount), apSsid);
        } else {
            snprintf(statusMain, sizeof(statusMain), "配网热点 [%s] 已就绪 (IP: %s) — 等待手机扫码接入...",
                     apSsid, state.network.apIp);
        }
        snprintf(statusSub, sizeof(statusSub), "已保存热点槽位: %u / %u 组  |  提交保存后将自动连网并注入星历",
                 static_cast<unsigned>(state.network.savedCount),
                 static_cast<unsigned>(NetConfig::MAX_SAVED_NETWORKS));
    } else {
        snprintf(statusMain, sizeof(statusMain), "正在启动 ESP32-C6 SoftAP 配网热点 [%s]...", apSsid);
        snprintf(statusSub, sizeof(statusSub), "当前状态: %s", state.network.statusText);
    }

    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString(statusMain, barX + 60, barY + 16);

    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString(statusSub, barX + 60, barY + 48);
}

bool PageWifiConfig::onTouch(const TouchEvent& event, const SystemStateSnapshot& state) {
    if (event.type == TOUCH_EVT_DOWN || event.type == TOUCH_EVT_MOVE) {
        _backPressed = event.hitTestLocal(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H);
        return _backPressed;
    }

    if (event.type == TOUCH_EVT_TAP) {
        _backPressed = false;
        if (event.hitTestLocal(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H)) {
            playFeedbackTone(state);
            requestNavigate(_returnPage);
            return true;
        }
    }

    if (event.type == TOUCH_EVT_UP) {
        _backPressed = false;
    }
    return false;
}
