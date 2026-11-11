#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

// =============================================================================
// M5Track-Tab5 Authoritative Hardware, Protocol & UI Configuration Contract
// Target: M5Stack Tab5 (ESP32-P4 Dual RISC-V, 16MB QIO Flash, 32MB HEX PSRAM)
//         + M5Stack Module GNSS (SKU: M135, u-blox NEO-M9N + BMI270 + BMP280)
// =============================================================================

// -----------------------------------------------------------------------------
// 1. System I2C Bus & Orthogonal 7-bit Peripheral Addresses (SYS_I2C)
// -----------------------------------------------------------------------------
namespace SysI2cConfig {
    constexpr int8_t   PIN_SDA      = 31;       // Tab5 SYS_I2C SDA (GPIO 31)
    constexpr int8_t   PIN_SCL      = 32;       // Tab5 SYS_I2C SCL (GPIO 32)
    constexpr uint32_t FREQ_HZ      = 400000UL; // 400 kHz Fast-Mode I2C

    // --- Authoritative Tab5 + M135 I2C Address Map ---
    // WARNING [0x10]: On Tab5 main SYS_I2C, 0x10 is the onboard ES8388 Audio Codec!
    // M135's BMM150 (also 0x10) sits behind M135 BMI270 (0x69) secondary AUX I2C bus
    // and is completely bypassed due to MagSafe magnetic interference.
    // NEVER probe or write magnetometer registers to 0x10 on SYS_I2C!
    constexpr uint8_t  ADDR_ES8388_AUDIO_DO_NOT_TOUCH = 0x10;

    constexpr uint8_t  ADDR_RX8130CE_RTC  = 0x32; // Tab5 onboard hardware RTC (supercap backup)

    // WARNING [0x40]: On Tab5 main SYS_I2C, 0x40 is the onboard ES7210 4-ch Mic ADC!
    // NEVER write INA226 configuration/calibration to 0x40!
    constexpr uint8_t  ADDR_ES7210_MIC_DO_NOT_TOUCH   = 0x40;

    constexpr uint8_t  ADDR_INA226        = 0x41; // Tab5 onboard INA226 Power Monitor (A0 pulled high, R_SHUNT=5mOhm)
    constexpr uint8_t  ADDR_PI4IOE_1      = 0x43; // Tab5 IO Expander U6 (P1=SPK_EN, P2=EXT5V_EN, P4=LCD_RST, P5=TP_RST)
    constexpr uint8_t  ADDR_PI4IOE_2      = 0x44; // Tab5 IO Expander U7 (P0=WLAN_PWR_EN, P3=USB5V_EN, P6=CHG_STAT, P7=CHG_EN)
    constexpr uint8_t  ADDR_M24C32_EEPROM = 0x50; // M135 optional onboard EEPROM
    constexpr uint8_t  ADDR_BMI270_MASTER = 0x68; // Tab5 onboard 6-axis Master IMU (CHIP_ID=0x24)
    constexpr uint8_t  ADDR_BMI270_SLAVE  = 0x69; // M135 onboard 6-axis Slave IMU (SDO pulled high on PCB, CHIP_ID=0x24)
    constexpr uint8_t  ADDR_BMP280        = 0x76; // M135 onboard Barometer (CHIP_ID=0x58)
}

// PI4IOE5V6408 Register & Bit Definitions (Note: 0x07 OUT_HIGH_IMPEDANCE defaults to 0xFF!)
namespace Pi4IoeConfig {
    constexpr uint8_t REG_DEVICE_ID     = 0x01;
    constexpr uint8_t REG_IO_DIR        = 0x03; // 0=Input, 1=Output
    constexpr uint8_t REG_OUT_SET       = 0x05; // Output state
    constexpr uint8_t REG_OUT_HIGH_Z    = 0x07; // 0=Push-Pull active, 1=High-Z (MUST clear bit to 0 when enabling output!)
    constexpr uint8_t REG_PULL_ENABLE   = 0x0B;
    constexpr uint8_t REG_PULL_SELECT   = 0x0D;
    constexpr uint8_t REG_IN_STATE      = 0x0F;

    // U6 (0x43) Pins
    constexpr uint8_t U6_WIFI_ANT_SEL   = (1u << 0);
    constexpr uint8_t U6_SPK_EN         = (1u << 1);
    constexpr uint8_t U6_EXT5V_EN       = (1u << 2); // M5-Bus 5V & Port.A 5V power rail
    constexpr uint8_t U6_LCD_RST        = (1u << 4);
    constexpr uint8_t U6_TP_RST         = (1u << 5);
    constexpr uint8_t U6_CAM_RST        = (1u << 6);
    constexpr uint8_t U6_HP_DET         = (1u << 7);

    // U7 (0x44) Pins
    constexpr uint8_t U7_WLAN_PWR_EN    = (1u << 0); // ESP32-C6 3.3V power gate (must be HIGH before WiFi)
    constexpr uint8_t U7_USB5V_EN       = (1u << 3);
    constexpr uint8_t U7_PWROFF_PULSE   = (1u << 4);
    constexpr uint8_t U7_N_CHG_QC_EN    = (1u << 5);
    constexpr uint8_t U7_CHG_STAT       = (1u << 6); // IP2326 charge status input
    constexpr uint8_t U7_CHG_EN         = (1u << 7); // IP2326 charge enable output
}

// -----------------------------------------------------------------------------
// 2. Module GNSS (SKU: M135 / u-blox NEO-M9N SPG 4.04) UART & UBX Configuration
// -----------------------------------------------------------------------------
struct UartPinPair {
    int8_t rxPin; // ESP32-P4 RX <- NEO-M9N TXD
    int8_t txPin; // ESP32-P4 TX -> NEO-M9N RXD
};

