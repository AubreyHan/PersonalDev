#include "Page7WorldClock.h"
#include <stdio.h>
#include <string.h>
#include <type_traits>

static_assert(!std::is_abstract<Page7WorldClock>::value && std::is_default_constructible<Page7WorldClock>::value,
              "Page7WorldClock must be concrete and default-constructible");

namespace {

struct CityLabelMap {
    const char* enName;
    const char* zhName;
};

constexpr CityLabelMap kCityLabels[] = {
    { "GPS Dynamic", "GPS 动态经度时区" },
    { "GPS Local",   "GPS 动态经度时区" },
    { "Beijing",     "北京" },
    { "Urumqi",      "乌鲁木齐" },
    { "UTC",         "协调世界时" },
    { "Tokyo",       "东京" },
    { "Dubai",       "迪拜" },
    { "London",      "伦敦" },
    { "Paris",       "巴黎" },
    { "Moscow",      "莫斯科" },
    { "New York",    "纽约" },
    { "Los Angeles", "洛杉矶" },
    { "Sydney",      "悉尼" },
    { "Bangkok",     "曼谷" },
    { "Singapore",   "新加坡" },
    { "Almaty",      "阿拉木图" },
    { "Cairo",       "开罗" },
    { "Frankfurt",   "法兰克福" },
    { "Vancouver",   "温哥华" },
    { "Chicago",     "芝加哥" },
    { "Anchorage",   "安克雷奇" },
    { "Auckland",    "奥克兰" }
};

const char* lookupZhCityName(const char* enName) {
    if (enName == nullptr || enName[0] == '\0') {
        return "自定义时区";
    }
    for (size_t i = 0; i < sizeof(kCityLabels) / sizeof(kCityLabels[0]); ++i) {
        if (strcasecmp(enName, kCityLabels[i].enName) == 0) {
            return kCityLabels[i].zhName;
        }
    }
    return enName;
}

const char* weekdayZh(uint8_t wday) {
    static const char* kDays[7] = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };
    return kDays[wday % 7U];
}

void formatUtcOffset(int16_t offsetMin, char* buf, size_t bufLen) {
    const char sign = (offsetMin >= 0) ? '+' : '-';
    const int total = (offsetMin >= 0) ? offsetMin : -offsetMin;
    const int hh    = total / 60;
    const int mm    = total % 60;
    if (mm == 0) {
        snprintf(buf, bufLen, "UTC%c%d", sign, hh);
    } else {
        snprintf(buf, bufLen, "UTC%c%d:%02d", sign, hh, mm);
    }
}

uint32_t slotAccentColor(uint8_t slot) {
    switch (slot & 0x03U) {
        case 0:  return ThemeColors::NEON_GREEN;
        case 1:  return ThemeColors::ICE_CYAN;
        case 2:  return ThemeColors::AMBER_WARN;
        default: return ThemeColors::TEXT_WHITE;
    }
}

} // namespace

void Page7WorldClock::onEnter(const SystemStateSnapshot& state) {
    (void)state;
    _pressedSlot = -1;
}

void Page7WorldClock::getCardRect(uint8_t slot, int16_t& x, int16_t& y) {
    x = ((slot & 1U) == 0U) ? COL_0_X : COL_1_X;
    y = (slot < 2U) ? ROW_0_Y : ROW_1_Y;
}

void Page7WorldClock::render(M5Canvas& canvas, const SystemStateSnapshot& state) {
    clearViewportCanvas(canvas);
    drawTopSyncBar(canvas, state);
    for (uint8_t slot = 0; slot < 4U; ++slot) {
        drawClockCard(canvas, slot, state);
    }
}

