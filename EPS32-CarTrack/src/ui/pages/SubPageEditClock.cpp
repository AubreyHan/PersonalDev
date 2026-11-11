#include "SubPageEditClock.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <type_traits>

static_assert(!std::is_abstract<SubPageEditClock>::value && std::is_default_constructible<SubPageEditClock>::value,
              "SubPageEditClock must be concrete and default-constructible");

namespace {

constexpr uint8_t MASK_POP   = (1U << 0);
constexpr uint8_t MASK_ASIA  = (1U << 1);
constexpr uint8_t MASK_EU_ME = (1U << 2);
constexpr uint8_t MASK_AM_OC = (1U << 3);

bool isLeapYear(uint16_t y) {
    return ((y % 4U) == 0U && (y % 100U) != 0U) || ((y % 400U) == 0U);
}

uint8_t daysInMonth(uint16_t y, uint8_t m) {
    static const uint8_t kDays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2U && isLeapYear(y)) {
        return 29U;
    }
    if (m >= 1U && m <= 12U) {
        return kDays[m - 1U];
    }
    return 30U;
}

// Sakamoto algorithm: 0=Sunday .. 6=Saturday
uint8_t computeWeekday(uint16_t y, uint8_t m, uint8_t d) {
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int yy = static_cast<int>(y);
    if (m < 3U) {
        yy -= 1;
    }
    return static_cast<uint8_t>((yy + yy / 4 - yy / 100 + yy / 400 + t[(m - 1U) % 12U] + d) % 7);
}

uint8_t nthSundayOfMonth(uint16_t y, uint8_t m, uint8_t nth) {
    const uint8_t firstWday = computeWeekday(y, m, 1U);
    const uint8_t firstSun  = static_cast<uint8_t>(1U + ((7U - firstWday) % 7U));
    return static_cast<uint8_t>(firstSun + (nth - 1U) * 7U);
}

uint8_t lastSundayOfMonth(uint16_t y, uint8_t m) {
    const uint8_t dim      = daysInMonth(y, m);
    const uint8_t lastWday = computeWeekday(y, m, dim);
    return static_cast<uint8_t>(dim - lastWday);
}

bool evaluateDstActive(DstRuleType rule, uint16_t utcY, uint8_t utcM, uint8_t utcD, uint8_t utcH, int16_t baseOffsetMin) {
    if (rule == DST_RULE_NONE) {
        return false;
    }

    if (rule == DST_RULE_US_EASTERN || rule == DST_RULE_US_PACIFIC) {
        // US DST: 2nd Sunday of March 02:00 local standard -> 1st Sunday of November 02:00 local daylight
        if (utcM < 3U || utcM > 11U) return false;
        if (utcM > 3U && utcM < 11U) return true;
        const int stdHourUtc = 2 - (baseOffsetMin / 60);
        if (utcM == 3U) {
            const uint8_t startDay = nthSundayOfMonth(utcY, 3U, 2U);
            if (utcD > startDay) return true;
            if (utcD < startDay) return false;
            return static_cast<int>(utcH) >= stdHourUtc;
        } else {
            const int dstEndUtc    = 2 - ((baseOffsetMin + 60) / 60);
            const uint8_t endDay   = nthSundayOfMonth(utcY, 11U, 1U);
            if (utcD < endDay) return true;
            if (utcD > endDay) return false;
            return static_cast<int>(utcH) < dstEndUtc;
        }
    }

    if (rule == DST_RULE_EU_LONDON || rule == DST_RULE_EU_CENTRAL) {
        // EU/UK DST: Last Sunday of March 01:00 UTC -> Last Sunday of October 01:00 UTC
        if (utcM < 3U || utcM > 10U) return false;
        if (utcM > 3U && utcM < 10U) return true;
        if (utcM == 3U) {
            const uint8_t startDay = lastSundayOfMonth(utcY, 3U);
            if (utcD > startDay) return true;
            if (utcD < startDay) return false;
            return utcH >= 1U;
        } else {
            const uint8_t endDay = lastSundayOfMonth(utcY, 10U);
            if (utcD < endDay) return true;
            if (utcD > endDay) return false;
            return utcH < 1U;
        }
    }

    if (rule == DST_RULE_AU_SYDNEY) {
        // Australia NSW DST: 1st Sunday of October 16:00 UTC (prev day) -> 1st Sunday of April 16:00 UTC
        if (utcM > 4U && utcM < 10U) return false;
        if (utcM > 10U || utcM < 4U) return true;
        if (utcM == 10U) {
            const uint8_t startDay = nthSundayOfMonth(utcY, 10U, 1U);
            return utcD >= startDay;
        } else {
            const uint8_t endDay = nthSundayOfMonth(utcY, 4U, 1U);
            return utcD < endDay;
        }
    }

    return false;
}

