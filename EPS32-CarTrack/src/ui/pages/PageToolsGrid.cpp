#include "PageToolsGrid.h"
#include <stdio.h>
#include <type_traits>

static_assert(!std::is_abstract<PageToolsGrid>::value && std::is_default_constructible<PageToolsGrid>::value,
              "PageToolsGrid must be concrete and default-constructible");

namespace {

PageId targetSubpageForTool(uint8_t toolIndex) {
    switch (toolIndex) {
        case 0:  return SUBPAGE_TOOL_TF_MANAGER;
        case 1:  return SUBPAGE_TOOL_WIFI;
        case 2:  return SUBPAGE_TOOL_GNSS;
        case 3:  return SUBPAGE_TOOL_ONEDRIVE;
        case 4:  return SUBPAGE_TOOL_SETTINGS;
        default: return PAGE_NONE;
    }
}

uint32_t accentForTool(uint8_t toolIndex, const SystemStateSnapshot& state) {
    switch (toolIndex) {
        case 0: return state.storage.isMounted ? ThemeColors::NEON_GREEN : ThemeColors::AMBER_WARN;
        case 1: return state.network.staConnected ? ThemeColors::ICE_CYAN : ThemeColors::RACING_YELLOW;
        case 2: return state.gnss.isValid ? ThemeColors::NEON_GREEN : ThemeColors::ICE_CYAN;
        case 3: return state.oneDrive.isBound ? ThemeColors::ICE_CYAN : ThemeColors::AMBER_WARN;
        case 4: return state.power.isExternalPower ? ThemeColors::POWER_EXT_GREEN : ThemeColors::POWER_BATT_ONLY_RED;
        default: return ThemeColors::BORDER_SUBTLE;
    }
}

} // namespace

void PageToolsGrid::onEnter(const SystemStateSnapshot& state) {
    (void)state;
    _pressedSlot = -1;
    _prevPressed = false;
    _nextPressed = false;
}

void PageToolsGrid::getSlotRect(uint8_t slotOnPage, int16_t& x, int16_t& y) {
    x = ((slotOnPage & 1U) == 0U) ? COL_0_X : COL_1_X;
    y = (slotOnPage < 2U) ? ROW_0_Y : ROW_1_Y;
}

void PageToolsGrid::render(M5Canvas& canvas, const SystemStateSnapshot& state) {
    clearViewportCanvas(canvas);
    drawHeaderAndPager(canvas);

    const uint8_t baseIndex = static_cast<uint8_t>(_gridPage * CARDS_PER_PAGE);
    for (uint8_t slot = 0; slot < CARDS_PER_PAGE; ++slot) {
        const uint8_t toolIndex = static_cast<uint8_t>(baseIndex + slot);
        if (toolIndex < TOTAL_TOOLS) {
            drawToolCard(canvas, toolIndex, slot, state);
        } else {
            drawExpansionSlot(canvas, slot, toolIndex);
        }
    }
}

void PageToolsGrid::drawHeaderAndPager(M5Canvas& canvas) const {
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(top_left);
    canvas.drawString("系统工具箱", 20, 14);

    char subBuf[96];
    snprintf(subBuf, sizeof(subBuf), "2x2 车规盲操网格 · 第 %u / %u 页 (共 %u 项核心工具)",
             static_cast<unsigned>(_gridPage + 1U),
             static_cast<unsigned>(TOTAL_PAGES),
             static_cast<unsigned>(TOTAL_TOOLS));
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString(subBuf, 20, 48);

    // Page indicator pills in middle
    for (uint8_t p = 0; p < TOTAL_PAGES; ++p) {
        const int16_t px  = 536 + static_cast<int16_t>(p) * 44;
        const bool active = (_gridPage == p);
        canvas.fillRoundRect(px, 32, active ? 34 : 20, 22, 11, active ? ThemeColors::NEON_GREEN : 0x223040U);
    }

    // Large Touch Pager Buttons (>4 items automatic pagination)
    const bool canPrev = (_gridPage > 0U);
    const bool canNext = (_gridPage + 1U < TOTAL_PAGES);

    const uint32_t prevBg = _prevPressed ? ThemeColors::BG_CARD_HIGHLIGHT : ThemeColors::BG_CARD_DARK;
    canvas.fillRoundRect(BTN_PREV_X, BTN_PREV_Y, BTN_PREV_W, BTN_PREV_H, 12, prevBg);
    canvas.drawRoundRect(BTN_PREV_X, BTN_PREV_Y, BTN_PREV_W, BTN_PREV_H, 12,
                         canPrev ? ThemeColors::ICE_CYAN : ThemeColors::BORDER_SUBTLE);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(canPrev ? ThemeColors::ICE_CYAN : ThemeColors::TEXT_MUTED);
    canvas.setTextDatum(middle_center);
    canvas.drawString("< 上一页 (1-4)", BTN_PREV_X + BTN_PREV_W / 2, BTN_PREV_Y + BTN_PREV_H / 2);

    const uint32_t nextBg = _nextPressed ? ThemeColors::BG_CARD_HIGHLIGHT : (canNext ? 0x102822U : ThemeColors::BG_CARD_DARK);
    canvas.fillRoundRect(BTN_NEXT_X, BTN_NEXT_Y, BTN_NEXT_W, BTN_NEXT_H, 12, nextBg);
    canvas.drawRoundRect(BTN_NEXT_X, BTN_NEXT_Y, BTN_NEXT_W, BTN_NEXT_H, 12,
                         canNext ? ThemeColors::NEON_GREEN : ThemeColors::BORDER_SUBTLE);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(canNext ? ThemeColors::NEON_GREEN : ThemeColors::TEXT_MUTED);
    canvas.setTextDatum(middle_center);
    canvas.drawString("下一页 (5/5) >", BTN_NEXT_X + BTN_NEXT_W / 2, BTN_NEXT_Y + BTN_NEXT_H / 2);
}

