#pragma once
#include <Arduino.h>

// =============================================================================
// M5Track-Tab5 System Hardware Configuration & Constants
// =============================================================================

namespace PinConfig {
    // Module GNSS (M135) UART Pins via M5-Bus
    // Option 1: M5-Bus Pin 13 (RXD0 = G38) / Pin 14 (TXD0 = G37)
    // Option 2: M5-Bus Pin 15 (PC_RX = G7) / Pin 16 (PC_TX = G6)
    constexpr int GNSS_RX_PIN_PRIMARY = 38;
    constexpr int GNSS_TX_PIN_PRIMARY = 37;
    constexpr int GNSS_RX_PIN_ALT     = 6;
    constexpr int GNSS_TX_PIN_ALT     = 7;

    // Tab5 Internal & M5-Bus Shared I2C Bus (M5.In_I2C)
    constexpr int SYS_I2C_SDA = 31;
    constexpr int SYS_I2C_SCL = 32;

    // Tab5 MicroSD Card Pins (SDMMC / SPI Mode, GPIO 39~44)
    constexpr int SD_SPI_SCK  = 43;
    constexpr int SD_SPI_MOSI = 44;
    constexpr int SD_SPI_MISO = 39;
    constexpr int SD_SPI_CS   = 42;
}

namespace BaudConfig {
    constexpr uint32_t SERIAL_DEBUG_BAUD = 115200;
    constexpr uint32_t GNSS_DEFAULT_BAUD = 38400;
    constexpr uint32_t GNSS_TARGET_BAUD  = 115200;
}

namespace I2cAddrConfig {
    constexpr uint8_t ADDR_BMM150_MAG     = 0x10; // Bypassed due to MagSafe
    constexpr uint8_t ADDR_RX8130CE_RTC   = 0x32; // Tab5 Onboard Hardware RTC
    constexpr uint8_t ADDR_INA226_PRIMARY = 0x40; // Tab5 Onboard Power Monitor
    constexpr uint8_t ADDR_INA226_ALT     = 0x41; // Alternate INA226 address
    constexpr uint8_t ADDR_BMI270_MASTER  = 0x68; // Tab5 Onboard Master IMU
    constexpr uint8_t ADDR_BMI270_SLAVE   = 0x69; // Module GNSS Slave IMU (SDO high)
    constexpr uint8_t ADDR_BMP280_BARO    = 0x76; // Module GNSS Barometer
    constexpr uint8_t ADDR_BMP280_ALT     = 0x77; // Alternate BMP280 address
}

namespace Viewport {
    constexpr int16_t SCREEN_W   = 1280;
    constexpr int16_t SCREEN_H   = 720;

    // Left 160px Resident Touch Dock
    constexpr int16_t DOCK_X         = 0;
    constexpr int16_t DOCK_Y         = 0;
    constexpr int16_t DOCK_W         = 160;
    constexpr int16_t DOCK_H         = 720;

    // 7 Vertical Touch Hitboxes in Left Dock
    constexpr int16_t DOCK_HOME_Y    = 0;
    constexpr int16_t DOCK_HOME_H    = 90;
    constexpr int16_t DOCK_TIME_Y    = 90;
    constexpr int16_t DOCK_TIME_H    = 110;
    constexpr int16_t DOCK_GNSS_Y    = 200;
    constexpr int16_t DOCK_GNSS_H    = 105;
    constexpr int16_t DOCK_WIFI_Y    = 305;
    constexpr int16_t DOCK_WIFI_H    = 105;
    constexpr int16_t DOCK_BATTERY_Y = 410;
    constexpr int16_t DOCK_BATTERY_H = 105;
    constexpr int16_t DOCK_WEATHER_Y = 515;
    constexpr int16_t DOCK_WEATHER_H = 105;
    constexpr int16_t DOCK_TOOLS_Y   = 620;
    constexpr int16_t DOCK_TOOLS_H   = 100;