namespace GnssConfig {
    // M135 default Port C DIP switch maps to M5-Bus Pin 15 (PC_RX = GPIO 7) & Pin 16 (PC_TX = GPIO 6)
    constexpr int8_t  PIN_RX_PRIMARY   = 7;  // ESP32-P4 RX <- M9N TXD (M5-Bus Pin 15)
    constexpr int8_t  PIN_TX_PRIMARY   = 6;  // ESP32-P4 TX -> M9N RXD (M5-Bus Pin 16)
    constexpr int8_t  PIN_RX_ALT_1     = 48; // Alternate DIP: M5-Bus Pin 22
    constexpr int8_t  PIN_TX_ALT_1     = 47; // Alternate DIP: M5-Bus Pin 23
    constexpr int8_t  PIN_RX_ALT_2     = 38; // Fallback probe: M5-Bus Pin 13 (RX0)
    constexpr int8_t  PIN_TX_ALT_2     = 37; // Fallback probe: M5-Bus Pin 14 (TX0)
    constexpr int8_t  PIN_PPS          = 51; // M135 default PPS DIP -> M5-Bus Pin 26 (GPIO 51)

    constexpr UartPinPair UART_CANDIDATES[] = {
        { PIN_RX_PRIMARY, PIN_TX_PRIMARY },
        { PIN_RX_ALT_1,   PIN_TX_ALT_1   },
        { PIN_RX_ALT_2,   PIN_TX_ALT_2   }
    };
    constexpr size_t   UART_CANDIDATE_COUNT = sizeof(UART_CANDIDATES) / sizeof(UART_CANDIDATES[0]);

    constexpr uint32_t BAUD_INIT            = 38400UL;  // NEO-M9N factory default baudrate
    constexpr uint32_t BAUD_OPERATIONAL     = 115200UL; // High-speed UBX operational baudrate
    constexpr uint8_t  NAV_FREQ_HZ          = 5;        // 5Hz navigation solution rate (200ms)
    constexpr uint16_t MEAS_RATE_MS         = 200;      // 200ms measurement period (100ms for 10Hz)
    constexpr uint32_t MON_RF_POLL_MS       = 2000;     // Poll UBX-MON-RF every 2s

    // Vehicle Dynamic & Deadband Thresholds
    constexpr float    STATIC_DEADBAND_KMH          = 1.8f;    // <= 1.8 km/h (0.50 m/s) locks speed=0 & heading
    constexpr uint8_t  STATIC_HOLD_THRS_CMS         = 50;      // 50 cm/s = 0.50 m/s = 1.8 km/h
    constexpr float    RECORD_START_SPEED_KMH       = 3.0f;    // Speed > 3.0 km/h starts GPX recording
    constexpr float    KINEMATIC_MAX_SPEED_JUMP_KMH = 180.0f;  // Outlier rejection max 1s speed jump
    constexpr float    KINEMATIC_MAX_ACCEL_G        = 1.2f;    // Outlier rejection max transient G

    // Two-Stage Dynamic Elevation / C/N0 Filter & Hot-Start Watchdog
    constexpr int8_t   STAGE1_ACQ_MIN_ELEV_DEG      = 5;       // Acquisition stage min elevation (deg)
    constexpr uint8_t  STAGE1_ACQ_MIN_CNO_DBHZ      = 14;      // Acquisition stage min C/N0 (dBHz)
    constexpr int8_t   STAGE2_TRK_MIN_ELEV_DEG      = 10;      // Tracking stage anti-multipath min elevation (deg)
    constexpr uint8_t  STAGE2_TRK_MIN_CNO_DBHZ      = 20;      // Tracking stage anti-multipath min C/N0 (dBHz)
    constexpr uint8_t  STAGE2_MIN_SATS              = 6;       // Min satellites to enter Stage 2
    constexpr uint32_t STAGE2_STABLE_HOLD_MS        = 10000;   // 10s continuous 3D fix before tightening filter
    constexpr uint32_t HOTSTART_WATCHDOG_MS         = 45000;   // 45s no-fix auto UBX-CFG-RST hotstart

    // UBX-MGA-INI Time & Position Assistance Constants
    constexpr int8_t   MGA_GPS_LEAP_SECONDS         = 18;
    constexpr uint16_t MGA_NTP_TACC_SEC             = 0;
    constexpr uint32_t MGA_NTP_TACC_NS              = 50000000UL; // 50ms NTP uncertainty
    constexpr uint16_t MGA_RTC_TACC_SEC             = 2;          // 2s RTC uncertainty
    constexpr uint32_t MGA_RTC_TACC_NS              = 0;
    constexpr uint32_t MGA_POS_FRESH_MAX_AGE_SEC    = 7200UL;     // 2 hours freshness window
    constexpr uint32_t MGA_POS_ACC_FRESH_CM         = 50000UL;    // 500m accuracy in cm
    constexpr uint32_t MGA_POS_ACC_STALE_CM         = 3000000UL;  // 30km accuracy if older