void refreshEntryLiveClock(WorldClockEntry& entry, const TimeData& tm, const GnssData& gnss) {
    int16_t effOffset = entry.baseUtcOffsetMin;
    bool dstActive    = false;

    if (entry.useGpsDynamicTz) {
        if (gnss.isValid) {
            int zoneHours = static_cast<int>(lround(gnss.longitude / 15.0));
            if (zoneHours < -12) zoneHours = -12;
            if (zoneHours > 14)  zoneHours = 14;
            effOffset = static_cast<int16_t>(zoneHours * 60);
        } else {
            effOffset = tm.localTzOffsetMin;
        }
        entry.baseUtcOffsetMin = effOffset;
    } else {
        dstActive = evaluateDstActive(entry.dstRule, tm.utcYear, tm.utcMonth, tm.utcDay, tm.utcHour, entry.baseUtcOffsetMin);
        if (dstActive) {
            effOffset = static_cast<int16_t>(effOffset + 60);
        }
    }

    entry.effectiveOffsetMin = effOffset;
    entry.isDstActive        = dstActive;

    int totalMinutes = static_cast<int>(tm.utcHour) * 60 + static_cast<int>(tm.utcMinute) + static_cast<int>(effOffset);
    uint16_t y       = (tm.utcYear >= 2020U) ? tm.utcYear : 2026U;
    uint8_t  m       = (tm.utcMonth >= 1U && tm.utcMonth <= 12U) ? tm.utcMonth : 1U;
    uint8_t  d       = (tm.utcDay >= 1U && tm.utcDay <= 31U) ? tm.utcDay : 1U;

    while (totalMinutes < 0) {
        totalMinutes += 1440;
        if (d > 1U) {
            d--;
        } else {
            if (m > 1U) {
                m--;
            } else {
                m = 12U;
                y--;
            }
            d = daysInMonth(y, m);
        }
    }
    while (totalMinutes >= 1440) {
        totalMinutes -= 1440;
        const uint8_t dim = daysInMonth(y, m);
        if (d < dim) {
            d++;
        } else {
            d = 1U;
            if (m < 12U) {
                m++;
            } else {
                m = 1U;
                y++;
            }
        }
    }

    entry.year      = y;
    entry.month     = m;
    entry.day       = d;
    entry.weekday   = computeWeekday(y, m, d);
    entry.hour      = static_cast<uint8_t>((totalMinutes / 60) % 24);
    entry.minute    = static_cast<uint8_t>(totalMinutes % 60);
    entry.second    = tm.utcSecond;
    entry.isDaytime = (entry.hour >= 6U && entry.hour < 18U);
}

void formatOffsetText(int16_t offsetMin, char* buf, size_t len) {
    const char sign = (offsetMin >= 0) ? '+' : '-';
    const int absM  = (offsetMin >= 0) ? offsetMin : -offsetMin;
    const int hh    = absM / 60;
    const int mm    = absM % 60;
    if (mm == 0) {
        snprintf(buf, len, "UTC%c%d", sign, hh);
    } else {
        snprintf(buf, len, "UTC%c%d:%02d", sign, hh, mm);
    }
}

} // namespace

