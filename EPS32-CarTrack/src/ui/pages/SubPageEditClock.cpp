#include "ui/pages/SubPageEditClock.h"
#include "config.h"

static const WorldCityDef PRESET_CITIES[] = {
    // Asia / Pacific
    {"北京", "Beijing", 8, false, REGION_ASIA_PACIFIC},
    {"东京", "Tokyo", 9, false, REGION_ASIA_PACIFIC},
    {"首尔", "Seoul", 9, false, REGION_ASIA_PACIFIC},
    {"悉尼", "Sydney", 10, true, REGION_ASIA_PACIFIC},
    {"新加坡", "Singapore", 8, false, REGION_ASIA_PACIFIC},

    // Americas
    {"纽约", "New York", -5, true, REGION_AMERICAS},
    {"旧金山", "San Francisco", -8, true, REGION_AMERICAS},
    {"芝加哥", "Chicago", -6, true, REGION_AMERICAS},
    {"温哥华", "Vancouver", -8, true, REGION_AMERICAS},

    // Europe
    {"伦敦", "London", 0, true, REGION_EUROPE},
    {"巴黎", "Paris", 1, true, REGION_EUROPE},
    {"莫斯科", "Moscow", 3, false, REGION_EUROPE},

    // UTC
    {"UTC", "UTC Standard", 0, false, REGION_UTC_STANDARD}
};

static constexpr size_t PRESET_CITY_COUNT = sizeof(PRESET_CITIES) / sizeof(PRESET_CITIES[0]);

void SubPageEditClock::draw(M5Canvas& canvas, const SystemContext& ctx) {
    if (!_isOpen) return;

    canvas.fillScreen(UiColors::COLOR_BG);

    // Title
    canvas.setTextSize(1);
    char titleBuf[48];
    snprintf(titleBuf, sizeof(titleBuf), "修改时钟槽位 #%d: 目标城市选择", ctx.worldClock.activeEditSlot + 1);
    canvas.setTextColor(UiColors::COLOR_SECONDARY, UiColors::COLOR_BG);
    canvas.drawString(titleBuf, 8, 28);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
    canvas.drawRightString("[上滑返回]", 312, 28);

    // 1. GPS Auto Capsule Button (X: 8~150, Y: 46~72)
    canvas.fillRoundRect(8, 46, 144, 26, 4, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(8, 46, 144, 26, 4, UiColors::COLOR_PRIMARY);
    canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_PANEL_BG);
    canvas.drawCenterString("📍 GPS 自动时区", 80, 52);

    // 2. Region Tabs (X: 8~312, Y: 78~100)
    static const char* const TABS[] = {"亚太", "美洲", "欧洲", "UTC"};
    for (int i = 0; i < 4; i++) {
        int tx = 8 + i * 78;
        uint16_t tabBg = (ctx.worldClock.activeEditRegion == i) ? UiColors::COLOR_PRIMARY : UiColors::COLOR_PANEL_BG;
        uint16_t tabTxt = (ctx.worldClock.activeEditRegion == i) ? UiColors::COLOR_BG : UiColors::COLOR_WHITE;
        canvas.fillRoundRect(tx, 78, 72, 22, 3, tabBg);
        canvas.setTextColor(tabTxt, tabBg);
        canvas.drawCenterString(TABS[i], tx + 36, 82);
    }

    // 3. City Items List (38px large touch items, Y: 106~220)
    uint8_t curRegion = ctx.worldClock.activeEditRegion;
    int yOffset = 106;
    for (size_t i = 0; i < PRESET_CITY_COUNT; i++) {
        const WorldCityDef& city = PRESET_CITIES[i];
        if (city.region == curRegion) {
            canvas.fillRoundRect(8, yOffset, 304, 34, 4, UiColors::COLOR_PANEL_BG);
            canvas.drawRoundRect(8, yOffset, 304, 34, 4, UiColors::COLOR_BORDER);

            char nameBuf[32];
            snprintf(nameBuf, sizeof(nameBuf), "%s %s (UTC%+d)", city.cityNameCn, city.cityNameEn, city.standardUtcOffsetHours);
            canvas.setTextColor(UiColors::COLOR_WHITE, UiColors::COLOR_PANEL_BG);
            canvas.drawString(nameBuf, 16, yOffset + 10);

            canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_PANEL_BG);
            canvas.drawRightString("点选保存", 300, yOffset + 10);

            yOffset += 38;
            if (yOffset > 210) break;
        }
    }
}

void SubPageEditClock::handleSwipeUp() {
    close();
}

void SubPageEditClock::handleTap(int16_t x, int16_t y) {
    if (!_isOpen) return;

    SystemState& state = SystemState::instance();

    // 1. Tap on GPS Auto (X: 8~152, Y: 46~72)
    if (x >= 8 && x <= 152 && y >= 46 && y <= 72) {
        state.lock();
        uint8_t slotIdx = state.getContextUnsafe().worldClock.activeEditSlot;
        state.getContextUnsafe().worldClock.slots[slotIdx].mode = CLOCK_MODE_GPS_AUTO;
        snprintf(state.getContextUnsafe().worldClock.slots[slotIdx].displayName, 20, "GPS 自适应");
        state.unlock();
        close();
        return;
    }

    // 2. Tap on Region Tabs (Y: 76~102)
    if (y >= 76 && y <= 102) {
        int tabIdx = (x - 8) / 78;
        if (tabIdx >= 0 && tabIdx < 4) {
            state.lock();
            state.getContextUnsafe().worldClock.activeEditRegion = tabIdx;
            state.unlock();
            return;
        }
    }

    // 3. Tap on City Item (Y: 106~220)
    if (y >= 106 && y <= 220) {
        state.lock();
        uint8_t curRegion = state.getContextUnsafe().worldClock.activeEditRegion;
        uint8_t slotIdx = state.getContextUnsafe().worldClock.activeEditSlot;

        int row = (y - 106) / 38;
        int matchCount = 0;
        for (size_t i = 0; i < PRESET_CITY_COUNT; i++) {
            if (PRESET_CITIES[i].region == curRegion) {
                if (matchCount == row) {
                    // Selected this city
                    state.getContextUnsafe().worldClock.slots[slotIdx].mode = CLOCK_MODE_MANUAL_CITY;
                    state.getContextUnsafe().worldClock.slots[slotIdx].utcOffsetHours = PRESET_CITIES[i].standardUtcOffsetHours;
                    snprintf(state.getContextUnsafe().worldClock.slots[slotIdx].displayName, 20, "%s %s", PRESET_CITIES[i].cityNameCn, PRESET_CITIES[i].cityNameEn);
                    state.unlock();
                    close();
                    return;
                }
                matchCount++;
            }
        }
        state.unlock();
    }
}