    // Official NEO-M9N (SPG 4.04) UBX-CFG-VALSET 32-bit Key IDs
    constexpr uint32_t KEY_UART1_BAUDRATE           = 0x40520001UL; // U4
    constexpr uint32_t KEY_UART1OUTPROT_UBX         = 0x10740001UL; // L (bool)
    constexpr uint32_t KEY_UART1OUTPROT_NMEA        = 0x10740002UL; // L (bool)
    constexpr uint32_t KEY_RATE_MEAS                = 0x30210001UL; // U2 (ms)
    constexpr uint32_t KEY_RATE_NAV                 = 0x30210002UL; // U2 (cycles)
    constexpr uint32_t KEY_NAVSPG_DYNMODEL          = 0x20110021UL; // E1 (4 = Automotive)
    constexpr uint32_t KEY_MOT_GNSSSPEED_THRS       = 0x20250038UL; // U1 (0.01 m/s units)
    constexpr uint32_t KEY_NAVSPG_INFIL_MINELEV     = 0x201100A4UL; // I1 (deg)
    constexpr uint32_t KEY_NAVSPG_INFIL_MINCNO      = 0x201100A3UL; // U1 (dBHz)
    constexpr uint32_t KEY_ITFM_ENABLE              = 0x1041000DUL; // L (1 = enable jamming monitor)
    constexpr uint32_t KEY_SIGNAL_GPS_ENA           = 0x1031001FUL; // L
    constexpr uint32_t KEY_SIGNAL_GAL_ENA           = 0x10310021UL; // L
    constexpr uint32_t KEY_SIGNAL_BDS_ENA           = 0x10310022UL; // L
    constexpr uint32_t KEY_SIGNAL_GLO_ENA           = 0x10310025UL; // L
    constexpr uint32_t KEY_MSGOUT_UBX_NAV_PVT_UART1 = 0x20910007UL; // U1
    constexpr uint32_t KEY_MSGOUT_UBX_NAV_SAT_UART1 = 0x20910016UL; // U1
    constexpr uint32_t KEY_MSGOUT_UBX_NAV_DOP_UART1 = 0x20910039UL; // U1
}

// -----------------------------------------------------------------------------
// 3. Dual BMI270 Redundant IMU, BMP280 Baro & Tunnel Constraint Constants
// -----------------------------------------------------------------------------
namespace DualImuConfig {
    constexpr uint8_t  I2C_ADDR_MASTER            = SysI2cConfig::ADDR_BMI270_MASTER; // 0x68
    constexpr uint8_t  I2C_ADDR_SLAVE             = SysI2cConfig::ADDR_BMI270_SLAVE;  // 0x69
    constexpr uint8_t  CHIP_ID_BMI270             = 0x24;
    constexpr uint16_t SAMPLE_RATE_HZ             = 50;      // 50Hz (20ms interval)
    constexpr uint32_t SAMPLE_INTERVAL_MS         = 20;
    constexpr float    CROSS_CHECK_MAX_GYRO_DIFF  = 0.087f;  // ~5 deg/s rad/s Z-gyro tolerance
    constexpr float    CROSS_CHECK_MAX_ACCEL_DIFF = 0.35f;   // 0.35 G accel cross-check tolerance
    constexpr uint32_t FAILOVER_TIMEOUT_MS        = 20;      // 20ms master->slave hot failover
    constexpr float    DUAL_FUSION_WEIGHT_MASTER  = 0.50f;
    constexpr float    DUAL_FUSION_WEIGHT_SLAVE   = 0.50f;
    constexpr bool     BYPASS_GEOMAGNETIC_BMM150  = true;    // Bypassed permanently for MagSafe immunity
}

namespace BaroFusionConfig {
    constexpr uint8_t  I2C_ADDR                   = SysI2cConfig::ADDR_BMP280; // 0x76
    constexpr uint8_t  CHIP_ID_BMP280             = 0x58;
    constexpr uint8_t  REG_CTRL_MEAS_VAL          = 0x57;    // osrs_t=x2, osrs_p=x16, Normal mode
    constexpr uint8_t  REG_CONFIG_VAL             = 0x10;    // filter=16
    constexpr uint16_t SAMPLE_RATE_HZ             = 10;      // 10Hz (100ms)
    constexpr uint32_t SAMPLE_INTERVAL_MS         = 100;
    constexpr float    DEFAULT_SEA_LEVEL_HPA      = 1013.25f;
    constexpr float    P0_CALIB_ALPHA             = 0.005f;  // Slow sea-level pressure tracking gain
    constexpr float    P0_CALIB_MAX_HDOP          = 1.5f;
    constexpr float    P0_INITIAL_MAX_HDOP        = 8.0f;
    constexpr float    BARO_DIFF_WEIGHT           = 0.75f;   // 75% baro delta + 25% GNSS anchor
    constexpr float    STEEP_SLOPE_WARN_PERCENT   = 7.0f;    // >= 7.0% triggers steep slope warning
}

namespace TunnelConfig {
    constexpr float    ENTRY_MATCH_RADIUS_M       = 200.0f;  // 200m tunnel portal capture radius
    constexpr float    ENTRY_MAX_HEADING_DIFF_DEG = 35.0f;   // Heading match tolerance
    constexpr uint32_t EXIT_SMOOTH_RECOVERY_MS    = 2000;    // 2s low-pass recovery after tunnel exit
}

// -----------------------------------------------------------------------------
// 4. Tab5 Onboard INA226 (0x41, 5mOhm), RX8130CE RTC & NP-F550 Power Management
// -----------------------------------------------------------------------------
namespace Ina226Config {
    constexpr uint8_t  I2C_ADDR            = SysI2cConfig::ADDR_INA226; // MUST BE 0x41 (NOT 0x40!)
    constexpr float    SHUNT_RESISTOR_OHM  = 0.005f; // Authoritative Tab5 physical R_shunt = 5 mOhm
    constexpr uint32_t SAMPLE_INTERVAL_MS  = 500;    // 2Hz (500ms) Coulomb integration period