uint8_t SubPageEditClock::collectFilteredPresets(const CityPreset* outList[MAX_GRID_ITEMS]) const {
    static const CityPreset kPresets[] = {
        { "北京",     "Beijing",     "Asia",     "中国标准时间",      480, DST_RULE_NONE,       MASK_POP | MASK_ASIA },
        { "乌鲁木齐", "Urumqi",      "Asia",     "丝路西域地方时",    360, DST_RULE_NONE,       MASK_POP | MASK_ASIA },
        { "世界标准", "UTC",         "UTC",      "航空基准原子时",      0, DST_RULE_NONE,       MASK_POP | MASK_EU_ME },
        { "东京",     "Tokyo",       "Asia",     "日本标准时间",      540, DST_RULE_NONE,       MASK_POP | MASK_ASIA },
        { "迪拜",     "Dubai",       "Asia",     "中东海湾自驾",      240, DST_RULE_NONE,       MASK_POP | MASK_ASIA | MASK_EU_ME },
        { "伦敦",     "London",      "Europe",   "自动夏令时 BST",      0, DST_RULE_EU_LONDON,  MASK_POP | MASK_EU_ME },
        { "巴黎",     "Paris",       "Europe",   "中欧自动夏令时",     60, DST_RULE_EU_CENTRAL, MASK_POP | MASK_EU_ME },
        { "莫斯科",   "Moscow",      "Europe",   "亚欧大陆走廊",      180, DST_RULE_NONE,       MASK_POP | MASK_EU_ME },
        { "纽约",     "New York",    "Americas", "美东自动夏令时",   -300, DST_RULE_US_EASTERN, MASK_POP | MASK_AM_OC },
        { "洛杉矶",   "Los Angeles", "Americas", "美西66号公路",     -480, DST_RULE_US_PACIFIC, MASK_POP | MASK_AM_OC },
        { "悉尼",     "Sydney",      "Oceania",  "澳洲自动夏令时",    600, DST_RULE_AU_SYDNEY,  MASK_POP | MASK_AM_OC },
        { "曼谷",     "Bangkok",     "Asia",     "中南半岛自驾",      420, DST_RULE_NONE,       MASK_POP | MASK_ASIA },

        // Additional regional presets
        { "新加坡",   "Singapore",   "Asia",     "东南亚枢纽",        480, DST_RULE_NONE,       MASK_ASIA },
        { "阿拉木图", "Almaty",      "Asia",     "中亚丝路门户",      300, DST_RULE_NONE,       MASK_ASIA },
        { "开罗",     "Cairo",       "Europe",   "北非红海走廊",      120, DST_RULE_NONE,       MASK_EU_ME },
        { "法兰克福", "Frankfurt",   "Europe",   "德国不限速高速",     60, DST_RULE_EU_CENTRAL, MASK_EU_ME },
        { "温哥华",   "Vancouver",   "Americas", "落基山冰原大道",   -480, DST_RULE_US_PACIFIC, MASK_AM_OC },
        { "芝加哥",   "Chicago",     "Americas", "北美中部起点",     -360, DST_RULE_US_EASTERN, MASK_AM_OC },
        { "安克雷奇", "Anchorage",   "Americas", "阿拉斯加极地",     -540, DST_RULE_NONE,       MASK_AM_OC },
        { "奥克兰",   "Auckland",    "Oceania",  "新西兰环岛",        720, DST_RULE_NONE,       MASK_AM_OC }
    };

    const uint8_t targetBit = static_cast<uint8_t>(1U << static_cast<uint8_t>(_activeFilter));
    uint8_t count           = 0;
    for (size_t i = 0; i < sizeof(kPresets) / sizeof(kPresets[0]); ++i) {
        if ((kPresets[i].filterMask & targetBit) != 0U) {
            outList[count++] = &kPresets[i];
            if (count >= MAX_GRID_ITEMS) {
                break;
            }
        }
    }
    return count;
}

