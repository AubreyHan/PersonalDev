#include "ToolOneDrive.h"
#include <stdio.h>
#include <string.h>
#include <type_traits>

static_assert(!std::is_abstract<ToolOneDrive>::value && std::is_default_constructible<ToolOneDrive>::value,
              "ToolOneDrive must be concrete and default-constructible");

namespace {

const char* transferStateLabel(OneDriveTransferState st) {
    switch (st) {
        case ONEDRIVE_SYNCING:        return "正在执行 320KiB 分片上传...";
        case ONEDRIVE_PRE_SLEEP_SYNC: return "驻车休眠前紧急抢传闭卷轨迹...";
        case ONEDRIVE_SYNC_OK:        return "最近一次云端增量同步成功";
        case ONEDRIVE_SYNC_ERR:       return "上次上传遇到网络波动 (将自动重试)";
        case ONEDRIVE_IDLE:
        default:                      return "空闲待命 (每 60s 自动扫描未同步账本)";
    }
}

uint32_t transferStateColor(OneDriveTransferState st) {
    switch (st) {
        case ONEDRIVE_SYNCING:        return ThemeColors::ICE_CYAN;
        case ONEDRIVE_PRE_SLEEP_SYNC: return ThemeColors::RACING_YELLOW;
        case ONEDRIVE_SYNC_OK:        return ThemeColors::NEON_GREEN;
        case ONEDRIVE_SYNC_ERR:       return ThemeColors::ALERT_RED;
        default:                      return ThemeColors::TEXT_MUTED;
    }
}

} // namespace

void ToolOneDrive::onEnter(const SystemStateSnapshot& state) {
    (void)state;
    _showUnbindConfirm = false;
}

void ToolOneDrive::render(M5Canvas& canvas, const SystemStateSnapshot& state) {
    clearViewportCanvas(canvas);
    drawTopHeader(canvas, state);

    const OneDriveData& od = state.oneDrive;
    if (od.isBound || od.bindState == ONEDRIVE_BOUND_ACTIVE) {
        drawBoundActiveView(canvas, state);
        if (_showUnbindConfirm) {
            drawUnbindConfirmModal(canvas);
        }
    } else if (od.bindState == ONEDRIVE_WAIT_USER_VERIFY || od.userCode[0] != '\0') {
        drawDeviceCodeVerifyView(canvas, state);
    } else {
        drawUnboundView(canvas, state);
    }
}

void ToolOneDrive::drawTopHeader(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    canvas.fillRoundRect(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H, 10, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H, 10, ThemeColors::ICE_CYAN);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.setTextDatum(middle_center);
    canvas.drawString("< 工具箱", BTN_BACK_X + BTN_BACK_W / 2, BTN_BACK_Y + BTN_BACK_H / 2);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(middle_left);
    canvas.drawString("Microsoft OneDrive 云端同步", 224, 40);

    const bool netOk = state.network.staConnected;
    canvas.fillRoundRect(838, 16, 264, 48, 10, netOk ? 0x0E2A22U : 0x2A1A0CU);
    canvas.drawRoundRect(838, 16, 264, 48, 10, netOk ? ThemeColors::NEON_GREEN : ThemeColors::AMBER_WARN);
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextColor(netOk ? ThemeColors::NEON_GREEN : ThemeColors::AMBER_WARN);
    canvas.setTextDatum(middle_center);
    canvas.drawString(netOk ? "互联网已就绪 · 支持云同步" : "Wi-Fi 未连接 · 待联网后同步", 970, 40);
}

