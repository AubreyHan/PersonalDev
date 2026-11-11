#include "ToolTfManager.h"
#include <stdio.h>
#include <string.h>
#include <type_traits>

static_assert(!std::is_abstract<ToolTfManager>::value && std::is_default_constructible<ToolTfManager>::value,
              "ToolTfManager must be concrete and default-constructible");

namespace {

const char* fsTypeLabel(StorageFsType fs, bool mounted, bool incompatible) {
    if (incompatible) return "格式不兼容 (需格式化 FAT32)";
    if (!mounted)     return "未检测到 TF 卡";
    switch (fs) {
        case STORAGE_FS_FAT32:       return "FAT32 标准车规文件系统";
        case STORAGE_FS_FAT16:       return "FAT16 文件系统";
        case STORAGE_FS_EXFAT:       return "exFAT (建议转 FAT32)";
        case STORAGE_FS_UNSUPPORTED: return "未识别分区 (需格式化)";
        default:                     return "已挂载";
    }
}

void formatBytesHuman(uint64_t bytes, char* out, size_t outLen) {
    const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (gb >= 1.0) {
        snprintf(out, outLen, "%.2f GB", gb);
    } else if (mb >= 0.1) {
        snprintf(out, outLen, "%.1f MB", mb);
    } else {
        snprintf(out, outLen, "%llu KB", static_cast<unsigned long long>(bytes / 1024ULL));
    }
}

} // namespace

void ToolTfManager::onEnter(const SystemStateSnapshot& state) {
    _showFormatConfirm = false;
    _startedShareHere  = false;
    _showShareModal    = (state.network.softApActive && state.network.isShareHotspot);
}

void ToolTfManager::onExit() {
    _showFormatConfirm = false;
    _showShareModal    = false;
    if (_startedShareHere) {
        StateGuard lock(SystemState::instance());
        if (lock.locked()) {
            auto& net = SystemState::instance().data().network;
            if (net.softApActive && net.isShareHotspot) {
                net.cmdStopAp = true;
            }
        }
        _startedShareHere = false;
    }
}

void ToolTfManager::render(M5Canvas& canvas, const SystemStateSnapshot& state) {
    clearViewportCanvas(canvas);

    drawTopHeader(canvas, state);
    drawCapacityAndQuotaCard(canvas, state);
    drawTriFolderCards(canvas, state);
    drawBottomActionButtons(canvas, state);

    const bool shareActive = _showShareModal || (state.network.softApActive && state.network.isShareHotspot);
    if (shareActive) {
        drawShareQrModal(canvas, state);
    } else if (_showFormatConfirm || state.storage.isFormatting) {
        drawFormatConfirmModal(canvas, state);
    }
}