void PageToolsGrid::drawToolVectorIcon(M5Canvas& canvas, uint8_t toolIndex, int16_t cx, int16_t cy, uint32_t accent) {
    canvas.fillRoundRect(cx - 38, cy - 38, 76, 76, 16, 0x13202EU);
    canvas.drawRoundRect(cx - 38, cy - 38, 76, 76, 16, accent);

    switch (toolIndex) {
        case 0: {
            // MicroSD Card + Wireless Export Wave
            canvas.drawRoundRect(cx - 18, cy - 24, 36, 48, 5, accent);
            canvas.drawLine(cx + 6, cy - 24, cx + 18, cy - 12, accent);
            for (int i = 0; i < 4; ++i) {
                canvas.fillRect(cx - 11 + i * 6, cy - 18, 3, 9, ThemeColors::RACING_YELLOW);
            }
            canvas.drawArc(cx, cy + 12, 12, 10, 220, 320, ThemeColors::ICE_CYAN);
            canvas.fillCircle(cx, cy + 14, 3, ThemeColors::ICE_CYAN);
            break;
        }
        case 1: {
            // Wi-Fi 6 Concentric Signal Arcs
            canvas.fillCircle(cx, cy + 18, 5, accent);
            canvas.drawArc(cx, cy + 18, 15, 12, 225, 315, accent);
            canvas.drawArc(cx, cy + 18, 26, 23, 225, 315, accent);
            canvas.drawArc(cx, cy + 18, 37, 34, 225, 315, ThemeColors::ICE_CYAN);
            break;
        }
        case 2: {
            // GNSS Skyplot Crosshair + Orbiting Sat
            canvas.drawCircle(cx, cy, 24, accent);
            canvas.drawCircle(cx, cy, 11, ThemeColors::ICE_CYAN);
            canvas.drawFastHLine(cx - 28, cy, 56, accent);
            canvas.drawFastVLine(cx, cy - 28, 56, accent);
            canvas.fillCircle(cx + 14, cy - 14, 5, ThemeColors::RACING_YELLOW);
            break;
        }
        case 3: {
            // OneDrive Cloud + Upload Arrow
            canvas.fillCircle(cx - 10, cy + 4, 13, accent);
            canvas.fillCircle(cx + 4, cy - 3, 16, accent);
            canvas.fillCircle(cx + 17, cy + 6, 11, accent);
            canvas.fillRect(cx - 12, cy + 5, 30, 12, accent);
            canvas.fillTriangle(cx, cy - 6, cx - 8, cy + 4, cx + 8, cy + 4, TFT_BLACK);
            canvas.fillRect(cx - 3, cy + 4, 6, 10, TFT_BLACK);
            break;
        }
        case 4: {
            // Settings Sliders + INA226 Bolt
            for (int row = -1; row <= 1; ++row) {
                const int16_t sy = cy + row * 15;
                canvas.drawFastHLine(cx - 22, sy, 44, ThemeColors::TEXT_MUTED);
                const int16_t kx = cx + (row == 0 ? 10 : -8);
                canvas.fillCircle(kx, sy, 5, accent);
            }
            break;
        }
        default:
            break;
    }
}