    // Register Map (16-bit Big-Endian)
    constexpr uint8_t  REG_CONFIG          = 0x00;
    constexpr uint8_t  REG_SHUNT_VOLTAGE   = 0x01;   // 2.5 uV / LSB
    constexpr uint8_t  REG_BUS_VOLTAGE     = 0x02;   // 1.25 mV / LSB
    constexpr uint8_t  REG_POWER           = 0x03;   // 25 * Current_LSB = 2.5 mW / LSB
    constexpr uint8_t  REG_CURRENT         = 0x04;   // Current_LSB = 0.1 mA / LSB
    constexpr uint8_t  REG_CALIBRATION     = 0x05;
    constexpr uint8_t  REG_MASK_ENABLE     = 0x06;
    constexpr uint8_t  REG_ALERT_LIMIT     = 0x07;
    constexpr uint8_t  REG_MANUF_ID        = 0xFE;   // Expected: 0x5449 ('TI')
    constexpr uint8_t  REG_DIE_ID          = 0xFF;   // Expected: 0x2260

    // CAL = floor(0.00512 / (0.0001 A * 0.005 Ohm)) = 10240 (0x2800)
    constexpr uint16_t CALIBRATION_VAL     = 10240;  // 0x2800
    constexpr float    CURRENT_LSB_MA      = 0.1f;   // 0.1 mA / bit
    constexpr float    POWER_LSB_MW        = 2.5f;   // 2.5 mW / bit
    constexpr float    BUS_VOLT_LSB_MV     = 1.25f;  // 1.25 mV / bit
    constexpr float    SHUNT_VOLT_LSB_UV   = 2.5f;   // 2.5 uV / bit
    constexpr uint16_t CONFIG_WORD_DEFAULT = 0x4127; // Avg=16, VbusCT=1.1ms, VshCT=1.1ms, Continuous
    constexpr uint16_t EXPECTED_MANUF_ID   = 0x5449;
    constexpr uint16_t EXPECTED_DIE_ID     = 0x2260;
}

namespace RtcRx8130Config {
    constexpr uint8_t  I2C_ADDR            = SysI2cConfig::ADDR_RX8130CE_RTC; // 0x32
    constexpr uint8_t  REG_SEC             = 0x10; // Probe 0x10 (NOT 0x00)
    constexpr uint8_t  REG_MIN             = 0x11;
    constexpr uint8_t  REG_HOUR            = 0x12;
    constexpr uint8_t  REG_WEEK_ONEHOT     = 0x13; // MUST be (1u << (wday % 7)), NEVER 0x00!
    constexpr uint8_t  REG_DAY             = 0x14;
    constexpr uint8_t  REG_MONTH           = 0x15;
    constexpr uint8_t  REG_YEAR            = 0x16;
    constexpr uint8_t  REG_EXTENSION       = 0x1C;
    constexpr uint8_t  REG_FLAG            = 0x1D;
    constexpr uint8_t  FLAG_VLF_BIT        = 0x02; // Voltage Low Flag (bit 1)
    constexpr uint8_t  REG_CTRL0           = 0x1E;
    constexpr uint8_t  CTRL0_STOP_BIT      = 0x40; // STOP bit (bit 6)
    constexpr uint8_t  REG_CTRL1           = 0x1F; // Supercap backup charge control - preserve!
}

struct OcvLookupPoint {
    uint16_t voltageMv;  // 2S Li-ion open-circuit bus voltage (mV)
    uint8_t  socPercent; // State of Charge (0 ~ 100%)
};

namespace CoulombCounterConfig {
    constexpr float    BATTERY_RATED_MAH     = 2000.0f; // NP-F550 2S 7.4V nominal capacity (mAh)
    constexpr float    CHARGE_EFFICIENCY_ETA = 0.95f;
    constexpr float    DISCHARGE_EFFICIENCY  = 1.00f;
    constexpr float    STANDSTILL_CURRENT_MA = 20.0f;   // |I| < 20mA triggers OCV drift calibration
    constexpr uint32_t STANDSTILL_SETTLE_MS  = 5000;    // 5s quiet current window
    constexpr float    CHARGING_CURRENT_MA   = 15.0f;   // I > +15mA indicates active charging
    constexpr float    CUTOFF_VOLTAGE_V      = 6.00f;   // 2S Li-ion 0% cutoff protection voltage
    constexpr float    FULL_VOLTAGE_V        = 8.40f;   // 2S Li-ion 100% full charge voltage
    constexpr float    NO_BATTERY_VOLT_MAX_V = 4.50f;   // Bus voltage threshold when pack removed

    // 11-Node 2S (7.4V) NP-F550 Li-ion OCV Lookup Table
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
}

namespace MotionPowerConfig {
    constexpr uint32_t STANDSTILL_SLEEP_TIMEOUT_MS = 1200000UL; // 20 min (1200s) standstill -> settle + sync + sleep
    constexpr uint32_t SILENT_ASSESS_WINDOW_MS     = 3000UL;    // 3s wake assessment window
    constexpr float    STATIC_VARIANCE_THRESH      = 0.005f;    // IMU static variance threshold (m/s^2)^2
    constexpr float    SUSTAINED_ACCEL_THRESH_G    = 0.12f;     // Wakeup vehicle motion accel threshold (G)
    constexpr uint32_t SUSTAINED_ACCEL_MS          = 1500UL;    // 1.5s sustained motion wakeup confirmation
    constexpr uint32_t ACTIVE_CPU_FREQ_MHZ         = 360UL;     // Normal operation CPU clock (MHz)
    constexpr uint32_t STANDBY_CPU_FREQ_MHZ        = 80UL;      // Low-power standby CPU clock (MHz)
    constexpr float    BATTERY_CHARGE_MAX_TEMP_C   = 55.0f;
    constexpr float    BATTERY_CHARGE_RESUME_TEMP  = 45.0f;
}