void Page7WorldClock::drawTopSyncBar(M5Canvas& canvas, const SystemStateSnapshot& state) const {
    const TimeData& tm = state.time;

    const char* srcLabel = "未校准 (等待卫星/NTP)";
    uint32_t srcColor    = ThemeColors::AMBER_WARN;
    switch (tm.source) {
        case CLOCK_SRC_GNSS:
            srcLabel = "授时基准: NEO-M9N 卫星原子钟";
            srcColor = ThemeColors::NEON_GREEN;
            break;
        case CLOCK_SRC_NTP:
            srcLabel = "授时基准: Wi-Fi 多源 NTP";
            srcColor = ThemeColors::ICE_CYAN;
            break;
        case CLOCK_SRC_RTC:
            srcLabel = "授时基准: RX8130CE 硬件 RTC";
            srcColor = ThemeColors::RACING_YELLOW;
            break;
        default:
            break;
    }

    canvas.fillCircle(26, 20, 5, srcColor);
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(srcColor);
    canvas.setTextDatum(middle_left);
    canvas.drawString(srcLabel, 38, 20);

    if (tm.rtcVoltageLowVlf) {
        canvas.fillRoundRect(340, 8, 210, 24, 6, 0x3A1010U);
        canvas.drawRoundRect(340, 8, 210, 24, 6, ThemeColors::ALERT_RED);
        canvas.setFont(&fonts::efontCN_14);
        canvas.setTextColor(ThemeColors::ALERT_RED);
        canvas.setTextDatum(middle_center);
        canvas.drawString("RTC 超级电容低压 VLF", 445, 20);
    }

    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.setTextDatum(middle_right);
    canvas.drawString("轻触任意卡片自定义城市或开启 GPS 经度动态时区", 1102, 20);
}

void Page7WorldClock::drawDayNightIcon(M5Canvas& canvas, int16_t cx, int16_t cy, bool isDaytime) {
    if (isDaytime) {
        const uint32_t sunCol = ThemeColors::RACING_YELLOW;
        canvas.fillCircle(cx, cy, 8, sunCol);
        for (int i = 0; i < 8; ++i) {
            const float rad = static_cast<float>(i) * 0.78539816f;
            const int16_t x0 = cx + static_cast<int16_t>(roundf(cosf(rad) * 11.0f));
            const int16_t y0 = cy + static_cast<int16_t>(roundf(sinf(rad) * 11.0f));
            const int16_t x1 = cx + static_cast<int16_t>(roundf(cosf(rad) * 15.0f));
            const int16_t y1 = cy + static_cast<int16_t>(roundf(sinf(rad) * 15.0f));
            canvas.drawLine(x0, y0, x1, y1, sunCol);
        }
    } else {
        const uint32_t moonCol = ThemeColors::ICE_CYAN;
        canvas.fillCircle(cx, cy, 11, moonCol);
        canvas.fillCircle(cx + 5, cy - 4, 9, ThemeColors::BG_CARD_DARK);
    }
}