void PageToolsGrid::drawToolCard(M5Canvas& canvas, uint8_t toolIndex, uint8_t slotOnPage, const SystemStateSnapshot& state) const {
    int16_t x = 0;
    int16_t y = 0;
    getSlotRect(slotOnPage, x, y);

    const uint32_t accent    = accentForTool(toolIndex, state);
    const bool isPressed     = (_pressedSlot == static_cast<int8_t>(slotOnPage));
    const uint32_t bgCol     = isPressed ? ThemeColors::BG_CARD_HIGHLIGHT : ThemeColors::BG_CARD_DARK;
    const uint32_t borderCol = isPressed ? accent : ThemeColors::BORDER_SUBTLE;

    canvas.fillRoundRect(x, y, CARD_W, CARD_H, 16, bgCol);
    canvas.drawRoundRect(x, y, CARD_W, CARD_H, 16, borderCol);
    if (isPressed) {
        canvas.drawRoundRect(x + 1, y + 1, CARD_W - 2, CARD_H - 2, 15, accent);
    }

    drawToolVectorIcon(canvas, toolIndex, x + 58, y + 62, accent);

    const char* title       = "";
    const char* subtitle    = "";
    char badgeText[40]      = "";
    char metricLine1[80]    = "";
    char metricLine2[80]    = "";

    switch (toolIndex) {
        case 0: {
            title    = "TF 卡与轨迹导出";
            subtitle = "50% 配额监控 · M5Track-Share 热点 · FAT32 格式化";
            if (!state.storage.isMounted) {
                snprintf(badgeText, sizeof(badgeText), "未挂载");
                snprintf(metricLine1, sizeof(metricLine1), "TF 状态: 请检查卡槽或执行 FAT32 格式化");
            } else {
                const float totalGb = static_cast<float>(state.storage.totalBytes) / (1024.0f * 1024.0f * 1024.0f);
                const float quotaPct = state.storage.tracksQuotaRatio * 100.0f;
                snprintf(badgeText, sizeof(badgeText), "%.1f GB 在线", totalGb);
                snprintf(metricLine1, sizeof(metricLine1), "轨迹配额占用: %.1f%% / 50.0%% 上限", quotaPct);
            }
            snprintf(metricLine2, sizeof(metricLine2), "双轨文件对: 单次 %u · 按天 %u · 小记 %u",
                     static_cast<unsigned>(state.storage.singlePairCount),
                     static_cast<unsigned>(state.storage.dailyPairCount),
                     static_cast<unsigned>(state.storage.subPairCount));
            break;
        }
        case 1: {
            title    = "Wi-Fi 6 网络管理";
            subtitle = "ESP32-C6 漫游槽位管理 · 扫码配网门户";
            if (state.network.staConnected) {
                snprintf(badgeText, sizeof(badgeText), "%d dBm", static_cast<int>(state.network.rssiDbm));
                snprintf(metricLine1, sizeof(metricLine1), "当前连接: %s (%s)",
                         state.network.connectedSsid, state.network.staIp);
            } else if (state.network.softApActive) {
                snprintf(badgeText, sizeof(badgeText), "热点开启");
                snprintf(metricLine1, sizeof(metricLine1), "SoftAP: %s (%s)",
                         state.network.apSsid, state.network.apIp);
            } else {
                snprintf(badgeText, sizeof(badgeText), "离线待机");
                snprintf(metricLine1, sizeof(metricLine1), "当前状态: %s", state.network.statusText);
            }
            snprintf(metricLine2, sizeof(metricLine2), "已存漫游网络: %u / %u 槽位 · NTP %s",
                     static_cast<unsigned>(state.network.savedCount),
                     static_cast<unsigned>(NetConfig::MAX_SAVED_NETWORKS),
                     state.network.ntpSynced ? "已同步" : "待同步");
            break;
        }
        case 2: {
            title    = "GPS 四星与射频诊断";
            subtitle = "极坐标星空图 · C/N0 柱状图 · UBX-MON-RF 天线与干扰";
            snprintf(badgeText, sizeof(badgeText), "%u/%u 颗星",
                     static_cast<unsigned>(state.gnss.numSats),
                     static_cast<unsigned>(state.gnss.satsInView));
            const char* antStr = (state.gnss.antennaStatus == ANT_OK)    ? "OK" :
                                 (state.gnss.antennaStatus == ANT_SHORT) ? "SHORT!" :
                                 (state.gnss.antennaStatus == ANT_OPEN)  ? "OPEN!" : "INIT";
            const char* jamStr = (state.gnss.jammingState == JAMMING_OK)   ? "OK" :
                                 (state.gnss.jammingState == JAMMING_WARN) ? "WARN" :
                                 (state.gnss.jammingState == JAMMING_CRIT) ? "CRIT!" : "UNK";
            snprintf(metricLine1, sizeof(metricLine1), "HDOP %.1f · 天线 %s · 抗干扰 %s",
                     state.gnss.hdop, antStr, jamStr);
            snprintf(metricLine2, sizeof(metricLine2), "四星解算: 北斗 %u · GPS %u · GLO %u · GAL %u",
                     static_cast<unsigned>(state.gnss.numBdsSats),
                     static_cast<unsigned>(state.gnss.numGpsSats),
                     static_cast<unsigned>(state.gnss.numGloSats),
                     static_cast<unsigned>(state.gnss.numGalSats));
            break;
        }
        case 3: {
            title    = "OneDrive 云端同步";
            subtitle = "Device Code 扫码授权 · 320KiB 分片断点续传";
            if (state.oneDrive.isBound) {
                snprintf(badgeText, sizeof(badgeText), "已绑定");
                snprintf(metricLine1, sizeof(metricLine1), "账号: %s",
                         state.oneDrive.accountEmail[0] ? state.oneDrive.accountEmail : "Microsoft 账户已授权");
            } else {
                snprintf(badgeText, sizeof(badgeText), "未绑定");
                snprintf(metricLine1, sizeof(metricLine1), "点击扫码获取 8 位设备验证码绑定云盘");
            }
            snprintf(metricLine2, sizeof(metricLine2), "已同步 %u 个 · 待传 %u 个 · 自动同步 %s",
                     static_cast<unsigned>(state.oneDrive.syncedTotalFiles),
                     static_cast<unsigned>(state.oneDrive.pendingFiles),
                     state.oneDrive.autoSyncEnabled ? "开启" : "关闭");
            break;
        }
        case 4: {
            title    = "系统偏好与库仑计监控";
            subtitle = "背光/提示音/小记控制 · INA226 (0x41, 5mΩ) 复合电量计";
            snprintf(badgeText, sizeof(badgeText), "%.0f%% %s",
                     state.power.socPercent,
                     state.power.isExternalPower ? "外电" : "电池");
            snprintf(metricLine1, sizeof(metricLine1), "INA226: %.2fV · %+.0fmA · %.0fmW",
                     state.power.busVoltageV, state.power.currentMa, state.power.powerMw);
            const uint32_t remSleepSec = (state.power.standstillSeconds < 1200U)
                                             ? (1200U - state.power.standstillSeconds)
                                             : 0U;
            snprintf(metricLine2, sizeof(metricLine2), "背光 %u/255 · 静止休眠倒计时 %02u:%02u",
                     static_cast<unsigned>(state.settings.screenBrightness),
                     static_cast<unsigned>(remSleepSec / 60U),
                     static_cast<unsigned>(remSleepSec % 60U));
            break;
        }
        default:
            break;
    }

    // Top-right live badge
    const int16_t badgeW = 134;
    const int16_t badgeX = x + CARD_W - badgeW - 18;
    const int16_t badgeY = y + 20;
    canvas.fillRoundRect(badgeX, badgeY, badgeW, 34, 8, 0x162432U);
    canvas.drawRoundRect(badgeX, badgeY, badgeW, 34, 8, accent);
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextColor(accent);
    canvas.setTextDatum(middle_center);
    canvas.drawString(badgeText, badgeX + badgeW / 2, badgeY + 17);

    // Title & subtitle next to icon
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(top_left);
    canvas.drawString(title, x + 112, y + 24);

    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString(subtitle, x + 112, y + 62);

    canvas.drawFastHLine(x + 20, y + 118, CARD_W - 40, 0x1B2634U);

    // Live telemetry preview rows
    canvas.setFont(&fonts::efontCN_24);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(top_left);
    canvas.drawString(metricLine1, x + 22, y + 136);

    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString(metricLine2, x + 22, y + 180);

    // Bottom action bar
    canvas.fillRoundRect(x + 20, y + 224, CARD_W - 40, 48, 10, 0x132230U);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(accent);
    canvas.setTextDatum(middle_center);
    canvas.drawString("进入工具详情 >", x + CARD_W / 2, y + 248);
}