// -----------------------------------------------------------------------------
// 5. Dedicated TF Card SPI, Tri-Folder Storage & On-Chip LittleFS Configuration
// -----------------------------------------------------------------------------
namespace SdCardConfig {
    // Dedicated Hardware SPI pins for Tab5 MicroSD slot (avoids SDMMC slot contention with ESP32-C6 SDIO)
    constexpr int8_t   PIN_SCK             = 43; // SDMMC CLK  / SPI SCK
    constexpr int8_t   PIN_MISO            = 39; // SDMMC DAT0 / SPI MISO
    constexpr int8_t   PIN_MOSI            = 44; // SDMMC CMD  / SPI MOSI
    constexpr int8_t   PIN_CS              = 42; // SDMMC DAT3 / SPI CS
    constexpr int8_t   PIN_DAT1_PULLUP     = 40; // Unused in SPI mode -> set INPUT_PULLUP
    constexpr int8_t   PIN_DAT2_PULLUP     = 41; // Unused in SPI mode -> set INPUT_PULLUP

    constexpr uint32_t SPI_FREQ_MHZ        = 25; // Primary 25 MHz dedicated SPI clock
    constexpr uint32_t SPI_FALLBACK_MHZ    = 16; // Fallback 16 MHz clock for older cards

    // Tri-Folder Hierarchy & System Files on MicroSD
    constexpr const char* DIR_TRACKS_ROOT     = "/tracks";
    constexpr const char* DIR_SINGLE          = "/tracks/single";
    constexpr const char* DIR_DAILY           = "/tracks/daily";
    constexpr const char* DIR_SUB             = "/tracks/sub";
    constexpr const char* FILE_TRIPS_CSV      = "/tracks/trips.csv";
    constexpr const char* FILE_POWER_LOG      = "/tracks/power.log";
    constexpr const char* FILE_POWER_TMP      = "/tracks/power.tmp";
    constexpr const char* FILE_SYNCED_LEDGER  = "/tracks/.synced_ledger";

    constexpr const char* TRIPS_CSV_HEADER =
        "Start Time,End Time,Category,Distance(km),Drive Time,Idle Time,"
        "Avg Speed(km/h),Max Speed(km/h),Climb(m),Max Alt(m),Min Alt(m),"
        "Max Lat G,Max Long G,GPX File";

    // Recording Split, Quota & FIFO Limits
    constexpr uint32_t HOURLY_SPLIT_POINTS        = 3600UL;                 // 1h (3600 pts @ 1Hz) per single-trip file
    constexpr uint32_t SUB_TRIP_MAX_VOLUME_BYTES  = 10UL * 1024UL * 1024UL; // 10 MiB (10,485,760 B) strict split
    constexpr uint32_t GPX_SYNC_INTERVAL_MS       = 10000UL;                // Periodic 10s file.sync()
    constexpr float    TRACK_STORAGE_QUOTA_RATIO  = 0.50f;                  // Hard cap: <= 50% of TF total bytes
    constexpr float    TRACK_RECOVERY_QUOTA_RATIO = 0.40f;                  // Evict oldest W/C pairs down to <= 40%
    constexpr size_t   POWER_LOG_MAX_BYTES        = 100UL * 1024UL;         // 100 KiB (102,400 B) FIFO cap
    constexpr size_t   POWER_LOG_TRIM_BYTES       = 20UL * 1024UL;          // Trim oldest ~20 KiB on overflow
    constexpr float    MIN_LOG_TRIP_DIST_KM       = 0.05f;                  // Debounce < 50m micro-movements
    constexpr uint32_t MIN_LOG_TRIP_MOVING_SEC    = 10UL;                   // Debounce < 10s micro-movements
}

namespace LittleFsConfig {
    // Verified against local Arduino-ESP32 LittleFS.h & partitions.csv:
    // partitions.csv entry: Name="spiffs", Type="data", SubType="littlefs" (0x83), Size=0x360000 (3.375 MiB)
    // Calling LittleFS.begin(true) or LittleFS.begin(true, BASE_PATH, 10, PARTITION_LABEL) mounts cleanly.
    constexpr const char* BASE_PATH       = "/littlefs";
    constexpr const char* PARTITION_LABEL = "spiffs";
    constexpr const char* TUNNELS_BIN_REL = "/tunnels.bin";          // Pass to LittleFS.open()
    constexpr const char* TUNNELS_BIN_VFS = "/littlefs/tunnels.bin"; // Full POSIX VFS path
}

// -----------------------------------------------------------------------------
// 6. ESP32-C6 SDIO Wi-Fi 6, Captive Portal, Weather & OneDrive Cloud Constants
// -----------------------------------------------------------------------------
namespace Esp32C6SdioConfig {
    constexpr int8_t   PIN_CLK               = 12;
    constexpr int8_t   PIN_CMD               = 13;
    constexpr int8_t   PIN_D0                = 11;
    constexpr int8_t   PIN_D1                = 10;
    constexpr int8_t   PIN_D2                = 9;
    constexpr int8_t   PIN_D3                = 8;
    constexpr int8_t   PIN_RESET             = 15; // Active-low C6 reset
    constexpr uint32_t POST_POWERON_DELAY_MS = 60; // Wait >= 50ms after M5.begin() enables U7 P0 WLAN_PWR_EN
}