void Page7WorldClock::drawClockCard(M5Canvas& canvas, uint8_t slot, const SystemStateSnapshot& state) const {
    int16_t x = 0;
    int16_t y = 0;
    getCardRect(slot, x, y);

    const WorldClockEntry& wc = state.time.worldClocks[slot];
    const uint32_t accent     = slotAccentColor(slot);
    const bool isPressed      = (_pressedSlot == static_cast<int8_t>(slot));

    const uint32_t bgCol     = isPressed ? ThemeColors::BG_CARD_HIGHLIGHT : ThemeColors::BG_CARD_DARK;
    const uint32_t borderCol = isPressed ? accent : ThemeColors::BORDER_SUBTLE;

    canvas.fillRoundRect(x, y, CARD_W, CARD_H, 14, bgCol);
    canvas.drawRoundRect(x, y, CARD_W, CARD_H, 14, borderCol);
    if (isPressed) {
        canvas.drawRoundRect(x + 1, y + 1, CARD_W - 2, CARD_H - 2, 13, accent);
    }

    // Left aviation accent bar
    canvas.fillRoundRect(x + 10, y + 18, 6, 56, 3, accent);

    // City Chinese + English header
    const char* zhName = wc.useGpsDynamicTz ? "GPS 动态时区" : lookupZhCityName(wc.cityName);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(top_left);
    canvas.drawString(zhName, x + 28, y + 16);

    char subLine[64];
    if (wc.useGpsDynamicTz) {
        if (state.gnss.isValid) {
            snprintf(subLine, sizeof(subLine), "LON %.2f%c · 经度自动推算",
                     fabs(state.gnss.longitude), (state.gnss.longitude >= 0.0) ? 'E' : 'W');
        } else {
            snprintf(subLine, sizeof(subLine), "等待定位 · 沿用本地时区");
        }
    } else {
        snprintf(subLine, sizeof(subLine), "%s · %s", wc.cityName, wc.regionGroup);
    }
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString(subLine, x + 28, y + 48);

    // Top-right Day/Night icon + UTC offset pill + optional GPS/DST badge
    char utcBuf[24];
    formatUtcOffset(wc.effectiveOffsetMin, utcBuf, sizeof(utcBuf));

    const int16_t utcPillW = 114;
    const int16_t utcPillX = x + CARD_W - utcPillW - 18;
    const int16_t utcPillY = y + 18;
    canvas.fillRoundRect(utcPillX, utcPillY, utcPillW, 32, 8, 0x182432U);
    canvas.drawRoundRect(utcPillX, utcPillY, utcPillW, 32, 8, accent);
    canvas.setFont(&fonts::DejaVu18);
    canvas.setTextColor(accent);
    canvas.setTextDatum(middle_center);
    canvas.drawString(utcBuf, utcPillX + utcPillW / 2, utcPillY + 16);

    drawDayNightIcon(canvas, utcPillX - 24, utcPillY + 16, wc.isDaytime);

    if (wc.useGpsDynamicTz) {
        const int16_t badgeW = 92;
        const int16_t badgeX = x + CARD_W - badgeW - 18;
        const int16_t badgeY = y + 56;
        canvas.fillRoundRect(badgeX, badgeY, badgeW, 24, 6, 0x0E2F24U);
        canvas.drawRoundRect(badgeX, badgeY, badgeW, 24, 6, ThemeColors::NEON_GREEN);
        canvas.setFont(&fonts::efontCN_14);
        canvas.setTextColor(ThemeColors::NEON_GREEN);
        canvas.setTextDatum(middle_center);
        canvas.drawString("GPS 跟随", badgeX + badgeW / 2, badgeY + 12);
    } else if (wc.isDstActive) {
        const int16_t badgeW = 86;
        const int16_t badgeX = x + CARD_W - badgeW - 18;
        const int16_t badgeY = y + 56;
        canvas.fillRoundRect(badgeX, badgeY, badgeW, 24, 6, 0x332408U);
        canvas.drawRoundRect(badgeX, badgeY, badgeW, 24, 6, ThemeColors::AMBER_WARN);
        canvas.setFont(&fonts::efontCN_14);
        canvas.setTextColor(ThemeColors::AMBER_WARN);
        canvas.setTextDatum(middle_center);
        canvas.drawString("DST 夏令时", badgeX + badgeW / 2, badgeY + 12);
    } else {
        canvas.setFont(&fonts::efontCN_14);
        canvas.setTextColor(ThemeColors::TEXT_MUTED);
        canvas.setTextDatum(top_right);
        canvas.drawString(wc.isDaytime ? "昼间时段" : "夜间时段", x + CARD_W - 20, y + 58);
    }

    // Divider line
    canvas.drawFastHLine(x + 20, y + 88, CARD_W - 40, 0x1B2634U);

    // Center ultra-large HH:MM:SS digital readout
    char hmBuf[12];
    char secBuf[8];
    snprintf(hmBuf, sizeof(hmBuf), "%02u:%02u",
             static_cast<unsigned>(wc.hour % 24U),
             static_cast<unsigned>(wc.minute % 60U));
    snprintf(secBuf, sizeof(secBuf), ":%02u",
             static_cast<unsigned>(wc.second % 60U));

    canvas.setFont(&fonts::DejaVu72);
    const int32_t hmW = canvas.textWidth(hmBuf);
    canvas.setFont(&fonts::DejaVu40);
    const int32_t secW = canvas.textWidth(secBuf);

    const int16_t totalTimeW = static_cast<int16_t>(hmW + secW + 6);
    const int16_t timeStartX = x + (CARD_W - totalTimeW) / 2;
    const int16_t timeBaseY  = y + 206;

    canvas.setFont(&fonts::DejaVu72);
    canvas.setTextColor(accent);
    canvas.setTextDatum(bottom_left);
    canvas.drawString(hmBuf, timeStartX, timeBaseY);

    canvas.setFont(&fonts::DejaVu40);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(bottom_left);
    canvas.drawString(secBuf, timeStartX + static_cast<int16_t>(hmW) + 6, timeBaseY - 6);

    // Bottom bar: Date + Weekday on left, Local diff + Edit hint on right
    canvas.drawFastHLine(x + 20, y + 252, CARD_W - 40, 0x1B2634U);

    char dateBuf[48];
    snprintf(dateBuf, sizeof(dateBuf), "%04u-%02u-%02u  %s",
             static_cast<unsigned>(wc.year),
             static_cast<unsigned>(wc.month),
             static_cast<unsigned>(wc.day),
             weekdayZh(wc.weekday));
    canvas.setFont(&fonts::efontCN_24);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(middle_left);
    canvas.drawString(dateBuf, x + 24, y + 286);

    const int diffMin = static_cast<int>(wc.effectiveOffsetMin) - static_cast<int>(state.time.localTzOffsetMin);
    char diffBuf[40];
    if (diffMin == 0) {
        snprintf(diffBuf, sizeof(diffBuf), "本地同区 · 点击编辑 >");
    } else {
        const int absDiff = (diffMin > 0) ? diffMin : -diffMin;
        const int dH      = absDiff / 60;
        const int dM      = absDiff % 60;
        if (dM == 0) {
            snprintf(diffBuf, sizeof(diffBuf), "%s %dh · 编辑 >", (diffMin > 0) ? "较本地快" : "较本地慢", dH);
        } else {
            snprintf(diffBuf, sizeof(diffBuf), "%s %dh%02dm · 编辑 >", (diffMin > 0) ? "快" : "慢", dH, dM);
        }
    }
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.setTextDatum(middle_right);
    canvas.drawString(diffBuf, x + CARD_W - 22, y + 286);
}