void ToolTfManager::drawTopHeader(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    canvas.fillRoundRect(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H, 10, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H, 10, ThemeColors::ICE_CYAN);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.setTextDatum(middle_center);
    canvas.drawString("< 工具箱", BTN_BACK_X + BTN_BACK_W / 2, BTN_BACK_Y + BTN_BACK_H / 2);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(middle_left);
    canvas.drawString("TF 卡配额与无线极速导出", 224, 40);

    // Auto-record toggle button
    const bool autoRec = state.storage.autoRecordEnabled;
    canvas.fillRoundRect(BTN_AUTOREC_X, BTN_AUTOREC_Y, BTN_AUTOREC_W, BTN_AUTOREC_H, 10,
                         autoRec ? 0x0E2A20U : ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(BTN_AUTOREC_X, BTN_AUTOREC_Y, BTN_AUTOREC_W, BTN_AUTOREC_H, 10,
                         autoRec ? ThemeColors::NEON_GREEN : ThemeColors::BORDER_SUBTLE);
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextColor(autoRec ? ThemeColors::NEON_GREEN : ThemeColors::TEXT_MUTED);
    canvas.setTextDatum(middle_center);
    canvas.drawString(autoRec ? "自动录轨: 已开启" : "自动录轨: 已暂停",
                      BTN_AUTOREC_X + BTN_AUTOREC_W / 2, BTN_AUTOREC_Y + BTN_AUTOREC_H / 2);

    // Refresh storage stats button
    canvas.fillRoundRect(BTN_REFRESH_X, BTN_REFRESH_Y, BTN_REFRESH_W, BTN_REFRESH_H, 10, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(BTN_REFRESH_X, BTN_REFRESH_Y, BTN_REFRESH_W, BTN_REFRESH_H, 10, ThemeColors::BORDER_SUBTLE);
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(middle_center);
    canvas.drawString("刷新容量统计", BTN_REFRESH_X + BTN_REFRESH_W / 2, BTN_REFRESH_Y + BTN_REFRESH_H / 2);
}

void ToolTfManager::drawCapacityAndQuotaCard(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    const StorageData& st = state.storage;
    const int16_t cx = 18;
    const int16_t cy = 80;
    const int16_t cw = 1084;
    const int16_t ch = 224;

    canvas.fillRoundRect(cx, cy, cw, ch, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(cx, cy, cw, ch, 14, ThemeColors::BORDER_SUBTLE);

    // FS status pill
    const uint32_t fsCol = st.formatIncompatible ? ThemeColors::ALERT_RED :
                           (st.isMounted ? ThemeColors::NEON_GREEN : ThemeColors::AMBER_WARN);
    canvas.fillCircle(cx + 28, cy + 28, 6, fsCol);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(fsCol);
    canvas.setTextDatum(middle_left);
    canvas.drawString(fsTypeLabel(st.fsType, st.isMounted, st.formatIncompatible), cx + 44, cy + 28);

    char totalStr[32], usedStr[32], trackStr[32];
    formatBytesHuman(st.totalBytes, totalStr, sizeof(totalStr));
    formatBytesHuman(st.usedBytes, usedStr, sizeof(usedStr));
    formatBytesHuman(st.tracksDirBytes, trackStr, sizeof(trackStr));

    char capSummary[128];
    snprintf(capSummary, sizeof(capSummary), "总容量 %s  |  全卡已用 %s  |  /tracks 轨迹占用 %s",
             totalStr, usedStr, trackStr);
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(middle_right);
    canvas.drawString(capSummary, cx + cw - 24, cy + 28);

    // 50% Track Storage Quota Water-Level Bar (Full Card 0..100% scale with highlighted 40% & 50% markers)
    const int16_t barX = cx + 24;
    const int16_t barY = cy + 68;
    const int16_t barW = cw - 48; // 1036px
    const int16_t barH = 40;

    canvas.fillRoundRect(barX, barY, barW, barH, 8, 0x16202CU);

    // Shade forbidden >50% zone for /tracks/
    const int16_t halfW = barW / 2;
    canvas.fillRect(barX + halfW, barY + 2, halfW - 4, barH - 4, 0x1E1216U);

    float totalUsedRatio = (st.totalBytes > 0ULL)
                               ? static_cast<float>(static_cast<double>(st.usedBytes) / static_cast<double>(st.totalBytes))
                               : 0.0f;
    if (totalUsedRatio < 0.0f) totalUsedRatio = 0.0f;
    if (totalUsedRatio > 1.0f) totalUsedRatio = 1.0f;

    float trackRatio = st.tracksQuotaRatio;
    if (trackRatio <= 0.0f && st.totalBytes > 0ULL) {
        trackRatio = static_cast<float>(static_cast<double>(st.tracksDirBytes) / static_cast<double>(st.totalBytes));
    }
    if (trackRatio < 0.0f) trackRatio = 0.0f;
    if (trackRatio > 1.0f) trackRatio = 1.0f;

    // Non-track other files bar
    const int16_t totalFillW = static_cast<int16_t>(totalUsedRatio * static_cast<float>(barW));
    if (totalFillW > 4) {
        canvas.fillRoundRect(barX, barY, totalFillW, barH, 8, 0x32465AU);
    }

    // /tracks/ quota bar
    const uint32_t quotaColor = (trackRatio >= 0.48f) ? ThemeColors::ALERT_RED :
                                (trackRatio >= 0.38f) ? ThemeColors::AMBER_WARN :
                                                        ThemeColors::NEON_GREEN;
    const int16_t trackFillW = static_cast<int16_t>(trackRatio * static_cast<float>(barW));
    if (trackFillW > 4) {
        canvas.fillRoundRect(barX, barY, trackFillW, barH, 8, quotaColor);
    }

    // 40% FIFO Recovery Line & 50% Hard Quota Cap Line
    const int16_t x40 = barX + static_cast<int16_t>(0.40f * static_cast<float>(barW));
    const int16_t x50 = barX + halfW;
    canvas.drawFastVLine(x40, barY - 6, barH + 12, ThemeColors::AMBER_WARN);
    canvas.drawFastVLine(x40 + 1, barY - 6, barH + 12, ThemeColors::AMBER_WARN);
    canvas.drawFastVLine(x50, barY - 8, barH + 16, ThemeColors::ALERT_RED);
    canvas.drawFastVLine(x50 + 1, barY - 8, barH + 16, ThemeColors::ALERT_RED);
    canvas.drawRoundRect(barX, barY, barW, barH, 8, ThemeColors::BORDER_SUBTLE);

    // Labels under quota bar
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(quotaColor);
    char quotaDetail[96];
    snprintf(quotaDetail, sizeof(quotaDetail),
             "轨迹配额水位: %.1f%% / 50.0%% 上限 (相当于配额池已用 %.0f%%)",
             trackRatio * 100.0f,
             (trackRatio / SdCardConfig::TRACK_STORAGE_QUOTA_RATIO) * 100.0f);
    canvas.drawString(quotaDetail, barX, barY + barH + 14);

    canvas.setTextDatum(top_center);
    canvas.setTextColor(ThemeColors::AMBER_WARN);
    canvas.drawString("40% 自动回收线", x40, barY + barH + 14);

    canvas.setTextColor(ThemeColors::ALERT_RED);
    canvas.drawString("50% 严控红线", x50 + 36, barY + barH + 14);

    // Bottom status row inside card: Recording indicator + Self-healed GPX count
    canvas.drawFastHLine(cx + 24, cy + 164, cw - 48, 0x1B2634U);
    canvas.fillCircle(cx + 34, cy + 192, 6, st.isRecording ? ThemeColors::ALERT_RED : ThemeColors::TEXT_MUTED);
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(st.isRecording ? ThemeColors::TEXT_WHITE : ThemeColors::TEXT_MUTED);
    canvas.setTextDatum(middle_left);
    char recLine[128];
    snprintf(recLine, sizeof(recLine),
             "%s  |  全库双轨总计 %u 对 (WGS84 + GCJ02)  |  开机断电自愈修复 %u 个文件",
             st.isRecording ? "REC 正在实时写入双坐标系 GPX" : "当前静止待机 (车速 > 3km/h 自动落盘)",
             static_cast<unsigned>(st.totalTrackPairs),
             static_cast<unsigned>(st.healedFilesOnBoot));
    canvas.drawString(recLine, cx + 48, cy + 192);
}

void ToolTfManager::drawTriFolderCards(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    const StorageData& st = state.storage;
    const int16_t cardW   = 352;
    const int16_t cardH   = 264;
    const int16_t gap     = 14;
    const int16_t y       = 318;

    struct FolderView {
        const char* dirPath;
        const char* zhTitle;
        const char* ruleNote;
        uint16_t    pairCount;
        const char* activePath;
        uint32_t    accent;
        char        extraLine[64];
    };

    FolderView views[3] = {
        {
            SdCardConfig::DIR_SINGLE,
            "单次行程 /single",
            "每 1 小时 (3600点) 自动无缝切卷",
            st.singlePairCount,
            st.activeSingleWgsPath,
            ThemeColors::NEON_GREEN,
            ""
        },
        {
            SdCardConfig::DIR_DAILY,
            "按天汇总 /daily",
            "00:00~23:59 全天轨迹自动拼接",
            st.dailyPairCount,
            st.activeDailyWgsPath,
            ThemeColors::ICE_CYAN,
            ""
        },
        {
            SdCardConfig::DIR_SUB,
            "手动小记 /sub",
            "跨日累计 · 满 10 MiB 强制分卷",
            st.subPairCount,
            st.activeSubWgsPath,
            ThemeColors::RACING_YELLOW,
            ""
        }
    };

    snprintf(views[0].extraLine, sizeof(views[0].extraLine), "当前卷点数: %u / %u pts (卷 #%u)",
             static_cast<unsigned>(st.singlePointsInFile),
             static_cast<unsigned>(SdCardConfig::HOURLY_SPLIT_POINTS),
             static_cast<unsigned>(st.singleSplitSeq));
    snprintf(views[1].extraLine, sizeof(views[1].extraLine), "双轨同步输出: W-Day-*.gpx + C-Day-*.gpx");
    snprintf(views[2].extraLine, sizeof(views[2].extraLine), "当前分卷体积: %.2f / 10.00 MiB",
             static_cast<float>(st.subCurrentBytes) / (1024.0f * 1024.0f));

    for (int i = 0; i < 3; ++i) {
        const int16_t x = 18 + i * (cardW + gap);
        const FolderView& fv = views[i];

        canvas.fillRoundRect(x, y, cardW, cardH, 14, ThemeColors::BG_CARD_DARK);
        canvas.drawRoundRect(x, y, cardW, cardH, 14, ThemeColors::BORDER_SUBTLE);
        canvas.fillRoundRect(x + 12, y + 16, 6, 44, 3, fv.accent);

        canvas.setFont(&fonts::efontCN_24_b);
        canvas.setTextColor(ThemeColors::TEXT_WHITE);
        canvas.setTextDatum(top_left);
        canvas.drawString(fv.zhTitle, x + 28, y + 14);

        canvas.setFont(&fonts::efontCN_14);
        canvas.setTextColor(ThemeColors::TEXT_MUTED);
        canvas.drawString(fv.ruleNote, x + 28, y + 44);

        // Large pair count
        canvas.setFont(&fonts::DejaVu56);
        canvas.setTextColor(fv.accent);
        canvas.setTextDatum(bottom_left);
        char cntBuf[16];
        snprintf(cntBuf, sizeof(cntBuf), "%u", static_cast<unsigned>(fv.pairCount));
        canvas.drawString(cntBuf, x + 24, y + 146);
        const int32_t numW = canvas.textWidth(cntBuf);

        canvas.setFont(&fonts::efontCN_24);
        canvas.setTextColor(ThemeColors::TEXT_MUTED);
        canvas.drawString("对 W/C 轨迹", x + 32 + static_cast<int16_t>(numW), y + 140);

        canvas.drawFastHLine(x + 18, y + 164, cardW - 36, 0x1B2634U);

        canvas.setFont(&fonts::efontCN_14);
        canvas.setTextColor(ThemeColors::TEXT_WHITE);
        canvas.setTextDatum(top_left);
        char actBuf[72];
        if (fv.activePath != nullptr && fv.activePath[0] != '\0') {
            snprintf(actBuf, sizeof(actBuf), "活跃流: %s", fv.activePath);
        } else {
            snprintf(actBuf, sizeof(actBuf), "活跃流: 当前无打开句柄");
        }
        canvas.drawString(actBuf, x + 18, y + 178);

        canvas.setTextColor(ThemeColors::TEXT_MUTED);
        canvas.drawString(fv.extraLine, x + 18, y + 208);

        canvas.setTextColor(fv.accent);
        canvas.drawString(fv.dirPath, x + 18, y + 234);
    }
}

void ToolTfManager::drawBottomActionButtons(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    const bool shareOn = state.network.softApActive && state.network.isShareHotspot;

    // 1. M5Track-Share Wireless Export Button
    canvas.fillRoundRect(BTN_SHARE_X, BTN_SHARE_Y, BTN_SHARE_W, BTN_SHARE_H, 14,
                         shareOn ? 0x12362AU : 0x0F2636U);
    canvas.drawRoundRect(BTN_SHARE_X, BTN_SHARE_Y, BTN_SHARE_W, BTN_SHARE_H, 14,
                         shareOn ? ThemeColors::NEON_GREEN : ThemeColors::ICE_CYAN);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(shareOn ? ThemeColors::NEON_GREEN : ThemeColors::ICE_CYAN);
    canvas.setTextDatum(middle_center);
    canvas.drawString(shareOn ? "查看 M5Track-Share 导出双二维码 (热点运行中)"
                              : "一键开启 M5Track-Share 无线极速导出热点",
                      BTN_SHARE_X + BTN_SHARE_W / 2, BTN_SHARE_Y + 34);

    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString("手机免流量直连热点 · 扫码打开网页批量下载 WGS84 / GCJ02 轨迹与 CSV 报表",
                      BTN_SHARE_X + BTN_SHARE_W / 2, BTN_SHARE_Y + 70);

    // 2. FAT32 Format Button
    canvas.fillRoundRect(BTN_FORMAT_X, BTN_FORMAT_Y, BTN_FORMAT_W, BTN_FORMAT_H, 14, 0x2A1014U);
    canvas.drawRoundRect(BTN_FORMAT_X, BTN_FORMAT_Y, BTN_FORMAT_W, BTN_FORMAT_H, 14, ThemeColors::ALERT_RED);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::ALERT_RED);
    canvas.setTextDatum(middle_center);
    canvas.drawString("机内格式化为 FAT32", BTN_FORMAT_X + BTN_FORMAT_W / 2, BTN_FORMAT_Y + 34);

    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("带二次防误触确认 · 自动建目录", BTN_FORMAT_X + BTN_FORMAT_W / 2, BTN_FORMAT_Y + 70);
}

void ToolTfManager::drawShareQrModal(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    const int16_t mx = 50;
    const int16_t my = 68;
    const int16_t mw = 1020;
    const int16_t mh = 616;

    canvas.fillRoundRect(mx, my, mw, mh, 18, 0x0A1118U);
    canvas.drawRoundRect(mx, my, mw, mh, 18, ThemeColors::NEON_GREEN);
    canvas.drawRoundRect(mx + 1, my + 1, mw - 2, mh - 2, 17, ThemeColors::NEON_GREEN);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.setTextDatum(top_center);
    canvas.drawString("M5Track-Share 无线极速轨迹导出门户", mx + mw / 2, my + 18);

    char subStatus[128];
    if (state.network.softApActive && state.network.isShareHotspot) {
        snprintf(subStatus, sizeof(subStatus),
                 "热点已就绪 · SSID: %s (免密) · 网关: %s · 已接入手机: %u 台",
                 state.network.apSsid, state.network.apIp, static_cast<unsigned>(state.network.apStationCount));
    } else {
        snprintf(subStatus, sizeof(subStatus),
                 "正在启动 ESP32-C6 SoftAP (%s) ... 请按顺序扫描下方两个二维码",
                 NetConfig::AP_SHARE_SSID);
    }
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString(subStatus, mx + mw / 2, my + 54);

    const char* wifiQr = (state.network.isShareHotspot && state.network.qrWifiConnect[0] != '\0')
                             ? state.network.qrWifiConnect
                             : NetConfig::QR_SHARE_WIFI_PAYLOAD;
    const char* urlQr  = (state.network.isShareHotspot && state.network.qrWebPortalUrl[0] != '\0')
                             ? state.network.qrWebPortalUrl
                             : NetConfig::QR_SHARE_URL_PAYLOAD;

    // Step 1 Card: Connect Wi-Fi Hotspot QR
    const int16_t c1X = mx + 50;
    const int16_t cY  = my + 92;
    const int16_t cW  = 430;
    const int16_t cH  = 412;
    canvas.fillRoundRect(c1X, cY, cW, cH, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(c1X, cY, cW, cH, 14, ThemeColors::ICE_CYAN);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.setTextDatum(top_center);
    canvas.drawString("第 1 步：扫码连接车载热点", c1X + cW / 2, cY + 16);

    const int16_t qrSize = 230;
    const int16_t q1X    = c1X + (cW - qrSize) / 2;
    const int16_t qY     = cY + 60;
    canvas.fillRoundRect(q1X - 12, qY - 12, qrSize + 24, qrSize + 24, 10, TFT_WHITE);
    canvas.qrcode(wifiQr, q1X, qY, qrSize, 4);

    canvas.setFont(&fonts::DejaVu18);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(top_center);
    canvas.drawString(NetConfig::AP_SHARE_SSID, c1X + cW / 2, qY + qrSize + 22);
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("开放式局域网导出热点 (无需密码)", c1X + cW / 2, qY + qrSize + 48);

    // Step 2 Card: Open Track Download Portal QR
    const int16_t c2X = mx + mw - cW - 50;
    canvas.fillRoundRect(c2X, cY, cW, cH, 14, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(c2X, cY, cW, cH, 14, ThemeColors::NEON_GREEN);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.setTextDatum(top_center);
    canvas.drawString("第 2 步：扫码打开轨迹下载页", c2X + cW / 2, cY + 16);

    const int16_t q2X = c2X + (cW - qrSize) / 2;
    canvas.fillRoundRect(q2X - 12, qY - 12, qrSize + 24, qrSize + 24, 10, TFT_WHITE);
    canvas.qrcode(urlQr, q2X, qY, qrSize, 4);

    canvas.setFont(&fonts::DejaVu18);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(top_center);
    canvas.drawString(urlQr, c2X + cW / 2, qY + qrSize + 22);
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("支持按目录筛选下载 GPX 与 trips.csv", c2X + cW / 2, qY + qrSize + 48);

    // Close & Stop Share Hotspot Button
    canvas.fillRoundRect(BTN_QR_STOP_X, BTN_QR_STOP_Y, BTN_QR_STOP_W, BTN_QR_STOP_H, 12, 0x361418U);
    canvas.drawRoundRect(BTN_QR_STOP_X, BTN_QR_STOP_Y, BTN_QR_STOP_W, BTN_QR_STOP_H, 12, ThemeColors::ALERT_RED);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(middle_center);
    canvas.drawString("关闭导出热点并恢复常规网络", BTN_QR_STOP_X + BTN_QR_STOP_W / 2, BTN_QR_STOP_Y + BTN_QR_STOP_H / 2);
}

void ToolTfManager::drawFormatConfirmModal(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    const int16_t mx = 190;
    const int16_t my = 160;
    const int16_t mw = 740;
    const int16_t mh = 400;

    canvas.fillRoundRect(mx, my, mw, mh, 18, 0x120B0EU);
    canvas.drawRoundRect(mx, my, mw, mh, 18, ThemeColors::ALERT_RED);
    canvas.drawRoundRect(mx + 1, my + 1, mw - 2, mh - 2, 17, ThemeColors::ALERT_RED);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::ALERT_RED);
    canvas.setTextDatum(top_center);

    if (state.storage.isFormatting) {
        canvas.drawString("正在执行机内 FAT32 格式化...", mx + mw / 2, my + 56);
        canvas.setFont(&fonts::efontCN_24);
        canvas.setTextColor(ThemeColors::TEXT_WHITE);
        canvas.drawString("正在重建分区表与 /tracks/{single,daily,sub} 目录", mx + mw / 2, my + 140);
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextColor(ThemeColors::AMBER_WARN);
        canvas.drawString("请勿拔出 TF 卡或断电，完成后将自动恢复挂载", mx + mw / 2, my + 200);
        return;
    }

    canvas.drawString("高危操作确认：格式化 TF 卡为 FAT32", mx + mw / 2, my + 34);

    canvas.setFont(&fonts::efontCN_24);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString("此操作将彻底清除 TF 卡上的所有历史轨迹与文件！", mx + mw / 2, my + 104);

    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString("格式化后系统将自动创建标准 FAT32 文件系统及 single / daily / sub 三级目录", mx + mw / 2, my + 156);
    canvas.setTextColor(ThemeColors::AMBER_WARN);
    canvas.drawString("建议先通过无线热点或 OneDrive 导出重要 GPX 轨迹后再执行", mx + mw / 2, my + 194);

    // Cancel Button
    canvas.fillRoundRect(BTN_FMT_CANCEL_X, BTN_FMT_CANCEL_Y, BTN_FMT_CANCEL_W, BTN_FMT_CANCEL_H, 12, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(BTN_FMT_CANCEL_X, BTN_FMT_CANCEL_Y, BTN_FMT_CANCEL_W, BTN_FMT_CANCEL_H, 12, ThemeColors::ICE_CYAN);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.setTextDatum(middle_center);
    canvas.drawString("取消返回", BTN_FMT_CANCEL_X + BTN_FMT_CANCEL_W / 2, BTN_FMT_CANCEL_Y + BTN_FMT_CANCEL_H / 2);

    // Confirm Format Button
    canvas.fillRoundRect(BTN_FMT_CONFIRM_X, BTN_FMT_CONFIRM_Y, BTN_FMT_CONFIRM_W, BTN_FMT_CONFIRM_H, 12, ThemeColors::ALERT_RED);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextDatum(middle_center);
    canvas.drawString("确认格式化 FAT32", BTN_FMT_CONFIRM_X + BTN_FMT_CONFIRM_W / 2, BTN_FMT_CONFIRM_Y + BTN_FMT_CONFIRM_H / 2);
}

bool ToolTfManager::onTouch(const TouchEvent& event, const SystemStateSnapshot& state) {
    if (event.type != TOUCH_EVT_TAP) {
        return true;
    }

    const bool shareModalOpen = _showShareModal || (state.network.softApActive && state.network.isShareHotspot);
    if (shareModalOpen) {
        if (event.hitTestLocal(BTN_QR_STOP_X, BTN_QR_STOP_Y, BTN_QR_STOP_W, BTN_QR_STOP_H)) {
            _showShareModal   = false;
            _startedShareHere = false;
            {
                StateGuard lock(SystemState::instance());
                if (lock.locked()) {
                    SystemState::instance().data().network.cmdStopAp = true;
                }
            }
            playFeedbackTone(state);
        }
        return true;
    }

    if (state.storage.isFormatting) {
        return true;
    }

    if (_showFormatConfirm) {
        if (event.hitTestLocal(BTN_FMT_CANCEL_X, BTN_FMT_CANCEL_Y, BTN_FMT_CANCEL_W, BTN_FMT_CANCEL_H)) {
            _showFormatConfirm = false;
            playFeedbackTone(state);
            return true;
        }
        if (event.hitTestLocal(BTN_FMT_CONFIRM_X, BTN_FMT_CONFIRM_Y, BTN_FMT_CONFIRM_W, BTN_FMT_CONFIRM_H)) {
            _showFormatConfirm = false;
            {
                StateGuard lock(SystemState::instance());
                if (lock.locked()) {
                    SystemState::instance().data().storage.cmdFormatFat32 = true;
                }
            }
            playFeedbackTone(state, HapticConfig::TONE_WARN_HZ, HapticConfig::TONE_WARN_MS);
            return true;
        }
        return true;
    }

    // Standard page buttons
    if (event.hitTestLocal(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H)) {
        playFeedbackTone(state);
        requestNavigate(PAGE_TOOLS_GRID);
        return true;
    }

    if (event.hitTestLocal(BTN_AUTOREC_X, BTN_AUTOREC_Y, BTN_AUTOREC_W, BTN_AUTOREC_H)) {
        {
            StateGuard lock(SystemState::instance());
            if (lock.locked()) {
                auto& st                      = SystemState::instance().data();
                const bool nextVal            = !st.storage.autoRecordEnabled;
                st.storage.autoRecordEnabled  = nextVal;
                st.settings.autoRecordOnMove  = nextVal;
                st.settings.cmdSaveSettingsToNvs = true;
            }
        }
        playFeedbackTone(state);
        return true;
    }

    if (event.hitTestLocal(BTN_REFRESH_X, BTN_REFRESH_Y, BTN_REFRESH_W, BTN_REFRESH_H)) {
        {
            StateGuard lock(SystemState::instance());
            if (lock.locked()) {
                SystemState::instance().data().storage.cmdRefreshStorage = true;
            }
        }
        playFeedbackTone(state);
        return true;
    }

    if (event.hitTestLocal(BTN_SHARE_X, BTN_SHARE_Y, BTN_SHARE_W, BTN_SHARE_H)) {
        _showShareModal   = true;
        _startedShareHere = true;
        {
            StateGuard lock(SystemState::instance());
            if (lock.locked()) {
                SystemState::instance().data().network.cmdStartShareAp = true;
            }
        }
        playFeedbackTone(state, HapticConfig::TONE_CONFIRM_1_HZ, HapticConfig::TONE_CONFIRM_MS);
        return true;
    }

    if (event.hitTestLocal(BTN_FORMAT_X, BTN_FORMAT_Y, BTN_FORMAT_W, BTN_FORMAT_H)) {
        _showFormatConfirm = true;
        playFeedbackTone(state, HapticConfig::TONE_WARN_HZ, HapticConfig::TONE_WARN_MS);
        return true;
    }

    return true;
}