namespace NetConfig {
    constexpr uint8_t     MAX_SAVED_NETWORKS    = 5;
    constexpr uint16_t    DNS_PORT              = 53;
    constexpr uint16_t    HTTP_PORT             = 80;
    constexpr const char* AP_IP_ADDR            = "192.168.4.1";

    // Mode 1: Captive Portal Wi-Fi Provisioning SoftAP
    constexpr const char* AP_SETUP_SSID         = "M5Track-Setup";
    constexpr const char* QR_SETUP_WIFI_PAYLOAD = "WIFI:T:nopass;S:M5Track-Setup;;";
    constexpr const char* QR_SETUP_URL_PAYLOAD  = "http://192.168.4.1/setup";

    // Mode 2: Wireless GPX Track Export SoftAP
    constexpr const char* AP_SHARE_SSID         = "M5Track-Share";
    constexpr const char* QR_SHARE_WIFI_PAYLOAD = "WIFI:T:nopass;S:M5Track-Share;;";
    constexpr const char* QR_SHARE_URL_PAYLOAD  = "http://192.168.4.1/tracks";

    // Multi-Server NTP
    constexpr const char* NTP_SERVER_1          = "ntp.aliyun.com";
    constexpr const char* NTP_SERVER_2          = "cn.pool.ntp.org";
    constexpr const char* NTP_SERVER_3          = "pool.ntp.org";
    constexpr uint32_t    WIFI_RECONNECT_MS     = 15000UL;
}

namespace CloudConfig {
    // 1. u-blox AssistNow Online (MUST use semicolon ';' delimiter!)
    constexpr const char* ASSISTNOW_HOST_PRIMARY = "online-live1.services.u-blox.com";
    constexpr const char* ASSISTNOW_HOST_BACKUP  = "online-live2.services.u-blox.com";
    constexpr const char* ASSISTNOW_PATH         = "/GetOnlineData.ashx";
    constexpr size_t      ASSISTNOW_CHUNK_SIZE   = 512;

    // 2. Open-Meteo v1 Forecast REST API (snake_case fields, no API key required)
    constexpr const char* OPEN_METEO_ENDPOINT    = "https://api.open-meteo.com/v1/forecast";
    constexpr uint32_t    WEATHER_UPDATE_MS      = 900000UL; // Refresh every 15 minutes

    // 3. Microsoft Identity Platform (RFC 8628 Device Code Flow) & Graph OneDrive API
    constexpr const char* ONEDRIVE_DEFAULT_TENANT   = "common";
    constexpr const char* ONEDRIVE_DEFAULT_CLIENT_ID = "";
    constexpr const char* ONEDRIVE_SCOPE            = "Files.ReadWrite offline_access User.Read";
    constexpr const char* ONEDRIVE_AUTH_HOST        = "login.microsoftonline.com";
    constexpr const char* ONEDRIVE_GRAPH_HOST       = "graph.microsoft.com";
    constexpr const char* ONEDRIVE_REMOTE_ROOT      = "/VehicleTracks";

    // Graph Upload Session Rules:
    // - Files < 4 MiB: single PUT .../content (with Authorization: Bearer header)
    // - Files >= 4 MiB: POST .../createUploadSession, then PUT chunks to pre-signed uploadUrl
    //   WITHOUT Authorization header; each non-final chunk MUST be a multiple of 327,680 bytes (320 KiB)!
    constexpr size_t      ONEDRIVE_DIRECT_PUT_MAX_BYTES = 4UL * 1024UL * 1024UL; // 4 MiB
    constexpr size_t      ONEDRIVE_CHUNK_UNIT_BYTES     = 327680UL;              // 320 KiB mandatory multiple
    constexpr size_t      ONEDRIVE_UPLOAD_CHUNK_BYTES   = 327680UL;              // 1x 320 KiB per chunk
    constexpr uint32_t    ONEDRIVE_PRE_SLEEP_TIMEOUT_MS = 25000UL;               // 25s pre-standby sync cap
    constexpr uint32_t    ONEDRIVE_BG_SYNC_INTERVAL_MS  = 60000UL;               // Check unsynced ledger every 60s
}

// -----------------------------------------------------------------------------
// 7. Audio / Tactile Feedback via Onboard Speaker (No Vibration Motor on Tab5!)
// -----------------------------------------------------------------------------
namespace HapticConfig {
    // Tab5 has NO vibration motor; audio-tactile confirmation uses ES8388 + NS4150B via M5.Speaker.tone()
    constexpr uint8_t  SPEAKER_DEFAULT_VOL   = 160;  // 0 ~ 255
    constexpr uint16_t TONE_TAP_HZ           = 1850; // Crisp dock/button tap click
    constexpr uint32_t TONE_TAP_MS           = 20;
    constexpr uint16_t TONE_SWIPE_HZ         = 1400; // Subtle page transition tick
    constexpr uint32_t TONE_SWIPE_MS         = 15;
    constexpr uint16_t TONE_CONFIRM_1_HZ     = 2000; // Rising dual-tone confirm
    constexpr uint16_t TONE_CONFIRM_2_HZ     = 2650;
    constexpr uint32_t TONE_CONFIRM_MS       = 55;
    constexpr uint16_t TONE_TRIP_RESET_HZ    = 2400; // 2s long-press TRIP reset chime
    constexpr uint32_t TONE_TRIP_RESET_MS    = 120;
    constexpr uint16_t TONE_WARN_HZ          = 1100; // Steep slope / RF jam / low-batt alert
    constexpr uint32_t TONE_WARN_MS          = 140;
}