    // Right Full-Height Main Content Safe Viewport (1120x720)
    constexpr int16_t SAFE_X         = 160;
    constexpr int16_t SAFE_Y         = 0;
    constexpr int16_t SAFE_W         = 1120;
    constexpr int16_t SAFE_H         = 720;

    // Floating OSD Page Indicator
    constexpr int16_t  INDICATOR_OSD_Y        = 690;
    constexpr int16_t  INDICATOR_OSD_H        = 20;
    constexpr uint32_t INDICATOR_AUTO_HIDE_MS = 3000;
}

namespace TripResetConfig {
    // Page 1 Dashboard bottom-right TRIP card hitbox (1280x720 coordinates)
    constexpr int16_t  HITBOX_TRIP_X         = 740;
    constexpr int16_t  HITBOX_TRIP_Y         = 460;
    constexpr int16_t  HITBOX_TRIP_W         = 500;
    constexpr int16_t  HITBOX_TRIP_H         = 220;
    constexpr uint32_t LONG_PRESS_MS         = 2000;
    constexpr uint32_t VIBRATION_FEEDBACK_MS = 100;
}

namespace Ina226Config {
    constexpr uint8_t  I2C_ADDR            = 0x40;
    constexpr float    SHUNT_RESISTOR_OHM  = 0.02f;
    constexpr uint32_t SAMPLE_INTERVAL_MS  = 500;

    constexpr uint8_t  REG_CONFIG          = 0x00;
    constexpr uint8_t  REG_SHUNTVOLTAGE    = 0x01;
    constexpr uint8_t  REG_BUSVOLTAGE      = 0x02;
    constexpr uint8_t  REG_POWER           = 0x03;
    constexpr uint8_t  REG_CURRENT         = 0x04;
    constexpr uint8_t  REG_CALIBRATION     = 0x05;
    constexpr uint8_t  REG_MANUF_ID        = 0xFE;
    constexpr uint8_t  REG_DIE_ID          = 0xFF;

    constexpr uint16_t CALIBRATION_VAL     = 2560; // 0x0A00
    constexpr float    CURRENT_LSB_MA      = 0.1f;
    constexpr float    POWER_LSB_MW        = 2.5f;
    constexpr float    BUS_VOLT_LSB_MV     = 1.25f;
    constexpr uint16_t CONFIG_WORD_DEFAULT = 0x4127;
}

namespace CoulombCounterConfig {
    constexpr float    BATTERY_RATED_MAH     = 2000.0f;
    constexpr float    CHARGE_EFFICIENCY_ETA = 0.95f;
    constexpr float    DISCHARGE_EFFICIENCY  = 1.00f;
    constexpr float    STANDSTILL_CURRENT_MA = 20.0f;
    constexpr uint32_t STANDSTILL_SETTLE_MS  = 5000;
    constexpr float    CUTOFF_VOLTAGE_V      = 6.00f;
    constexpr float    FULL_VOLTAGE_V        = 8.40f;
}

struct OcvLookupPoint {
    uint16_t voltageMv;
    uint8_t  socPercent;
};

constexpr OcvLookupPoint OCV_TABLE[11] = {
    { 8400, 100 },
    { 8200,  90 },
    { 8000,  80 },
    { 7800,  70 },
    { 7640,  60 },
    { 7520,  50 },
    { 7400,  40 },
    { 7300,  30 },
    { 7160,  20 },
    { 6900,  10 },
    { 6000,   0 }
};

namespace PowerColorConfig {
    constexpr uint16_t COLOR_EXTERNAL_POWER_GREEN = 0x07F5; // #00FFAA Fluorescent Cyan-Green
    constexpr uint16_t COLOR_BATTERY_ONLY_RED     = 0xF9E7; // #FF3B30 Alert Red
}

namespace GestureConfig {
    constexpr int16_t SWIPE_HORIZ_THRESHOLD_PX = 60;
    constexpr float   DIRECTION_RATIO          = 1.5f;
}