bool Page7WorldClock::onTouch(const TouchEvent& event, const SystemStateSnapshot& state) {
    int8_t hitSlot = -1;
    for (uint8_t slot = 0; slot < 4U; ++slot) {
        int16_t cx = 0;
        int16_t cy = 0;
        getCardRect(slot, cx, cy);
        if (event.hitTestLocal(cx, cy, CARD_W, CARD_H)) {
            hitSlot = static_cast<int8_t>(slot);
            break;
        }
    }

    if (event.type == TOUCH_EVT_DOWN) {
        _pressedSlot = hitSlot;
        return false; // Allow horizontal swipe if user drags across card
    }

    if (event.type == TOUCH_EVT_MOVE) {
        if (abs(event.localX - event.startLocalX) > GestureConfig::TAP_MAX_JITTER_PX ||
            abs(event.localY - event.startLocalY) > GestureConfig::TAP_MAX_JITTER_PX) {
            _pressedSlot = -1;
        }
        return false;
    }

    if (event.type == TOUCH_EVT_UP) {
        _pressedSlot = -1;
        return false;
    }

    if (event.type == TOUCH_EVT_TAP && hitSlot >= 0) {
        _pressedSlot = -1;
        {
            StateGuard lock(SystemState::instance());
            if (lock.locked()) {
                SystemState::instance().data().time.editingCardIndex = static_cast<uint8_t>(hitSlot);
            }
        }
        playFeedbackTone(state, HapticConfig::TONE_TAP_HZ, HapticConfig::TONE_TAP_MS);
        requestNavigate(SUBPAGE_EDIT_CLOCK);
        return true;
    }

    return false;
}