// -----------------------------------------------------------------------------
// 8. Display Viewport, Left Dock, Hitboxes & Pure Dark Automotive Palette
// -----------------------------------------------------------------------------
namespace Viewport {
    constexpr uint8_t  LANDSCAPE_ROTATION = 1;    // M5.Display.setRotation(1) -> 1280x720
    constexpr int16_t  SCREEN_W           = 1280;
    constexpr int16_t  SCREEN_H           = 720;

    // Permanent Left Touch Dock (0..159 x 0..719)
    constexpr int16_t  DOCK_X             = 0;
    constexpr int16_t  DOCK_Y             = 0;
    constexpr int16_t  DOCK_W             = 160;
    constexpr int16_t  DOCK_H             = 720;

    // 7 Dedicated Vertical Touch Zones inside Left Dock
    constexpr int16_t  DOCK_HOME_Y        = 0;
    constexpr int16_t  DOCK_HOME_H        = 90;
    constexpr int16_t  DOCK_TIME_Y        = 90;
    constexpr int16_t  DOCK_TIME_H        = 110;
    constexpr int16_t  DOCK_GNSS_Y        = 200;
    constexpr int16_t  DOCK_GNSS_H        = 105;
    constexpr int16_t  DOCK_WIFI_Y        = 305;
    constexpr int16_t  DOCK_WIFI_H        = 105;
    constexpr int16_t  DOCK_BATTERY_Y     = 410;
    constexpr int16_t  DOCK_BATTERY_H     = 105;
    constexpr int16_t  DOCK_WEATHER_Y     = 515;
    constexpr int16_t  DOCK_WEATHER_H     = 105;
    constexpr int16_t  DOCK_TOOLS_Y       = 620;
    constexpr int16_t  DOCK_TOOLS_H       = 100;

    // Right Full-Height Main Content Safe Viewport (160..1279 x 0..719)
    constexpr int16_t  SAFE_X             = 160;
    constexpr int16_t  SAFE_Y             = 0;
    constexpr int16_t  SAFE_W             = 1120;
    constexpr int16_t  SAFE_H             = 720;

    // Floating Bottom Page Indicator Dots (auto-hides after 3s idle)
    constexpr int16_t  INDICATOR_OSD_Y        = 690;
    constexpr int16_t  INDICATOR_OSD_H        = 20;
    constexpr uint32_t INDICATOR_AUTO_HIDE_MS = 3000UL;
}

namespace GestureConfig {
    constexpr int16_t  SWIPE_HORIZ_THRESHOLD_PX = 60;
    constexpr float    DIRECTION_RATIO          = 1.5f;
    constexpr uint32_t TAP_MAX_DURATION_MS      = 400UL;
    constexpr int16_t  TAP_MAX_JITTER_PX        = 24;
}

namespace TripResetConfig {
    // Page 1 Dashboard Bottom-Right TRIP Card Hitbox:
    // Screen coordinates (1280x720) & Canvas-local coordinates (1120x720, relative to SAFE_X=160)
    constexpr int16_t  HITBOX_SCREEN_X   = 740;
    constexpr int16_t  HITBOX_SCREEN_Y   = 460;
    constexpr int16_t  HITBOX_LOCAL_X    = HITBOX_SCREEN_X - Viewport::SAFE_X; // 580
    constexpr int16_t  HITBOX_LOCAL_Y    = 460;
    constexpr int16_t  HITBOX_W          = 500;
    constexpr int16_t  HITBOX_H          = 220;
    constexpr uint32_t LONG_PRESS_MS     = 2000UL; // Hold 2.0s to trigger full-system trip reset
}

namespace ThemeColors {
    // 24-bit RGB888 Colors (M5GFX converts automatically or use RGB565 helpers below)
    constexpr uint32_t BG_PURE_BLACK        = 0x000000U; // Pure dark night-vision background
    constexpr uint32_t BG_CARD_DARK         = 0x0D131AU; // Subtle instrument card background
    constexpr uint32_t BG_CARD_HIGHLIGHT    = 0x16202CU; // Pressed/active card surface
    constexpr uint32_t BORDER_SUBTLE        = 0x243242U; // Crisp thin card border
    constexpr uint32_t NEON_GREEN           = 0x00FFAAU; // Primary gauge readout & External Power GREEN ⚡
    constexpr uint32_t RACING_YELLOW        = 0xFFD700U; // Secondary accent / speed highlights
    constexpr uint32_t ICE_CYAN             = 0x00D4FFU; // Min-altitude / UTC / cold telemetry
    constexpr uint32_t TEXT_WHITE           = 0xFFFFFFU; // Crisp primary labels
    constexpr uint32_t TEXT_MUTED           = 0x8B949EU; // Units & secondary captions
    constexpr uint32_t AMBER_WARN           = 0xFFA500U; // Caution / DST / moderate alert
    constexpr uint32_t ALERT_RED            = 0xFF3B30U; // Battery-only RED 🔋 & critical warnings

    // Precomputed RGB565 equivalents for direct 16-bit sprite fast-paths
    constexpr uint16_t RGB565_BLACK         = 0x0000U;
    constexpr uint16_t RGB565_NEON_GREEN    = 0x07F5U; // #00FFAA
    constexpr uint16_t RGB565_RACING_YELLOW = 0xFEA0U; // #FFD700
    constexpr uint16_t RGB565_ICE_CYAN      = 0x06BFU; // #00D4FF
    constexpr uint16_t RGB565_WHITE         = 0xFFFFU; // #FFFFFF
    constexpr uint16_t RGB565_AMBER_WARN    = 0xFD20U; // #FFA500
    constexpr uint16_t RGB565_ALERT_RED     = 0xF9C6U; // #FF3B30