namespace MotionPowerConfig {
    constexpr uint32_t HOURLY_SPLIT_POINTS         = 3600;
    constexpr uint32_t STANDSTILL_TIMEOUT_MS       = 1200000; // 20 min
    constexpr uint32_t STANDSTILL_STOP_RECORD_MS   = 1200000; // 20 min
    constexpr uint32_t SILENT_ASSESS_WINDOW_MS     = 3000;
    constexpr float    STATIC_VARIANCE_THRESH      = 0.005f;
    constexpr float    SUSTAINED_ACCEL_THRESH      = 0.12f;
    constexpr uint32_t SUSTAINED_ACCEL_MS          = 1500;
    constexpr uint16_t BATTERY_CAPACITY_MAH        = 2000;
    constexpr float    BATTERY_CHARGE_MAX_TEMP_C   = 55.0f;
    constexpr float    BATTERY_CHARGE_RESUME_TEMP  = 45.0f;

    constexpr float    TRACK_STORAGE_QUOTA_RATIO   = 0.50f;
    constexpr float    TRACK_RECOVERY_QUOTA_RATIO  = 0.40f;
    constexpr float    KINEMATIC_MAX_SPEED_KMH     = 180.0f;
    constexpr float    KINEMATIC_MAX_ACCEL_G       = 1.2f;

    constexpr bool     BYPASS_GEOMAGNETIC_BMM150   = true;
    constexpr float    STANDSTILL_HEADING_LOCK_KMH = 1.8f;
    constexpr float    RECORD_TRIGGER_SPEED_KMH    = 3.0f;
    constexpr float    MAX_HDOP_FOR_RECORD         = 2.5f;
}

namespace DualImuConfig {
    constexpr uint8_t  I2C_ADDR_MASTER           = 0x68;
    constexpr uint8_t  I2C_ADDR_SLAVE            = 0x69;
    constexpr float    CROSS_CHECK_MAX_GYRO_DIFF = 0.087f; // ~5 deg/s
    constexpr float    CROSS_CHECK_MAX_ACCEL_DIFF= 0.35f;  // ~0.35 G
    constexpr uint32_t FAILOVER_TIMEOUT_MS       = 20;
    constexpr float    DUAL_FUSION_WEIGHT_MASTER = 0.50f;
    constexpr float    DUAL_FUSION_WEIGHT_SLAVE  = 0.50f;
}

namespace StorageConfig {
    constexpr const char* TRACKS_DIR           = "/tracks";
    constexpr const char* TUNNEL_LITTLEFS_PATH = "/littlefs/tunnels.bin";
    constexpr uint32_t    SYNC_INTERVAL_POINTS = 10;
}

namespace UiColors {
    // Pure Dark Dashboard Theme (RGB565)
    constexpr uint16_t COLOR_BG        = 0x0000; // #000000 Pure Black
    constexpr uint16_t COLOR_PANEL_BG  = 0x10A2; // #121518 Dark Slate
    constexpr uint16_t COLOR_CARD_BG   = 0x0861; // #080C08 Deep Card Background
    constexpr uint16_t COLOR_BORDER    = 0x2965; // #283038 Subtle Border
    constexpr uint16_t COLOR_PRIMARY   = 0x07F5; // #00FFAA Fluorescent Cyan-Green
    constexpr uint16_t COLOR_SECONDARY = 0xFEC0; // #FFD700 Racing Yellow
    constexpr uint16_t COLOR_WHITE     = 0xFFFF; // #FFFFFF Pure White
    constexpr uint16_t COLOR_WARN      = 0xFD20; // #FFA500 Amber Orange
    constexpr uint16_t COLOR_DANGER    = 0xF9E7; // #FF3B30 Alert Red
    constexpr uint16_t COLOR_MUTED     = 0x7BEF; // #7B7D7B Medium Gray
    constexpr uint16_t COLOR_BLUE      = 0x3D7F; // #38AEFF Ice Blue
}
