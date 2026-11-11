#pragma once

#include "BasePage.h"

class SubPageEditClock : public BasePage {
public:
    SubPageEditClock() = default;
    ~SubPageEditClock() override = default;

    PageId pageId() const override { return SUBPAGE_EDIT_CLOCK; }
    const char* pageTitle() const override { return "编辑世界时钟卡片"; }
    bool allowsHorizontalSwipe() const override { return false; }

    void onEnter(const SystemStateSnapshot& state) override;
    void render(M5Canvas& canvas, const SystemStateSnapshot& state) override;
    bool onTouch(const TouchEvent& event, const SystemStateSnapshot& state) override;

private:
    enum RegionFilter : uint8_t {
        FILTER_POPULAR  = 0,
        FILTER_ASIA     = 1,
        FILTER_EU_ME    = 2,
        FILTER_AM_OC    = 3,
        FILTER_COUNT    = 4
    };

    struct CityPreset {
        const char* zhName;
        const char* enName;
        const char* regionGroup;
        const char* subTag;
        int16_t     baseOffsetMin;
        DstRuleType dstRule;
        uint8_t     filterMask; // bitmask of RegionFilter
    };

    static constexpr int16_t BTN_BACK_X     = 18;
    static constexpr int16_t BTN_BACK_Y     = 12;
    static constexpr int16_t BTN_BACK_W     = 210;
    static constexpr int16_t BTN_BACK_H     = 58;

    static constexpr int16_t BTN_GPS_TZ_X   = 18;
    static constexpr int16_t BTN_GPS_TZ_Y   = 82;
    static constexpr int16_t BTN_GPS_TZ_W   = 1084;
    static constexpr int16_t BTN_GPS_TZ_H   = 96;

    static constexpr int16_t TAB_Y          = 192;
    static constexpr int16_t TAB_H          = 52;
    static constexpr int16_t TAB_W          = 262;
    static constexpr int16_t TAB_GAP        = 12;

    static constexpr int16_t GRID_X0        = 18;
    static constexpr int16_t GRID_Y0        = 258;
    static constexpr int16_t CELL_W         = 262;
    static constexpr int16_t CELL_H         = 138;
    static constexpr int16_t CELL_GAP_X     = 12;
    static constexpr int16_t CELL_GAP_Y     = 12;
    static constexpr uint8_t GRID_COLS      = 4;
    static constexpr uint8_t GRID_ROWS      = 3;
    static constexpr uint8_t MAX_GRID_ITEMS = GRID_COLS * GRID_ROWS; // 12

    RegionFilter _activeFilter = FILTER_POPULAR;
    int8_t       _pressedCell  = -1;
    bool         _gpsPressed   = false;
    bool         _backPressed  = false;

    uint8_t collectFilteredPresets(const CityPreset* outList[MAX_GRID_ITEMS]) const;
    void applyGpsDynamicTimezone(const SystemStateSnapshot& state);
    void applyCityPreset(const CityPreset& preset, const SystemStateSnapshot& state);
};