void PageToolsGrid::drawExpansionSlot(M5Canvas& canvas, uint8_t slotOnPage, uint8_t virtualIndex) const {
    int16_t x = 0;
    int16_t y = 0;
    getSlotRect(slotOnPage, x, y);

    canvas.fillRoundRect(x, y, CARD_W, CARD_H, 16, 0x070B10U);
    canvas.drawRoundRect(x, y, CARD_W, CARD_H, 16, 0x18222EU);

    char slotBuf[48];
    snprintf(slotBuf, sizeof(slotBuf), "扩展工具槽位 #%u", static_cast<unsigned>(virtualIndex + 1U));
    canvas.setFont(&fonts::efontCN_24);
    canvas.setTextColor(0x3A4858U);
    canvas.setTextDatum(middle_center);
    canvas.drawString(slotBuf, x + CARD_W / 2, y + CARD_H / 2 - 16);

    canvas.setFont(&fonts::efontCN_16);
    canvas.drawString("预留 OTA / OBD / 胎压模块接入位", x + CARD_W / 2, y + CARD_H / 2 + 20);
}

bool PageToolsGrid::onTouch(const TouchEvent& event, const SystemStateSnapshot& state) {
    if (event.type == TOUCH_EVT_DOWN) {
        _prevPressed = event.hitTestLocal(BTN_PREV_X, BTN_PREV_Y, BTN_PREV_W, BTN_PREV_H);
        _nextPressed = event.hitTestLocal(BTN_NEXT_X, BTN_NEXT_Y, BTN_NEXT_W, BTN_NEXT_H);
        _pressedSlot = -1;

        const uint8_t baseIndex = static_cast<uint8_t>(_gridPage * CARDS_PER_PAGE);
        for (uint8_t slot = 0; slot < CARDS_PER_PAGE; ++slot) {
            if (baseIndex + slot >= TOTAL_TOOLS) {
                continue;
            }
            int16_t cx = 0;
            int16_t cy = 0;
            getSlotRect(slot, cx, cy);
            if (event.hitTestLocal(cx, cy, CARD_W, CARD_H)) {
                _pressedSlot = static_cast<int8_t>(slot);
                break;
            }
        }
        return (_prevPressed || _nextPressed);
    }

    if (event.type == TOUCH_EVT_MOVE) {
        if (abs(event.localX - event.startLocalX) > GestureConfig::TAP_MAX_JITTER_PX ||
            abs(event.localY - event.startLocalY) > GestureConfig::TAP_MAX_JITTER_PX) {
            _pressedSlot = -1;
            _prevPressed = false;
            _nextPressed = false;
        }
        return false;
    }

    if (event.type == TOUCH_EVT_UP) {
        _pressedSlot = -1;
        _prevPressed = false;
        _nextPressed = false;
        return false;
    }

    if (event.type == TOUCH_EVT_TAP) {
        _pressedSlot = -1;
        _prevPressed = false;
        _nextPressed = false;

        if (event.hitTestLocal(BTN_PREV_X, BTN_PREV_Y, BTN_PREV_W, BTN_PREV_H)) {
            if (_gridPage > 0U) {
                _gridPage--;
                playFeedbackTone(state);
            }
            return true;
        }

        if (event.hitTestLocal(BTN_NEXT_X, BTN_NEXT_Y, BTN_NEXT_W, BTN_NEXT_H)) {
            if (_gridPage + 1U < TOTAL_PAGES) {
                _gridPage++;
                playFeedbackTone(state);
            }
            return true;
        }

        const uint8_t baseIndex = static_cast<uint8_t>(_gridPage * CARDS_PER_PAGE);
        for (uint8_t slot = 0; slot < CARDS_PER_PAGE; ++slot) {
            const uint8_t toolIndex = static_cast<uint8_t>(baseIndex + slot);
            if (toolIndex >= TOTAL_TOOLS) {
                continue;
            }
            int16_t cx = 0;
            int16_t cy = 0;
            getSlotRect(slot, cx, cy);
            if (event.hitTestLocal(cx, cy, CARD_W, CARD_H)) {
                const PageId target = targetSubpageForTool(toolIndex);
                if (target != PAGE_NONE) {
                    playFeedbackTone(state);
                    requestNavigate(target);
                }
                return true;
            }
        }
    }

    return false;
}