void SubPageEditClock::onEnter(const SystemStateSnapshot& state) {
    (void)state;
    _pressedCell = -1;
    _gpsPressed  = false;
    _backPressed = false;
}

void SubPageEditClock::render(M5Canvas& canvas, const SystemStateSnapshot& state) {
    clearViewportCanvas(canvas);

    const uint8_t slot          = static_cast<uint8_t>(state.time.editingCardIndex & 0x03U);
    const WorldClockEntry& curr = state.time.worldClocks[slot];

    // 1. Top Back Button & Title
    const uint32_t backBg = _backPressed ? ThemeColors::BG_CARD_HIGHLIGHT : ThemeColors::BG_CARD_DARK;
    canvas.fillRoundRect(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H, 10, backBg);
    canvas.drawRoundRect(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H, 10, ThemeColors::ICE_CYAN);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.setTextDatum(middle_center);
    canvas.drawString("< 返回时钟页", BTN_BACK_X + BTN_BACK_W / 2, BTN_BACK_Y + BTN_BACK_H / 2);

    char titleBuf[64];
    snprintf(titleBuf, sizeof(titleBuf), "编辑世界时钟卡槽 #%u", static_cast<unsigned>(slot + 1U));
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.setTextDatum(middle_left);
    canvas.drawString(titleBuf, 248, 41);

    char currInfo[80];
    char currUtc[24];
    formatOffsetText(curr.effectiveOffsetMin, currUtc, sizeof(currUtc));
    snprintf(currInfo, sizeof(currInfo), "当前配置: %s (%s)",
             curr.useGpsDynamicTz ? "GPS 动态经度时区" : curr.cityName, currUtc);
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.setTextDatum(middle_right);
    canvas.drawString(currInfo, 1102, 41);

    // 2. Hero Card Button: One-Tap GPS Longitude Dynamic Timezone
    const bool gpsActive   = curr.useGpsDynamicTz;
    const uint32_t gpsBg   = _gpsPressed ? 0x16382CU : (gpsActive ? 0x0E2920U : ThemeColors::BG_CARD_DARK);
    const uint32_t gpsBord = gpsActive ? ThemeColors::NEON_GREEN : ThemeColors::ICE_CYAN;

    canvas.fillRoundRect(BTN_GPS_TZ_X, BTN_GPS_TZ_Y, BTN_GPS_TZ_W, BTN_GPS_TZ_H, 14, gpsBg);
    canvas.drawRoundRect(BTN_GPS_TZ_X, BTN_GPS_TZ_Y, BTN_GPS_TZ_W, BTN_GPS_TZ_H, 14, gpsBord);
    if (gpsActive || _gpsPressed) {
        canvas.drawRoundRect(BTN_GPS_TZ_X + 1, BTN_GPS_TZ_Y + 1, BTN_GPS_TZ_W - 2, BTN_GPS_TZ_H - 2, 13, ThemeColors::NEON_GREEN);
    }

    // Vector Satellite / Globe Crosshair Icon on left
    const int16_t icX = BTN_GPS_TZ_X + 48;
    const int16_t icY = BTN_GPS_TZ_Y + BTN_GPS_TZ_H / 2;
    canvas.drawCircle(icX, icY, 24, ThemeColors::NEON_GREEN);
    canvas.drawCircle(icX, icY, 12, ThemeColors::ICE_CYAN);
    canvas.drawFastHLine(icX - 28, icY, 56, ThemeColors::NEON_GREEN);
    canvas.drawFastVLine(icX, icY - 28, 56, ThemeColors::NEON_GREEN);

    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.setTextDatum(top_left);
    canvas.drawString("一键设为【GPS 经度动态时区】", BTN_GPS_TZ_X + 92, BTN_GPS_TZ_Y + 16);

    char gpsDesc[128];
    if (state.gnss.isValid) {
        const int dynHours = static_cast<int>(lround(state.gnss.longitude / 15.0));
        snprintf(gpsDesc, sizeof(gpsDesc),
                 "跨省/跨国长途穿越推荐 · 当前经度 %.3f%c -> 实时推算 UTC%+d",
                 fabs(state.gnss.longitude),
                 (state.gnss.longitude >= 0.0) ? 'E' : 'W',
                 dynHours);
    } else {
        snprintf(gpsDesc, sizeof(gpsDesc),
                 "跨省/跨国长途穿越推荐 · 依据实时卫星经度 (Lon / 15°) 自动切换当地标准时区");
    }
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString(gpsDesc, BTN_GPS_TZ_X + 92, BTN_GPS_TZ_Y + 54);

    // Right action pill inside GPS button
    const int16_t actW = 160;
    const int16_t actX = BTN_GPS_TZ_X + BTN_GPS_TZ_W - actW - 22;
    const int16_t actY = BTN_GPS_TZ_Y + 24;
    canvas.fillRoundRect(actX, actY, actW, 48, 10, gpsActive ? ThemeColors::NEON_GREEN : 0x162B38U);
    canvas.drawRoundRect(actX, actY, actW, 48, 10, ThemeColors::NEON_GREEN);
    canvas.setFont(&fonts::efontCN_24_b);
    canvas.setTextColor(gpsActive ? TFT_BLACK : ThemeColors::NEON_GREEN);
    canvas.setTextDatum(middle_center);
    canvas.drawString(gpsActive ? "当前已启用" : "点击启用", actX + actW / 2, actY + 24);

    // 3. Region Filter Tabs
    static const char* kTabTitles[FILTER_COUNT] = {
        "全部热门 (12)",
        "亚太与丝路",
        "欧洲与中东",
        "美洲与大洋洲"
    };

    for (uint8_t t = 0; t < FILTER_COUNT; ++t) {
        const int16_t tx  = GRID_X0 + static_cast<int16_t>(t) * (TAB_W + TAB_GAP);
        const bool active = (_activeFilter == static_cast<RegionFilter>(t));
        canvas.fillRoundRect(tx, TAB_Y, TAB_W, TAB_H, 10, active ? 0x1A2E3DU : ThemeColors::BG_CARD_DARK);
        canvas.drawRoundRect(tx, TAB_Y, TAB_W, TAB_H, 10, active ? ThemeColors::ICE_CYAN : ThemeColors::BORDER_SUBTLE);
        canvas.setFont(active ? &fonts::efontCN_24_b : &fonts::efontCN_24);
        canvas.setTextColor(active ? ThemeColors::ICE_CYAN : ThemeColors::TEXT_MUTED);
        canvas.setTextDatum(middle_center);
        canvas.drawString(kTabTitles[t], tx + TAB_W / 2, TAB_Y + TAB_H / 2);
    }

    // 4. 4x3 Large Hitbox City Selector Grid
    const CityPreset* list[MAX_GRID_ITEMS] = {};
    const uint8_t count = collectFilteredPresets(list);

    for (uint8_t idx = 0; idx < count; ++idx) {
        const CityPreset& item = *list[idx];
        const uint8_t col      = idx % GRID_COLS;
        const uint8_t row      = idx / GRID_COLS;
        const int16_t cx       = GRID_X0 + static_cast<int16_t>(col) * (CELL_W + CELL_GAP_X);
        const int16_t cy       = GRID_Y0 + static_cast<int16_t>(row) * (CELL_H + CELL_GAP_Y);

        const bool isSelected = (!curr.useGpsDynamicTz) && (strcasecmp(curr.cityName, item.enName) == 0);
        const bool isPressed  = (_pressedCell == static_cast<int8_t>(idx));

        const uint32_t bgCol   = isPressed ? ThemeColors::BG_CARD_HIGHLIGHT : (isSelected ? 0x122634U : ThemeColors::BG_CARD_DARK);
        const uint32_t bordCol = isSelected ? ThemeColors::NEON_GREEN : (isPressed ? ThemeColors::ICE_CYAN : ThemeColors::BORDER_SUBTLE);

        canvas.fillRoundRect(cx, cy, CELL_W, CELL_H, 12, bgCol);
        canvas.drawRoundRect(cx, cy, CELL_W, CELL_H, 12, bordCol);
        if (isSelected) {
            canvas.drawRoundRect(cx + 1, cy + 1, CELL_W - 2, CELL_H - 2, 11, ThemeColors::NEON_GREEN);
        }

        // City Chinese Name
        canvas.setFont(&fonts::efontCN_24_b);
        canvas.setTextColor(isSelected ? ThemeColors::NEON_GREEN : ThemeColors::TEXT_WHITE);
        canvas.setTextDatum(top_left);
        canvas.drawString(item.zhName, cx + 16, cy + 14);

        // UTC Offset Pill on top-right
        char offStr[20];
        formatOffsetText(item.baseOffsetMin, offStr, sizeof(offStr));
        canvas.fillRoundRect(cx + CELL_W - 98, cy + 14, 84, 28, 6, 0x182432U);
        canvas.setFont(&fonts::DejaVu18);
        canvas.setTextColor(ThemeColors::ICE_CYAN);
        canvas.setTextDatum(middle_center);
        canvas.drawString(offStr, cx + CELL_W - 56, cy + 28);

        // English Name
        canvas.setFont(&fonts::DejaVu18);
        canvas.setTextColor(ThemeColors::TEXT_MUTED);
        canvas.setTextDatum(top_left);
        canvas.drawString(item.enName, cx + 16, cy + 52);

        // Subtitle / Silk Road / Auto-DST note
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextColor((item.dstRule != DST_RULE_NONE) ? ThemeColors::AMBER_WARN : ThemeColors::TEXT_MUTED);
        canvas.setTextDatum(bottom_left);
        canvas.drawString(item.subTag, cx + 16, cy + CELL_H - 14);
    }
}