    // Battery / External Power Dual-Color Mandate (PRD §3.1 & §3.8)
    constexpr uint32_t POWER_EXT_GREEN      = NEON_GREEN; // External USB-C / HVIN present -> GREEN
    constexpr uint32_t POWER_BATT_ONLY_RED  = ALERT_RED;  // Pure NP-F550 battery discharge -> RED
}

// -----------------------------------------------------------------------------
// 9. FreeRTOS Multi-Core Task Allocation (ESP32-P4 Dual-Core RISC-V)
// -----------------------------------------------------------------------------
namespace TaskConfig {
    // Core 0: High-rate sensor/GNSS ingestion & async ESP32-C6 network/cloud I/O
    constexpr BaseType_t  CORE_GNSS_IMU      = 0;
    constexpr UBaseType_t PRIO_GNSS_IMU      = 5;
    constexpr uint32_t    STACK_GNSS_IMU     = 12288UL;

    constexpr BaseType_t  CORE_NET_CLOUD     = 0;
    constexpr UBaseType_t PRIO_NET_CLOUD     = 2;
    constexpr uint32_t    STACK_NET_CLOUD    = 20480UL;

    // Core 1: 60fps PSRAM UI engine, 1Hz SdFat GPX logger & 2Hz INA226 power monitor
    constexpr BaseType_t  CORE_UI            = 1;
    constexpr UBaseType_t PRIO_UI            = 4;
    constexpr uint32_t    STACK_UI           = 20480UL;
    constexpr uint32_t    UI_FRAME_PERIOD_MS = 16UL; // ~60 FPS

    constexpr BaseType_t  CORE_STORAGE       = 1;
    constexpr UBaseType_t PRIO_STORAGE       = 3;
    constexpr uint32_t    STACK_STORAGE      = 16384UL;

    constexpr BaseType_t  CORE_POWER         = 1;
    constexpr UBaseType_t PRIO_POWER         = 1;
    constexpr uint32_t    STACK_POWER        = 8192UL;
}

// -----------------------------------------------------------------------------
// 10. Non-Volatile Storage (Preferences) Namespace & Standard Key Names
// -----------------------------------------------------------------------------
namespace NvsConfig {
    constexpr const char* NAMESPACE           = "m5track";

    // Wi-Fi Credentials (0..4)
    constexpr const char* KEY_WIFI_COUNT      = "wifi_count";
    constexpr const char* KEY_SSID_PREFIX     = "ssid_";       // ssid_0 .. ssid_4
    constexpr const char* KEY_PWD_PREFIX      = "pwd_";        // pwd_0  .. pwd_4

    // User Preferences
    constexpr const char* KEY_AUTO_RECORD     = "auto_record";
    constexpr const char* KEY_TZ_OFFSET_MIN   = "tz_offset_min";
    constexpr const char* KEY_TZ_AUTO_GPS     = "tz_auto_gps";
    constexpr const char* KEY_SCREEN_BRIGHT   = "screen_bright";
    constexpr const char* KEY_AUTO_BRIGHT     = "auto_bright";
    constexpr const char* KEY_BEEP_ENABLE     = "beep_en";

    // GNSS Warm-Start & Baro Calibration
    constexpr const char* KEY_LAST_LAT_E7     = "last_lat_e7";
    constexpr const char* KEY_LAST_LON_E7     = "last_lon_e7";
    constexpr const char* KEY_LAST_ALT_CM     = "last_alt_cm";
    constexpr const char* KEY_LAST_POS_UTC    = "last_pos_utc";
    constexpr const char* KEY_SEA_P0_HPA      = "sea_p0";

    // IMU Level Zero-Point Offsets
    constexpr const char* KEY_PITCH_ZERO_DEG  = "pitch_zero";
    constexpr const char* KEY_ROLL_ZERO_DEG   = "roll_zero";

    // Three-Tier Trip Persistence
    constexpr const char* KEY_REC_TRIP_MODE   = "rec_trip_mode";
    constexpr const char* KEY_ACT_TRIP_VIEW   = "act_trip_view";
    constexpr const char* KEY_MAN_TRIP_ACT    = "man_trip_act";
    constexpr const char* KEY_SUB_IDX         = "sub_idx";
    constexpr const char* KEY_SUB_SPLIT_IDX   = "sub_s_idx";
    constexpr const char* KEY_LAST_DAY_INT    = "last_day_int";
    constexpr const char* KEY_DAY_DIST_KM     = "day_dist";
    constexpr const char* KEY_MAN_DIST_KM     = "man_dist";

    // Cloud Tokens & OneDrive OAuth State
    constexpr const char* KEY_AGPS_TOKEN      = "agps_token";
    constexpr const char* KEY_OD_CLIENT_ID    = "od_client_id";
    constexpr const char* KEY_OD_TENANT       = "od_tenant";
    constexpr const char* KEY_OD_AUTO_SYNC    = "od_auto_sync";
    constexpr const char* KEY_OD_ACCESS_TOK   = "od_acc_tok";
    constexpr const char* KEY_OD_REFRESH_TOK  = "od_ref_tok";
    constexpr const char* KEY_OD_EXPIRES_UTC  = "od_exp_utc";
    constexpr const char* KEY_OD_USER_EMAIL   = "od_email";
}
