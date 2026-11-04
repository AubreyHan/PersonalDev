#include "ui/pages/PageWifiConfig.h"
#include "service/NetworkManager.h"

void PageWifiConfig::render(M5Canvas& canvas, const SystemContext& ctx) {
    if (!ctx.wifi.apSetupActive) {
        AppNetworkManager::instance().startSetupPortal();
    }

    // Auto-exit after 3 seconds upon successful Wi-Fi connection
    if (ctx.wifi.isConnected) {
        if (_successAutoExitMs == 0) {
            _successAutoExitMs = millis();
        } else if (millis() - _successAutoExitMs > 3000) {
            _successAutoExitMs = 0;
            AppNetworkManager::instance().stopPortals();
            SystemState& state = SystemState::instance();
            state.lock();
            state.getContextUnsafe().currentPage = (ctx.gnss.isValid && ctx.gnss.numSats >= 4) ? PAGE_1_DASHBOARD : PAGE_INIT;
            state.unlock();
            return;
        }
    } else {
        _successAutoExitMs = 0;
    }

    beginViewport(canvas, true); // Full 1280x720 Single-Scan Provisioning View

    // Top Header Bar
    canvas.fillRect(0, 0, 1280, 64, UiColors::COLOR_PANEL_BG);
    canvas.drawFastHLine(0, 64, 1280, UiColors::COLOR_BORDER);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.setTextSize(2);
    canvas.drawString("  Wi-Fi 一键扫码配网 (One-Scan Smart Provisioning)", 20, 32);

    // Exit Button on Top-Right
    canvas.fillRoundRect(1080, 10, 176, 44, 8, UiColors::COLOR_DANGER);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString("返回 / Back", 1168, 32);

    // Left Section: Unified QR Code Card (520x600)
    canvas.fillRoundRect(50, 85, 520, 600, 12, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(50, 85, 520, 600, 12, UiColors::COLOR_PRIMARY);

    canvas.setTextDatum(top_center);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.setTextSize(2);
    canvas.drawString("手机扫码一键配网", 310, 105);

    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString("热点: M5Track-Setup  (无需密码，扫码直连)", 310, 140);

    // Draw Crisp Unified Single QR Code (320x320)
    canvas.fillRect(135, 175, 350, 350, UiColors::COLOR_WHITE);
    canvas.qrcode("WIFI:T:nopass;S:M5Track-Setup;;", 150, 190, 320, 2);

    // Status pill under QR Code
    canvas.setTextDatum(middle_center);
    canvas.setTextSize(2);
    if (ctx.wifi.isConnected) {
        canvas.fillRoundRect(80, 545, 460, 54, 8, 0x03E0); // Bright Green
        canvas.setTextColor(UiColors::COLOR_WHITE);
        canvas.drawString("Wi-Fi 已成功连接！", 310, 572);
    } else if (ctx.wifi.apStations > 0) {
        canvas.fillRoundRect(80, 545, 460, 54, 8, UiColors::COLOR_PRIMARY);
        canvas.setTextColor(UiColors::COLOR_WHITE);
        canvas.drawString("手机已连入，正打开网页...", 310, 572);
    } else if (ctx.wifi.apSetupActive) {
        canvas.fillRoundRect(80, 545, 460, 54, 8, 0x1A6D); // Soft Slate Blue
        canvas.setTextColor(UiColors::COLOR_WHITE);
        canvas.drawString("等待手机扫码接入...", 310, 572);
    } else {
        canvas.fillRoundRect(80, 545, 460, 54, 8, UiColors::COLOR_WARN);
        canvas.setTextColor(UiColors::COLOR_BG);
        canvas.drawString("正在启动 SoftAP 配网...", 310, 572);
    }

    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString("iOS / Android / 微信扫一扫均支持直连", 310, 615);

    // Right Section: Step-by-Step Guide & Live Telemetry Card (640x600)
    canvas.fillRoundRect(590, 85, 640, 600, 12, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(590, 85, 640, 600, 12, UiColors::COLOR_SECONDARY);

    canvas.setTextDatum(top_left);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.setTextSize(2);
    canvas.drawString("单次扫码操作指引 (One-Scan Workflow)", 620, 105);

    // Guide Step 1
    canvas.fillRoundRect(620, 145, 46, 36, 6, UiColors::COLOR_PRIMARY);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.setTextSize(2);
    canvas.drawString("1", 643, 163);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString("打开手机「相机」或「微信」扫描左侧二维码", 680, 163);

    // Guide Step 2
    canvas.fillRoundRect(620, 205, 46, 36, 6, UiColors::COLOR_PRIMARY);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString("2", 643, 223);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString("点击系统弹窗提示：「加入网络 M5Track-Setup」", 680, 223);

    // Guide Step 3
    canvas.fillRoundRect(620, 265, 46, 36, 6, UiColors::COLOR_SECONDARY);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString("3", 643, 283);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.drawString("【自动弹窗】无需二次扫码，秒开配网界面！", 680, 283);

    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString("在自动弹出的网页中选择您的 Wi-Fi 并输入密码即可完成配置", 680, 310);

    // Status Panel Box (Y: 345, H: 220)
    canvas.fillRoundRect(620, 345, 580, 225, 8, UiColors::COLOR_CARD_BG);
    canvas.drawRoundRect(620, 345, 580, 225, 8, UiColors::COLOR_BORDER);

    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString("设备实时网络状态 (Live Status):", 640, 365);

    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString("热点 SSID   : M5Track-Setup (开放式/无需密码)", 640, 400);
    canvas.drawString("网关地址    : 192.168.4.1 (已开启全域 Captive Portal 劫持)", 640, 425);

    char devBuf[64];
    snprintf(devBuf, sizeof(devBuf), "连入手机数  : %u 台客户端", ctx.wifi.apStations);
    canvas.drawString(devBuf, 640, 450);

    // Dynamic State Line
    canvas.setTextSize(2);
    if (ctx.wifi.isConnected) {
        canvas.setTextColor(0x03E0);
        char connStr[96];
        snprintf(connStr, sizeof(connStr), "连接成功: %s (%s) 即将返回...", ctx.wifi.ssid, ctx.wifi.ipAddr);
        canvas.drawString(connStr, 640, 485);
    } else if (ctx.wifi.portalConnectState == 1) { // CONNECTING
        canvas.setTextColor(UiColors::COLOR_WARN);
        canvas.drawString("正在尝试连接目标 Wi-Fi 路由...", 640, 485);
    } else if (ctx.wifi.portalConnectState == 2) { // SUCCESS
        canvas.setTextColor(0x03E0);
        canvas.drawString("密码验证成功！正在切换连接...", 640, 485);
    } else if (ctx.wifi.portalConnectState == 3) { // FAILED
        canvas.setTextColor(UiColors::COLOR_DANGER);
        canvas.drawString("密码错误或超时，请在手机端重试", 640, 485);
    } else if (ctx.wifi.apStations > 0) {
        canvas.setTextColor(UiColors::COLOR_PRIMARY);
        canvas.drawString("手机已连入，配网网页已推送", 640, 485);
    } else {
        canvas.setTextColor(UiColors::COLOR_MUTED);
        canvas.drawString("等待手机扫描加入热点...", 640, 485);
    }

    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString("如果手机已连入，请在弹窗中选择车机或家用 Wi-Fi 并提交保存", 640, 520);

    // Bottom Fallback Notice
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(UiColors::COLOR_WARN);
    canvas.setTextSize(1);
    canvas.drawString("💡 备用提示: 若极个别机型未自动弹出网页，连入后浏览器手动打开 192.168.4.1 即可", 620, 605);

    endViewport(canvas);
}

bool PageWifiConfig::onTouchTap(int16_t x, int16_t y, SystemContext& ctx) {
    // Exit button (X: 1080..1256, Y: 10..54)
    if (x >= 1080 && x <= 1256 && y >= 10 && y <= 64) {
        _successAutoExitMs = 0;
        AppNetworkManager::instance().stopPortals();
        ctx.currentPage = (ctx.gnss.isValid && ctx.gnss.numSats >= 4) ? PAGE_1_DASHBOARD : PAGE_INIT;
        return true;
    }
    return false;
}