void SubPageEditClock::applyGpsDynamicTimezone(const SystemStateSnapshot& state) {
    StateGuard lock(SystemState::instance());
    if (lock.locked()) {
        auto& st             = SystemState::instance().data();
        const uint8_t slot   = static_cast<uint8_t>(st.time.editingCardIndex & 0x03U);
        WorldClockEntry& dst = st.time.worldClocks[slot];

        strncpy(dst.cityName, "GPS Dynamic", sizeof(dst.cityName) - 1);
        dst.cityName[sizeof(dst.cityName) - 1] = '\0';
        strncpy(dst.regionGroup, "GPS", sizeof(dst.regionGroup) - 1);
        dst.regionGroup[sizeof(dst.regionGroup) - 1] = '\0';
        dst.dstRule         = DST_RULE_NONE;
        dst.useGpsDynamicTz = true;

        refreshEntryLiveClock(dst, st.time, st.gnss);
        st.time.cmdSaveClocksNvs = true;
    }

    playFeedbackTone(state, HapticConfig::TONE_CONFIRM_2_HZ, HapticConfig::TONE_CONFIRM_MS);
    requestNavigate(PAGE_7_WORLD_CLOCK);
}

void SubPageEditClock::applyCityPreset(const CityPreset& preset, const SystemStateSnapshot& state) {
    StateGuard lock(SystemState::instance());
    if (lock.locked()) {
        auto& st             = SystemState::instance().data();
        const uint8_t slot   = static_cast<uint8_t>(st.time.editingCardIndex & 0x03U);
        WorldClockEntry& dst = st.time.worldClocks[slot];

        strncpy(dst.cityName, preset.enName, sizeof(dst.cityName) - 1);
        dst.cityName[sizeof(dst.cityName) - 1] = '\0';
        strncpy(dst.regionGroup, preset.regionGroup, sizeof(dst.regionGroup) - 1);
        dst.regionGroup[sizeof(dst.regionGroup) - 1] = '\0';
        dst.baseUtcOffsetMin = preset.baseOffsetMin;
        dst.dstRule          = preset.dstRule;
        dst.useGpsDynamicTz  = false;

        refreshEntryLiveClock(dst, st.time, st.gnss);
        st.time.cmdSaveClocksNvs = true;
    }

    playFeedbackTone(state, HapticConfig::TONE_CONFIRM_1_HZ, HapticConfig::TONE_CONFIRM_MS);
    requestNavigate(PAGE_7_WORLD_CLOCK);
}

