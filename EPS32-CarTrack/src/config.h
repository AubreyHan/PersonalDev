#pragma once
#include <Arduino.h>

// =============================================================================
// M5Track-M9N System Hardware Configuration & Constants
// =============================================================================

namespace PinConfig {
    // Module GNSS (M135) UART2 Pins (DIP Switch configured on Core2)
    // GPIO 14: RX2 (Receives from GNSS Module TXD at G14)
    // GPIO 13: TX2 (Transmits to GNSS Module RXD at G13)
    constexpr int GNSS_RX_PIN = 14;
    constexpr int GNSS_TX_PIN = 13;

    // External M-Bus I2C Pins (Wire1 on Core2)
    // Module GNSS onboard sensors: BMP280 (0x76), BMI270 (0x68), BMM150 (0x10)
    // Base Dual 16340 onboard sensor: INA226 (0x45)
    constexpr int EXT_I2C_SDA = 32;
    constexpr int EXT_I2C_SCL = 33;

    // MicroSD Card SPI Pins (Core2 Hardware SPI)
    constexpr int SD_SPI_SCK  = 18;
    constexpr int SD_SPI_MISO = 38;
    constexpr int SD_SPI_MOSI = 23;
    constexpr int SD_SPI_CS   = 4;
}

namespace BaudConfig {
    constexpr uint32_t SERIAL_DEBUG_BAUD = 115200;
    constexpr uint32_t GNSS_DEFAULT_BAUD = 38400;
    constexpr uint32_t GNSS_TARGET_BAUD  = 115200;
}

namespace GnssConfig {
    constexpr uint8_t  NAV_RATE_HZ           = 5;      // 5Hz NAV-PVT
    constexpr uint8_t  SIG_RATE_HZ           = 1;      // 1Hz NAV-SIG
    constexpr float    MIN_ELEVATION_DEG     = 12.0f;  // 12° elevation mask
    constexpr uint8_t  MIN_CNO_DBHZ          = 24;     // Carrier-to-noise ratio threshold
    constexpr float    STATIC_HOLD_THRESH_MPS= 0.5f;   // 0.5 m/s (~1.8 km/h) static hold
    constexpr float    MAX_PLAUSIBLE_SPEED_KMH= 180.0f;// Outlier filter max speed step
    constexpr float    MAX_PLAUSIBLE_ACCEL_G = 1.2f;   // Outlier filter max accel step
    constexpr float    RECORD_TRIGGER_SPEED_KMH = 3.0f;// Start logging threshold
    constexpr float    MAX_HDOP_FOR_RECORD   = 2.0f;   // Valid 3D Fix threshold
}

namespace StorageConfig {
    constexpr const char* TRACKS_DIR          = "/tracks";
    constexpr const char* TUNNEL_LITTLEFS_PATH= "/littlefs/tunnels.bin";
    constexpr uint32_t    SAFE_WATERMARK_MB   = 200;
    constexpr uint32_t    RECOVER_TARGET_MB   = 500;
    constexpr uint32_t    HOURLY_SPLIT_POINTS = 3600;  // 1 hour at 1Hz
    constexpr uint32_t    SYNC_INTERVAL_POINTS= 10;    // 10s file.sync()
}

namespace UiColors {
    // Pure Dark Dashboard Theme
    constexpr uint16_t COLOR_BG          = 0x0000; // TFT_BLACK #000000
    constexpr uint16_t COLOR_PANEL_BG    = 0x10A2; // Dark Slate Gray #121518
    constexpr uint16_t COLOR_BORDER      = 0x2965; // Subtle Border #283038
    constexpr uint16_t COLOR_PRIMARY     = 0x07F5; // Fluorescent Cyan-Green #00FFAA
    constexpr uint16_t COLOR_SECONDARY   = 0xFEC0; // Racing Yellow #FFD700
    constexpr uint16_t COLOR_WHITE       = 0xFFFF; // Pure White #FFFFFF
    constexpr uint16_t COLOR_WARN        = 0xFD20; // Amber Orange #FFA500
    constexpr uint16_t COLOR_DANGER      = 0xF9E7; // Alert Red #FF3B30
    constexpr uint16_t COLOR_MUTED       = 0x7BEF; // Medium Gray
    constexpr uint16_t COLOR_BLUE        = 0x3D7F; // Sky Blue
}