void ToolOneDrive::drawUnboundView(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    const OneDriveData& od = state.oneDrive;
    const int16_t cx = 18;
    const int16_t cy = 84;
    const int16_t cw = 1084;
    const int16_t ch = 610;

    canvas.fillRoundRect(cx, cy, cw, ch, 16, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(cx, cy, cw, ch, 16, ThemeColors::BORDER_SUBTLE);

    // Vector Cloud Icon
    const int16_t icX = cx + cw / 2;
    const int16_t icY = cy + 92;
    canvas.fillCircle(icX - 28, icY + 8, 28, ThemeColors::ICE_CYAN);
    canvas.fillCircle(icX + 8, icY - 8, 36, ThemeColors::ICE_CYAN);
    canvas.fillCircle(icX + 40, icY + 12, 24, ThemeColors::ICE_CYAN);
    canvas.fillRect(icX - 30, icY + 10, 72, 26, ThemeColors::ICE_CYAN);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(top_center);
    canvas.drawString("免车机键盘输入 · RFC 8628 设备码扫码授权", cx + cw / 2, cy + 160);

    canvas.setFont(&fonts::efontCN_24);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("绑定后车机将自动把已闭卷的 GPX 轨迹与 trips.csv 同步至云盘 /VehicleTracks", cx + cw / 2, cy + 206);

    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString("大文件 (>=4MiB) 自动采用 320KiB 分片会话续传 · 正在录制的活跃句柄自动跳过保护", cx + cw / 2, cy + 250);

    // Client ID & Error status box
    const bool hasClientId = (state.settings.oneDriveClientId[0] != '\0') ||
                             (CloudConfig::ONEDRIVE_DEFAULT_CLIENT_ID[0] != '\0');
    canvas.fillRoundRect(cx + 160, cy + 296, cw - 320, 96, 12, 0x121C28U);
    canvas.drawRoundRect(cx + 160, cy + 296, cw - 320, 96, 12,
                         (od.bindState == ONEDRIVE_AUTH_FAILED) ? ThemeColors::ALERT_RED : ThemeColors::BORDER_SUBTLE);

    if (od.bindState == ONEDRIVE_REQ_DEVICE_CODE) {
        canvas.setFont(&fonts::efontCN_24_b);
        canvas.setTextColor(ThemeColors::NEON_GREEN);
        canvas.setTextDatum(middle_center);
        canvas.drawString("正在向 login.microsoftonline.com 请求设备验证码...", cx + cw / 2, cy + 344);
    } else if (od.bindState == ONEDRIVE_AUTH_FAILED || od.lastError[0] != '\0') {
        canvas.setFont(&fonts::efontCN_16_b);
        canvas.setTextColor(ThemeColors::ALERT_RED);
        canvas.setTextDatum(middle_center);
        char errBuf[128];
        snprintf(errBuf, sizeof(errBuf), "授权提示: %s",
                 od.lastError[0] ? od.lastError : "请求超时或未配置 Azure Client ID");
        canvas.drawString(errBuf, cx + cw / 2, cy + 344);
    } else {
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextColor(hasClientId ? ThemeColors::NEON_GREEN : ThemeColors::AMBER_WARN);
        canvas.setTextDatum(middle_center);
        canvas.drawString(hasClientId
                              ? "Azure Application Client ID 已就绪，点击下方按钮生成扫码授权码"
                              : "提示：若尚未写入 Azure Client ID，可通过配网门户网页端一键填入保存",
                          cx + cw / 2, cy + 344);
    }

    // Hero Start Device Code Button
    const bool busy = (od.bindState == ONEDRIVE_REQ_DEVICE_CODE);
    canvas.fillRoundRect(BTN_START_AUTH_X, BTN_START_AUTH_Y, BTN_START_AUTH_W, BTN_START_AUTH_H, 14,
                         busy ? 0x162834U : 0x0E3226U);
    canvas.drawRoundRect(BTN_START_AUTH_X, BTN_START_AUTH_Y, BTN_START_AUTH_W, BTN_START_AUTH_H, 14,
                         ThemeColors::NEON_GREEN);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.setTextDatum(middle_center);
    canvas.drawString(busy ? "正在请求验证码，请稍候..." : "发起 Device Code 扫码绑定",
                      BTN_START_AUTH_X + BTN_START_AUTH_W / 2,
                      BTN_START_AUTH_Y + BTN_START_AUTH_H / 2);
}

void ToolOneDrive::drawDeviceCodeVerifyView(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    const OneDriveData& od = state.oneDrive;
    const int16_t cx = 18;
    const int16_t cy = 84;
    const int16_t cw = 1084;
    const int16_t ch = 610;

    canvas.fillRoundRect(cx, cy, cw, ch, 16, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(cx, cy, cw, ch, 16, ThemeColors::ICE_CYAN);

    // Left Column: QR Code for verificationUri
    const char* uri = (od.verificationUri[0] != '\0') ? od.verificationUri : "https://microsoft.com/devicelogin";
    const int16_t qrSize = 280;
    const int16_t qx     = cx + 76;
    const int16_t qy     = cy + 104;

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.setTextDatum(top_center);
    canvas.drawString("1. 手机扫码打开微软验证页", qx + qrSize / 2, cy + 36);

    canvas.fillRoundRect(qx - 16, qy - 16, qrSize + 32, qrSize + 32, 14, TFT_WHITE);
    canvas.qrcode(uri, qx, qy, qrSize, 5);

    canvas.setFont(&fonts::DejaVu18);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(top_center);
    canvas.drawString(uri, qx + qrSize / 2, qy + qrSize + 30);

    // Right Column: Highlighted Magnified 8~9 Char User Code
    const int16_t rx = cx + 448;
    const int16_t rw = 590;

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.setTextDatum(top_left);
    canvas.drawString("2. 在手机页面输入以下设备授权码：", rx, cy + 36);

    // Magnified Code Box
    canvas.fillRoundRect(rx, cy + 96, rw, 156, 16, 0x10241EU);
    canvas.drawRoundRect(rx, cy + 96, rw, 156, 16, ThemeColors::NEON_GREEN);
    canvas.drawRoundRect(rx + 1, cy + 97, rw - 2, 154, 15, ThemeColors::NEON_GREEN);

    const char* codeStr = (od.userCode[0] != '\0') ? od.userCode : "--------";
    canvas.setFont(&fonts::DejaVu56);
    canvas.setTextColor(ThemeColors::RACING_YELLOW);
    canvas.setTextDatum(middle_center);
    canvas.drawString(codeStr, rx + rw / 2, cy + 174);

    // Expiration countdown & polling info
    char expBuf[96];
    snprintf(expBuf, sizeof(expBuf), "验证码剩余有效期：%02u:%02u   |   车机正在后台自动轮询授权结果",
             static_cast<unsigned>(od.deviceCodeExpiresSec / 60U),
             static_cast<unsigned>(od.deviceCodeExpiresSec % 60U));
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextColor(ThemeColors::AMBER_WARN);
    canvas.setTextDatum(top_left);
    canvas.drawString(expBuf, rx, cy + 280);

    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("手机点击【允许访问】后无需操作车机，页面将自动切换至已绑定状态", rx, cy + 318);
    canvas.drawString("Refresh Token 将加密持久化于片上 NVS，后续发车全自动静默续期", rx, cy + 350);

    // Cancel Device Code Button
    canvas.fillRoundRect(BTN_CANCEL_AUTH_X, BTN_CANCEL_AUTH_Y, BTN_CANCEL_AUTH_W, BTN_CANCEL_AUTH_H, 12, 0x2A1216U);
    canvas.drawRoundRect(BTN_CANCEL_AUTH_X, BTN_CANCEL_AUTH_Y, BTN_CANCEL_AUTH_W, BTN_CANCEL_AUTH_H, 12, ThemeColors::ALERT_RED);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::ALERT_RED);
    canvas.setTextDatum(middle_center);
    canvas.drawString("取消本次扫码绑定",
                      BTN_CANCEL_AUTH_X + BTN_CANCEL_AUTH_W / 2,
                      BTN_CANCEL_AUTH_Y + BTN_CANCEL_AUTH_H / 2);
}

void ToolOneDrive::drawBoundActiveView(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    const OneDriveData& od = state.oneDrive;

    // 1. Account & Remote Path Card
    canvas.fillRoundRect(18, 82, 1084, 118, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(18, 82, 1084, 118, 14, ThemeColors::NEON_GREEN);

    canvas.fillCircle(48, 120, 8, ThemeColors::NEON_GREEN);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.setTextDatum(middle_left);
    canvas.drawString("Microsoft Graph 云盘已绑定", 68, 120);

    char acctBuf[96];
    snprintf(acctBuf, sizeof(acctBuf), "绑定账号: %s",
             od.accountEmail[0] ? od.accountEmail : "已授权个人/组织 OneDrive 账户");
    canvas.setFont(&fonts::efontCN_24);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(middle_right);
    canvas.drawString(acctBuf, 1078, 120);

    canvas.drawFastHLine(40, 148, 1040, 0x1B2634U);
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.setTextDatum(middle_left);
    canvas.drawString("云端归档根目录: /VehicleTracks/{single,daily,sub,trips.csv}   |   增量账本: /tracks/.synced_ledger",
                      40, 174);

    // 2. Synced vs Pending Ledger Stat Cards
    const int16_t statY = 214;
    const int16_t statW = 534;
    const int16_t statH = 166;

    // Left: Synced Total Files
    canvas.fillRoundRect(18, statY, statW, statH, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(18, statY, statW, statH, 14, ThemeColors::BORDER_SUBTLE);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.setTextDatum(top_left);
    canvas.drawString("累计已同步上云文件数", 42, statY + 18);

    char syncCnt[24];
    snprintf(syncCnt, sizeof(syncCnt), "%u", static_cast<unsigned>(od.syncedTotalFiles));
    canvas.setFont(&fonts::DejaVu72);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.setTextDatum(bottom_left);
    canvas.drawString(syncCnt, 42, statY + 146);

    canvas.setFont(&fonts::efontCN_24);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("个文件已安全归档", 240, statY + 138);

    // Right: Pending Files
    canvas.fillRoundRect(568, statY, statW, statH, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(568, statY, statW, statH, 14, ThemeColors::BORDER_SUBTLE);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.setTextDatum(top_left);
    canvas.drawString("本地待上传闭卷文件数", 592, statY + 18);

    char pendCnt[24];
    snprintf(pendCnt, sizeof(pendCnt), "%u", static_cast<unsigned>(od.pendingFiles));
    const uint32_t pendCol = (od.pendingFiles > 0U) ? ThemeColors::RACING_YELLOW : ThemeColors::ICE_CYAN;
    canvas.setFont(&fonts::DejaVu72);
    canvas.setTextColor(pendCol);
    canvas.setTextDatum(bottom_left);
    canvas.drawString(pendCnt, 592, statY + 146);

    canvas.setFont(&fonts::efontCN_24);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString((od.pendingFiles > 0U) ? "个闭卷轨迹待上传" : "全部闭卷轨迹已同步", 790, statY + 138);

    // 3. Chunked Upload Progress Card
    const int16_t progY = 394;
    const int16_t progH = 184;
    canvas.fillRoundRect(18, progY, 1084, progH, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(18, progY, 1084, progH, 14, ThemeColors::BORDER_SUBTLE);

    const uint32_t stCol = transferStateColor(od.transferState);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(stCol);
    canvas.setTextDatum(top_left);
    canvas.drawString(transferStateLabel(od.transferState), 42, progY + 18);

    const uint8_t pct = (od.currentFilePercent > 100U) ? 100U : od.currentFilePercent;
    char pctBuf[16];
    snprintf(pctBuf, sizeof(pctBuf), "%u%%", static_cast<unsigned>(pct));
    canvas.setFont(&fonts::DejaVu40);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(top_right);
    canvas.drawString(pctBuf, 1078, progY + 14);

    // Progress Bar
    const int16_t barX = 42;
    const int16_t barY = progY + 68;
    const int16_t barW = 1036;
    const int16_t barH = 34;
    canvas.fillRoundRect(barX, barY, barW, barH, 8, 0x162230U);
    if (pct > 0U) {
        const int16_t fw = static_cast<int16_t>((static_cast<int>(pct) * barW) / 100);
        canvas.fillRoundRect(barX, barY, (fw < 12) ? 12 : fw, barH, 8, ThemeColors::ICE_CYAN);
    }
    canvas.drawRoundRect(barX, barY, barW, barH, 8, ThemeColors::BORDER_SUBTLE);

    char fileLine[128];
    if (od.currentFileName[0] != '\0') {
        snprintf(fileLine, sizeof(fileLine), "当前传输文件: %s  (320KiB 标准分片会话)", od.currentFileName);
    } else if (od.lastError[0] != '\0') {
        snprintf(fileLine, sizeof(fileLine), "最近诊断记录: %s", od.lastError);
    } else {
        snprintf(fileLine, sizeof(fileLine), "当前无传输任务 · 驻车满 20 分钟休眠前将自动触发 25 秒闭卷抢传");
    }
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.setTextDatum(top_left);
    canvas.drawString(fileLine, 42, progY + 122);

    // 4. Bottom 3 Interactive Buttons: [Sync Now] [Auto Sync Toggle] [Unbind]
    const bool syncing = (od.transferState == ONEDRIVE_SYNCING || od.transferState == ONEDRIVE_PRE_SLEEP_SYNC);
    canvas.fillRoundRect(BTN_SYNC_NOW_X, BTN_SYNC_NOW_Y, BTN_SYNC_NOW_W, BTN_SYNC_NOW_H, 14, 0x0F2B3AU);
    canvas.drawRoundRect(BTN_SYNC_NOW_X, BTN_SYNC_NOW_Y, BTN_SYNC_NOW_W, BTN_SYNC_NOW_H, 14, ThemeColors::ICE_CYAN);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.setTextDatum(middle_center);
    canvas.drawString(syncing ? "正在云端同步中..." : "立即增量同步",
                      BTN_SYNC_NOW_X + BTN_SYNC_NOW_W / 2,
                      BTN_SYNC_NOW_Y + BTN_SYNC_NOW_H / 2);

    const bool autoOn = od.autoSyncEnabled;
    canvas.fillRoundRect(BTN_AUTO_SYNC_X, BTN_AUTO_SYNC_Y, BTN_AUTO_SYNC_W, BTN_AUTO_SYNC_H, 14,
                         autoOn ? 0x0E2E22U : ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(BTN_AUTO_SYNC_X, BTN_AUTO_SYNC_Y, BTN_AUTO_SYNC_W, BTN_AUTO_SYNC_H, 14,
                         autoOn ? ThemeColors::NEON_GREEN : ThemeColors::BORDER_SUBTLE);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(autoOn ? ThemeColors::NEON_GREEN : ThemeColors::TEXT_MUTED);
    canvas.setTextDatum(middle_center);
    canvas.drawString(autoOn ? "自动同步: 已开启" : "自动同步: 已关闭",
                      BTN_AUTO_SYNC_X + BTN_AUTO_SYNC_W / 2,
                      BTN_AUTO_SYNC_Y + BTN_AUTO_SYNC_H / 2);

    canvas.fillRoundRect(BTN_UNBIND_X, BTN_UNBIND_Y, BTN_UNBIND_W, BTN_UNBIND_H, 14, 0x2A1014U);
    canvas.drawRoundRect(BTN_UNBIND_X, BTN_UNBIND_Y, BTN_UNBIND_W, BTN_UNBIND_H, 14, ThemeColors::ALERT_RED);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::ALERT_RED);
    canvas.setTextDatum(middle_center);
    canvas.drawString("解除绑定",
                      BTN_UNBIND_X + BTN_UNBIND_W / 2,
                      BTN_UNBIND_Y + BTN_UNBIND_H / 2);
}

void ToolOneDrive::drawUnbindConfirmModal(M5Canvas& canvas) const {
    const int16_t mx = 210;
    const int16_t my = 190;
    const int16_t mw = 700;
    const int16_t mh = 340;

    canvas.fillRoundRect(mx, my, mw, mh, 18, 0x120B0EU);
    canvas.drawRoundRect(mx, my, mw, mh, 18, ThemeColors::ALERT_RED);
    canvas.drawRoundRect(mx + 1, my + 1, mw - 2, mh - 2, 17, ThemeColors::ALERT_RED);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::ALERT_RED);
    canvas.setTextDatum(top_center);
    canvas.drawString("确认解除 Microsoft OneDrive 绑定？", mx + mw / 2, my + 38);

    canvas.setFont(&fonts::efontCN_24);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString("解绑将清除车机 NVS 中的 OAuth 令牌，本地 TF 卡轨迹不受影响", mx + mw / 2, my + 112);

    canvas.fillRoundRect(BTN_UNBIND_CANCEL_X, BTN_UNBIND_CANCEL_Y, BTN_UNBIND_CANCEL_W, BTN_UNBIND_CANCEL_H, 12,
                         ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(BTN_UNBIND_CANCEL_X, BTN_UNBIND_CANCEL_Y, BTN_UNBIND_CANCEL_W, BTN_UNBIND_CANCEL_H, 12,
                         ThemeColors::ICE_CYAN);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.setTextDatum(middle_center);
    canvas.drawString("保留绑定",
                      BTN_UNBIND_CANCEL_X + BTN_UNBIND_CANCEL_W / 2,
                      BTN_UNBIND_CANCEL_Y + BTN_UNBIND_CANCEL_H / 2);

    canvas.fillRoundRect(BTN_UNBIND_CONFIRM_X, BTN_UNBIND_CONFIRM_Y, BTN_UNBIND_CONFIRM_W, BTN_UNBIND_CONFIRM_H, 12,
                         ThemeColors::ALERT_RED);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextDatum(middle_center);
    canvas.drawString("确认解绑",
                      BTN_UNBIND_CONFIRM_X + BTN_UNBIND_CONFIRM_W / 2,
                      BTN_UNBIND_CONFIRM_Y + BTN_UNBIND_CONFIRM_H / 2);
}

bool ToolOneDrive::onTouch(const TouchEvent& event, const SystemStateSnapshot& state) {
    if (event.type != TOUCH_EVT_TAP) {
        return true;
    }

    if (_showUnbindConfirm) {
        if (event.hitTestLocal(BTN_UNBIND_CANCEL_X, BTN_UNBIND_CANCEL_Y, BTN_UNBIND_CANCEL_W, BTN_UNBIND_CANCEL_H)) {
            _showUnbindConfirm = false;
            playFeedbackTone(state);
            return true;
        }
        if (event.hitTestLocal(BTN_UNBIND_CONFIRM_X, BTN_UNBIND_CONFIRM_Y, BTN_UNBIND_CONFIRM_W, BTN_UNBIND_CONFIRM_H)) {
            _showUnbindConfirm = false;
            {
                StateGuard lock(SystemState::instance());
                if (lock.locked()) {
                    SystemState::instance().data().oneDrive.cmdUnbind = true;
                }
            }
            playFeedbackTone(state, HapticConfig::TONE_WARN_HZ, HapticConfig::TONE_WARN_MS);
            return true;
        }
        return true;
    }

    if (event.hitTestLocal(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H)) {
        playFeedbackTone(state);
        requestNavigate(PAGE_TOOLS_GRID);
        return true;
    }

    const OneDriveData& od = state.oneDrive;
    if (od.isBound || od.bindState == ONEDRIVE_BOUND_ACTIVE) {
        if (event.hitTestLocal(BTN_SYNC_NOW_X, BTN_SYNC_NOW_Y, BTN_SYNC_NOW_W, BTN_SYNC_NOW_H)) {
            {
                StateGuard lock(SystemState::instance());
                if (lock.locked()) {
                    SystemState::instance().data().oneDrive.cmdSyncNow = true;
                }
            }
            playFeedbackTone(state, HapticConfig::TONE_CONFIRM_1_HZ, HapticConfig::TONE_CONFIRM_MS);
            return true;
        }

        if (event.hitTestLocal(BTN_AUTO_SYNC_X, BTN_AUTO_SYNC_Y, BTN_AUTO_SYNC_W, BTN_AUTO_SYNC_H)) {
            {
                StateGuard lock(SystemState::instance());
                if (lock.locked()) {
                    auto& st                           = SystemState::instance().data();
                    const bool nextAuto                = !st.oneDrive.autoSyncEnabled;
                    st.oneDrive.autoSyncEnabled        = nextAuto;
                    st.oneDrive.requestedAutoSyncVal   = nextAuto;
                    st.oneDrive.cmdSetAutoSync         = true;
                    st.settings.oneDriveAutoSync       = nextAuto;
                    st.settings.cmdSaveSettingsToNvs   = true;
                }
            }
            playFeedbackTone(state);
            return true;
        }

        if (event.hitTestLocal(BTN_UNBIND_X, BTN_UNBIND_Y, BTN_UNBIND_W, BTN_UNBIND_H)) {
            _showUnbindConfirm = true;
            playFeedbackTone(state, HapticConfig::TONE_WARN_HZ, HapticConfig::TONE_WARN_MS);
            return true;
        }
        return true;
    }

    if (od.bindState == ONEDRIVE_WAIT_USER_VERIFY || od.userCode[0] != '\0') {
        if (event.hitTestLocal(BTN_CANCEL_AUTH_X, BTN_CANCEL_AUTH_Y, BTN_CANCEL_AUTH_W, BTN_CANCEL_AUTH_H)) {
            {
                StateGuard lock(SystemState::instance());
                if (lock.locked()) {
                    SystemState::instance().data().oneDrive.cmdCancelDeviceCode = true;
                }
            }
            playFeedbackTone(state);
            return true;
        }
        return true;
    }

    // Unbound view
    if (event.hitTestLocal(BTN_START_AUTH_X, BTN_START_AUTH_Y, BTN_START_AUTH_W, BTN_START_AUTH_H)) {
        {
            StateGuard lock(SystemState::instance());
            if (lock.locked()) {
                SystemState::instance().data().oneDrive.cmdStartDeviceCode = true;
            }
        }
        playFeedbackTone(state, HapticConfig::TONE_CONFIRM_1_HZ, HapticConfig::TONE_CONFIRM_MS);
        return true;
    }

    return true;
}