bool SubPageEditClock::onTouch(const TouchEvent& event, const SystemStateSnapshot& state) {
    if (event.type == TOUCH_EVT_DOWN) {
        _backPressed = event.hitTestLocal(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H);
        _gpsPressed  = event.hitTestLocal(BTN_GPS_TZ_X, BTN_GPS_TZ_Y, BTN_GPS_TZ_W, BTN_GPS_TZ_H);
        _pressedCell = -1;

        const CityPreset* list[MAX_GRID_ITEMS] = {};
        const uint8_t count = collectFilteredPresets(list);
        for (uint8_t idx = 0; idx < count; ++idx) {
            const int16_t cx = GRID_X0 + static_cast<int16_t>(idx % GRID_COLS) * (CELL_W + CELL_GAP_X);
            const int16_t cy = GRID_Y0 + static_cast<int16_t>(idx / GRID_COLS) * (CELL_H + CELL_GAP_Y);
            if (event.hitTestLocal(cx, cy, CELL_W, CELL_H)) {
                _pressedCell = static_cast<int8_t>(idx);
                break;
            }
        }
        return true;
    }

    if (event.type == TOUCH_EVT_UP) {
        _backPressed = false;
        _gpsPressed  = false;
        _pressedCell = -1;
        return true;
    }

    if (event.type == TOUCH_EVT_TAP) {
        _backPressed = false;
        _gpsPressed  = false;
        _pressedCell = -1;

        if (event.hitTestLocal(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H)) {
            playFeedbackTone(state);
            requestNavigate(PAGE_7_WORLD_CLOCK);
            return true;
        }

        if (event.hitTestLocal(BTN_GPS_TZ_X, BTN_GPS_TZ_Y, BTN_GPS_TZ_W, BTN_GPS_TZ_H)) {
            applyGpsDynamicTimezone(state);
            return true;
        }

        // Filter tabs
        for (uint8_t t = 0; t < FILTER_COUNT; ++t) {
            const int16_t tx = GRID_X0 + static_cast<int16_t>(t) * (TAB_W + TAB_GAP);
            if (event.hitTestLocal(tx, TAB_Y, TAB_W, TAB_H)) {
                _activeFilter = static_cast<RegionFilter>(t);
                playFeedbackTone(state);
                return true;
            }
        }

        // City grid cells
        const CityPreset* list[MAX_GRID_ITEMS] = {};
        const uint8_t count = collectFilteredPresets(list);
        for (uint8_t idx = 0; idx < count; ++idx) {
            const int16_t cx = GRID_X0 + static_cast<int16_t>(idx % GRID_COLS) * (CELL_W + CELL_GAP_X);
            const int16_t cy = GRID_Y0 + static_cast<int16_t>(idx / GRID_COLS) * (CELL_H + CELL_GAP_Y);
            if (event.hitTestLocal(cx, cy, CELL_W, CELL_H)) {
                applyCityPreset(*list[idx], state);
                return true;
            }
        }
        return true;
    }

    return true;
}
