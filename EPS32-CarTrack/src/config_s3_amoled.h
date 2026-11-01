#pragma once
#include <Arduino.h>

// =============================================================================
// Waveshare ESP32-S3-Touch-AMOLED-1.75 Hardware Configuration
// =============================================================================

namespace BoardConfig {
    // 1.75" QSPI AMOLED (CO5300 Controller, 466x466)
    constexpr int16_t  SCREEN_WIDTH   = 466;
    constexpr int16_t  SCREEN_HEIGHT  = 466;
    constexpr uint16_t COL_OFFSET     = 6;
    constexpr uint16_t ROW_OFFSET     = 0;

    constexpr int LCD_CS    = 12;
    constexpr int LCD_PCLK  = 38;
    constexpr int LCD_SDIO0 = 4;
    constexpr int LCD_SDIO1 = 5;
    constexpr int LCD_SDIO2 = 6;
    constexpr int LCD_SDIO3 = 7;
    constexpr int LCD_RST   = 39;
    constexpr int LCD_TE    = 13;

    // Shared Master I2C Bus (SDA=GPIO15, SCL=GPIO14)
    constexpr int I2C_SDA   = 15;
    constexpr int I2C_SCL   = 14;
    constexpr uint32_t I2C_FREQ = 400000; // 400kHz Fast Mode

    // CST9217 Capacitive Touch Controller
    constexpr int TP_RST    = 40;
    constexpr int TP_INT    = 11;

    // Reserved Expansion Header (8-Pin 2.54mm)
    constexpr int EXP_GPIO16 = 16;
    constexpr int EXP_GPIO17 = 17;
    constexpr int EXP_GPIO18 = 18;
}

namespace I2cAddresses {
    constexpr uint8_t ADDR_LC76G_WRITE = 0x50; // Quectel LC76G GNSS Write
    constexpr uint8_t ADDR_LC76G_READ  = 0x54; // Quectel LC76G GNSS Read
    constexpr uint8_t ADDR_AXP2101     = 0x34; // PMIC
    constexpr uint8_t ADDR_PCF85063    = 0x51; // RTC
    constexpr uint8_t ADDR_QMI8658     = 0x6B; // 6-Axis IMU (or 0x6A)
    constexpr uint8_t ADDR_CST9217     = 0x5A; // Touch (or 0x1A)
}

namespace GnssFilterConfig {
    // Stability Convergence Filter Criteria
    constexpr float    MAX_HDOP_FOR_RECORDING    = 1.8f;  // Horizontal Dilution of Precision
    constexpr float    MAX_VDOP_FOR_RECORDING    = 2.2f;  // Vertical Dilution of Precision
    constexpr uint8_t  MIN_SVS_FOR_RECORDING     = 7;     // Minimum satellites in active fix
    constexpr uint32_t STABILITY_REQUIRED_TIME_MS= 3000;  // Must hold criteria for 3 continuous seconds
    constexpr float    STATIC_SPEED_THRESHOLD_KMH= 1.5f;  // Zero-Velocity Clamping threshold (<1.5 km/h)
    constexpr float    MAX_CLIMB_RATE_MPS        = 5.0f;  // Max realistic vertical speed (m/s) for outlier rejection
    constexpr uint32_t NVS_SNAPSHOT_INTERVAL_MS  = 15000; // 15s periodic save for hot-start recovery
}

namespace NetworkConfig {
    constexpr const char* AP_SSID_PREFIX = "CarTrack-AP-";
    constexpr const char* AP_DEFAULT_IP  = "192.168.4.1";
    constexpr const char* NTP_SERVER_1   = "ntp.aliyun.com";
    constexpr const char* NTP_SERVER_2   = "pool.ntp.org";
    constexpr long        TIMEZONE_OFFSET_SEC = 8 * 3600; // UTC+8 China Standard Time
}
