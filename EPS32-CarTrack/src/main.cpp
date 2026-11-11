#include <Arduino.h>
#include <LittleFS.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "model/SystemState.h"

// Hardware Drivers
#include "driver/HapticDriver.h"
#include "driver/RtcDriver.h"
#include "driver/Ina226Driver.h"
#include "driver/CompassDriver.h"
#include "driver/BaroDriver.h"
#include "driver/ImuDriver.h"
#include "driver/GnssDriver.h"

// Core Domain Services
#include "service/TimeService.h"
#include "service/PowerMonitorService.h"
#include "service/TunnelService.h"
#include "service/AltitudeFusion.h"
#include "service/DeadReckoningService.h"
#include "service/TrackLogger.h"
#include "service/NetworkManager.h"
#include "service/AgpsService.h"
#include "service/WeatherService.h"
#include "service/OneDriveSyncService.h"

// 60fps PSRAM Off-Screen UI Engine
#include "ui/UiManager.h"

// Ensure Arduino setup()/loopTask has generous stack headroom during hardware bring-up
SET_LOOP_TASK_STACK_SIZE(16384);

namespace {

constexpr uint16_t kGnssOperationalMeasRateMs = 100U; // 10Hz (100ms) PVT + 1Hz DOP/SAT + 2s MON-RF
constexpr uint32_t kGnssImuTickPeriodMs       = 5UL;  // 200Hz non-blocking poll cadence (50Hz IMU/DR, 10Hz GNSS/Baro)
constexpr uint32_t kNetCloudTickPeriodMs      = 20UL; // 50Hz async Web/DNS/SNTP/Cloud state-machine cadence
constexpr uint32_t kStorageTickPeriodMs       = 50UL; // 20Hz command response + 1Hz GPX stream writer
constexpr uint32_t kPowerTickPeriodMs         = 100UL;// 10Hz standby state machine + 2Hz INA226 Coulomb gauge

TaskHandle_t g_gnssImuTaskHandle     = nullptr;
TaskHandle_t g_netCloudTaskHandle    = nullptr;
TaskHandle_t g_uiTaskHandle          = nullptr;
TaskHandle_t g_trackLoggerTaskHandle = nullptr;
TaskHandle_t g_powerTaskHandle       = nullptr;

// -----------------------------------------------------------------------------
// Explicitly assert Tab5 PI4IOE5V6408 power rails:
//   - U6 (0x43) P2 EXT5V_EN    -> Powers M5-Bus 5V rail for Module GNSS (M135)
//   - U7 (0x44) P0 WLAN_PWR_EN -> Powers ESP32-C6 Wi-Fi 6 SDIO co-processor
// Note: PI4IOE5V6408 REG_OUT_HIGH_Z (0x07) defaults to 0xFF; bit MUST be 0!
// Strictly avoids touching 0x10 (ES8388 Audio) or 0x40 (ES7210 Mic ADC).
// -----------------------------------------------------------------------------
void ensureTab5PowerRailsAndSettle() {
    M5.Power.setExtOutput(true);

    if (M5.In_I2C.isEnabled()) {
        const uint32_t freq = SysI2cConfig::FREQ_HZ;

        // U6 (0x43): P2 = EXT5V_EN (M5-Bus 5V rail)
        M5.In_I2C.bitOn(SysI2cConfig::ADDR_PI4IOE_1,  Pi4IoeConfig::REG_IO_DIR,     Pi4IoeConfig::U6_EXT5V_EN,    freq);
        M5.In_I2C.bitOn(SysI2cConfig::ADDR_PI4IOE_1,  Pi4IoeConfig::REG_OUT_SET,    Pi4IoeConfig::U6_EXT5V_EN,    freq);
        M5.In_I2C.bitOff(SysI2cConfig::ADDR_PI4IOE_1, Pi4IoeConfig::REG_OUT_HIGH_Z, Pi4IoeConfig::U6_EXT5V_EN,    freq);

        // U7 (0x44): P0 = WLAN_PWR_EN (ESP32-C6 3.3V power gate)
        M5.In_I2C.bitOn(SysI2cConfig::ADDR_PI4IOE_2,  Pi4IoeConfig::REG_IO_DIR,     Pi4IoeConfig::U7_WLAN_PWR_EN, freq);
        M5.In_I2C.bitOn(SysI2cConfig::ADDR_PI4IOE_2,  Pi4IoeConfig::REG_OUT_SET,    Pi4IoeConfig::U7_WLAN_PWR_EN, freq);
        M5.In_I2C.bitOff(SysI2cConfig::ADDR_PI4IOE_2, Pi4IoeConfig::REG_OUT_HIGH_Z, Pi4IoeConfig::U7_WLAN_PWR_EN, freq);
    }

    // Mandatory >= 60ms power-on reset settle window for ESP32-C6 SDIO & M135 NEO-M9N
    delay(Esp32C6SdioConfig::POST_POWERON_DELAY_MS + 10UL);
}

// -----------------------------------------------------------------------------
// Update BootCheckState progress and render lightweight early boot feedback
// before UiManager takes over full 60fps PSRAM double-buffered rendering.
// -----------------------------------------------------------------------------
void reportBootStage(uint8_t percent, const char* statusMsg) {
    {
        StateGuard guard(SystemState::instance());
        if (guard.locked()) {
            auto& boot = SystemState::instance().data().boot;
            boot.progressPercent = (percent > 100U) ? 100U : percent;
            if (statusMsg != nullptr) {
                strncpy(boot.statusMessage, statusMsg, sizeof(boot.statusMessage) - 1);
                boot.statusMessage[sizeof(boot.statusMessage) - 1] = '\0';
            }
        }
    }

    // Minimal direct progress bar on pure black screen during ~1s hardware probe
    const int16_t barW = 480;
    const int16_t barH = 10;
    const int16_t barX = (Viewport::SCREEN_W - barW) / 2;
    const int16_t barY = (Viewport::SCREEN_H / 2) + 28;
    const int16_t fill = static_cast<int16_t>((static_cast<int32_t>(barW) * percent) / 100);

    M5.Display.fillRect(barX - 40, barY - 36, barW + 80, 28, TFT_BLACK);
    M5.Display.setFont(&fonts::efontCN_16);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(ThemeColors::TEXT_MUTED, TFT_BLACK);
    if (statusMsg != nullptr) {
        M5.Display.drawString(statusMsg, Viewport::SCREEN_W / 2, barY - 20);
    }
    M5.Display.drawRoundRect(barX, barY, barW, barH, 4, ThemeColors::BORDER_SUBTLE);
    if (fill > 0) {
        M5.Display.fillRoundRect(barX, barY, fill, barH, 4, ThemeColors::NEON_GREEN);
    }
}

// -----------------------------------------------------------------------------
// Emit structured [HW_SELF_TEST] diagnostic report over USB Serial (115200)
// Aligned 100% with scripts/check_and_flash.py & #3 hardware truth baseline.
// -----------------------------------------------------------------------------
void printBootSelfTestReport(const BootCheckState& boot) {
    Serial.println();
    Serial.println("==================== [HW_SELF_TEST] M5Track-Tab5 ====================");
    Serial.printf("  Tab5 PSRAM 32MB    (HEX)  : %s (%u bytes)\n",
                  boot.psramOk ? "ONLINE (OK)" : "FAIL",
                  static_cast<unsigned>(ESP.getPsramSize()));
    Serial.printf("  Tab5 LittleFS      (3.3M) : %s\n",
                  boot.littlefsOk ? "ONLINE (OK)" : "FAIL");
    Serial.printf("  Tab5 BMI270 Master (0x68) : %s\n",
                  boot.imuMasterOnline ? "ONLINE (OK)" : "OFFLINE");
    Serial.printf("  M135 BMI270 Slave  (0x69) : %s\n",
                  boot.imuSlaveOnline ? "ONLINE (OK)" : "OFFLINE");
    Serial.printf("  Tab5 RX8130CE      (0x32) : %s\n",
                  boot.rtcOnline ? "ONLINE (OK)" : "OFFLINE");
    Serial.printf("  Tab5 INA226 Gauge  (0x41) : %s [R_SHUNT=5mOhm, CAL=10240]\n",
                  boot.ina226Online ? "ONLINE (OK)" : "OFFLINE");
    Serial.printf("  M135 BMP280        (0x76) : %s\n",
                  boot.baroOnline ? "ONLINE (OK)" : "OFFLINE");
    Serial.printf("  M135 BMM150        (AUX)  : %s\n",
                  CompassDriver::instance().isBypassed() ? "BYPASSED (MagSafe)" : "ACTIVE");
    Serial.printf("  M135 NEO-M9N       (UART) : %s [RX=%d, TX=%d, %lu bps]\n",
                  boot.gnssUartOnline ? "ONLINE (OK)" : "OFFLINE",
                  static_cast<int>(boot.gnssActiveRxPin),
                  static_cast<int>(boot.gnssActiveTxPin),
                  static_cast<unsigned long>(GnssConfig::BAUD_OPERATIONAL));
    Serial.printf("  Tab5 MicroSD       (FSPI) : %s\n",
                  boot.tfCardMounted ? "MOUNTED (OK)"
                                     : (boot.tfCardFormatIncompatible ? "INCOMPATIBLE_FS (FORMAT_FAT32)" : "NO_CARD"));
    Serial.printf("  Tab5 ESP32-C6      (SDIO) : %s\n",
                  boot.wifiCoProcOnline ? "ONLINE (OK)" : "OFFLINE");
    Serial.println("=====================================================================");
}

// =============================================================================
// Core 0 - Task 1 (High Priority): High-Rate GNSS + Dual-IMU + Baro + Tunnel DR
//   - ImuDriver            : 50Hz (Master 0x68 + Slave 0x69, 0.5s G-trail)
//   - BaroDriver           : 10Hz (BMP280 0x76)
//   - GnssDriver           : 10Hz UBX-NAV-PVT + 1Hz DOP/SAT + 2s MON-RF
//   - TunnelService        : Hot-reload check for /littlefs/tunnels.bin
//   - AltitudeFusion       : 10Hz GNSS + Barometric complementary filter
//   - DeadReckoningService : 50Hz IMU + Tunnel Chord-Vector snapping (<5% error)
// =============================================================================
void gnssImuTask(void* pvParameters) {
    (void)pvParameters;
    uint32_t lastTunnelPollMs = millis();

    for (;;) {
        const uint32_t tickStartMs = millis();

        ImuDriver::instance().update(tickStartMs);
        BaroDriver::instance().update(tickStartMs);
        GnssDriver::instance().update(tickStartMs);

        if ((tickStartMs - lastTunnelPollMs) >= 200UL) {
            lastTunnelPollMs = tickStartMs;
            TunnelService::instance().pollReloadCommand();
        }

        AltitudeFusion::instance().update(tickStartMs);
        DeadReckoningService::instance().update(tickStartMs);

        // Guarantee >= 1 tick yield so lower-priority netCloudTask & Core 0 IDLE WDT never starve
        const uint32_t elapsedMs = millis() - tickStartMs;
        const uint32_t sleepMs   = (elapsedMs < kGnssImuTickPeriodMs)
                                       ? (kGnssImuTickPeriodMs - elapsedMs)
                                       : 1UL;
        vTaskDelay(pdMS_TO_TICKS(sleepMs));
    }
}

// =============================================================================
// Core 0 - Task 2 (Low Priority): Async Wi-Fi 6, SNTP/RTC, AGPS, Weather & Cloud
//   - Dynamically keeps SdFs* + shared recursive SD mutex synced (e.g. post-FAT32 format)
//   - NetworkManagerService : STA 5-slot RSSI roam, Setup/Share SoftAP, WebServer
//   - TimeService           : GNSS <-> SNTP <-> RX8130CE arbitration & MGA-INI
//   - AgpsService           : u-blox AssistNow Online + LittleFS cache -> M9N
//   - WeatherService        : Open-Meteo 24h forecast + severe weather alert
//   - OneDriveSyncService   : OAuth2 Device Code + 64KB chunked background GPX upload
// =============================================================================
void netCloudTask(void* pvParameters) {
    (void)pvParameters;
    SdFs* lastAttachedSd = TrackLogger::instance().sdFs();

    for (;;) {
        const uint32_t tickStartMs = millis();

        // Keep SdFs* pointer synchronized if TF card was formatted or remounted at runtime
        SdFs* const currentSd = TrackLogger::instance().sdFs();
        if (currentSd != lastAttachedSd) {
            SemaphoreHandle_t const sharedMutex = TrackLogger::instance().sdMutex();
            NetworkManagerService::instance().attachSdStorage(currentSd, sharedMutex, /*isRecursiveMutex=*/true);
            OneDriveSyncService::instance().attachSdStorage(currentSd, sharedMutex, /*isRecursiveMutex=*/true);
            lastAttachedSd = currentSd;
        }

        NetworkManagerService::instance().update();
        TimeService::instance().update();
        AgpsService::instance().update();
        GnssDriver::instance().flushAssistNowStream();
        WeatherService::instance().update();
        OneDriveSyncService::instance().update();

        const uint32_t elapsedMs = millis() - tickStartMs;
        const uint32_t sleepMs   = (elapsedMs < kNetCloudTickPeriodMs)
                                       ? (kNetCloudTickPeriodMs - elapsedMs)
                                       : 5UL;
        vTaskDelay(pdMS_TO_TICKS(sleepMs));
    }
}

// =============================================================================
// Core 1 - Task 3 (Medium-High Priority): 60fps PSRAM UI Engine & Audio Haptics
//   - UiManager    : M5.update() touch, 160x720 Dock + 1120x720 Main Canvas
//   - HapticDriver : Non-blocking ES8388 multi-step tone sequencer
// =============================================================================
void uiTask(void* pvParameters) {
    (void)pvParameters;

    for (;;) {
        const uint32_t frameStartMs = millis();

        UiManager::instance().update(frameStartMs);
        HapticDriver::instance().update();

        const bool inSleep =
            (UiManager::instance().lastSnapshot().power.standbyPhase == STANDBY_PHASE_SLEEPING);
        const uint32_t targetPeriodMs = inSleep ? 50UL : TaskConfig::UI_FRAME_PERIOD_MS;

        // Guarantee >= 1 tick yield so trackLoggerTask, powerTask & Core 1 IDLE WDT never starve
        const uint32_t elapsedMs = millis() - frameStartMs;
        const uint32_t sleepMs   = (elapsedMs < targetPeriodMs)
                                       ? (targetPeriodMs - elapsedMs)
                                       : 1UL;
        vTaskDelay(pdMS_TO_TICKS(sleepMs));
    }
}

// =============================================================================
// Core 1 - Task 4 (Medium Priority): Tri-Folder Dual WGS-84/GCJ-02 GPX Logger
//   - TrackLogger : 1Hz GPX writer, 10s sync, 10MB split, 50% quota, FAT32 format
// =============================================================================
void trackLoggerTask(void* pvParameters) {
    (void)pvParameters;

    for (;;) {
        const uint32_t tickStartMs = millis();

        TrackLogger::instance().update();

        const uint32_t elapsedMs = millis() - tickStartMs;
        const uint32_t sleepMs   = (elapsedMs < kStorageTickPeriodMs)
                                       ? (kStorageTickPeriodMs - elapsedMs)
                                       : 5UL;
        vTaskDelay(pdMS_TO_TICKS(sleepMs));
    }
}

// =============================================================================
// Core 1 - Task 5 (Low Priority): INA226 (0x41) Coulomb Gauge & 20-Min Standby
//   - PowerMonitorService : 2Hz INA226 5mOhm sampling, OCV+Coulomb SoC,
//                           20-min standstill settle -> OneDrive sync -> 80MHz sleep
// =============================================================================
void powerTask(void* pvParameters) {
    (void)pvParameters;

    for (;;) {
        const uint32_t tickStartMs = millis();

        PowerMonitorService::instance().update();

        const uint32_t elapsedMs = millis() - tickStartMs;
        const uint32_t sleepMs   = (elapsedMs < kPowerTickPeriodMs)
                                       ? (kPowerTickPeriodMs - elapsedMs)
                                       : 10UL;
        vTaskDelay(pdMS_TO_TICKS(sleepMs));
    }
}

} // namespace

// =============================================================================
// Arduino Entry Point: Deterministic Hardware Init, Callback Wiring & Task Spawn
// =============================================================================
void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    cfg.clear_display   = true;
    cfg.output_power    = true;  // Enable U6 P2 EXT5V_EN for M5-Bus Module GNSS (M135)
    cfg.internal_imu    = true;  // Tab5 onboard BMI270 Master (0x68)
    cfg.internal_rtc    = true;  // Tab5 onboard RX8130CE RTC (0x32)
    cfg.internal_spk    = true;  // Tab5 onboard ES8388 (0x10) + NS4150B speaker
    cfg.internal_mic    = false; // Leave ES7210 (0x40) idle

    M5.begin(cfg);
    ensureTab5PowerRailsAndSettle();

    // Initialize pure-black landscape viewport for instant boot feedback
    M5.Display.setRotation(Viewport::LANDSCAPE_ROTATION);
    M5.Display.setColorDepth(16);
    M5.Display.setBrightness(180);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setFont(&fonts::efontCN_24_b);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(ThemeColors::NEON_GREEN, TFT_BLACK);
    M5.Display.drawString("M5Track-Tab5 车规双核系统启动中", Viewport::SCREEN_W / 2, (Viewport::SCREEN_H / 2) - 36);

    // 1. Initialize global mutex-guarded SystemState & PSRAM trail ring buffers
    SystemState::instance().init();
    const bool psramReady = (psramFound() && ESP.getPsramSize() > 0U);

    reportBootStage(8, "正在挂载片上 LittleFS 分区...");
    const bool lfsMounted = LittleFS.begin(
        true,
        LittleFsConfig::BASE_PATH,
        10,
        LittleFsConfig::PARTITION_LABEL
    );

    {
        StateGuard guard(SystemState::instance());
        auto& st          = SystemState::instance().data();
        st.boot.psramOk    = psramReady;
        st.boot.littlefsOk = lfsMounted;
    }

    // 2. Audio Haptic Sequencer + Hardware RTC (0x32) & Time Arbitration Service
    reportBootStage(16, "初始化音频反馈与 RX8130CE 实时钟...");
    (void)HapticDriver::instance().begin();
    const bool rtcOk = TimeService::instance().begin(); // Calls RtcDriver::begin() + stages cold-boot MGA-INI

    // 3. INA226 (0x41, 5mOhm) Coulomb Counter & 20-Min Standby Power Service
    reportBootStage(26, "校准 INA226 (0x41, 5m\xCE\xA9) 电源库仑计...");
    const bool inaOk = PowerMonitorService::instance().begin(); // Calls Ina226Driver::begin()

    // 4. M135 Sensors: MagSafe-Bypassed Compass, BMP280 Baro (0x76), Dual BMI270 IMU (0x68+0x69)
    reportBootStage(38, "自检双六轴 BMI270 (0x68/0x69) 与 BMP280 气压计...");
    (void)CompassDriver::instance().begin(); // Permanent software bypass; never touches 0x10 ES8388
    const bool baroOk = BaroDriver::instance().begin();
    const bool imuOk  = ImuDriver::instance().begin();
    (void)imuOk;

    // Wire 20-minute standstill sleep/wakeup hook between PowerMonitorService and ImuDriver
    PowerMonitorService::instance().setSleepStateHook([](bool enteringSleep) {
        if (enteringSleep) {
            (void)ImuDriver::instance().configureWakeOnMotionInterrupt(
                MotionPowerConfig::SUSTAINED_ACCEL_THRESH_G, false);
        } else {
            (void)ImuDriver::instance().restoreActiveMode();
        }
    });

    // 5. Offline Tunnel Spatial DB, Complementary Altitude Fusion & Dead Reckoning Engine
    reportBootStage(48, "加载离线隧道空间库与航位推算引擎...");
    (void)TunnelService::instance().begin();
    (void)AltitudeFusion::instance().begin();
    DeadReckoningService::instance().begin();

    // 6. NEO-M9N (SPG 4.04) Multi-Constellation GNSS Driver (10Hz PVT @ 115200 bps)
    reportBootStage(60, "握手 u-blox NEO-M9N 四星并发定位引擎...");
    const bool gnssOk = GnssDriver::instance().begin(kGnssOperationalMeasRateMs);

    // Wire TimeService cold-boot RTC / warm-start NVS / SNTP UBX-MGA-INI injection -> GnssDriver
    TimeService::instance().setMgaInjectCallback([](const uint8_t* ubxFrame, size_t len) -> bool {
        return GnssDriver::instance().pushRawUbx(ubxFrame, len);
    });

    // 7. Dedicated FSPI MicroSD Tri-Folder Dual-Coordinate GPX Logger & Self-Heal
    reportBootStage(74, "挂载独立 SPI MicroSD 存储与 GPX 断电自愈...");
    const bool sdMounted = TrackLogger::instance().begin();
    SdFs* const sharedSdFs              = TrackLogger::instance().sdFs();
    SemaphoreHandle_t const sharedSdMtx = TrackLogger::instance().sdMutex();

    // 8. ESP32-C6 SDIO Wi-Fi 6 Network Manager + Cloud Services (sharing recursive SD mutex!)
    reportBootStage(88, "启动 ESP32-C6 Wi-Fi 6 协处理器与云端服务...");
    NetworkManagerService::instance().attachSdStorage(sharedSdFs, sharedSdMtx, /*isRecursiveMutex=*/true);
    const bool wifiOk = NetworkManagerService::instance().begin(sharedSdFs, sharedSdMtx, /*isRecursiveMutex=*/true);

    // Wire AgpsService AssistNow chunk stream -> GnssDriver::pushAssistNowChunk / flushAssistNowStream
    (void)AgpsService::instance().begin([](const uint8_t* data, size_t len) -> bool {
        if (!GnssDriver::instance().isOnline()) {
            return false;
        }
        (void)GnssDriver::instance().pushAssistNowChunk(data, len, /*skipTime=*/true);
        return true;
    });
    GnssDriver::instance().flushAssistNowStream();

    (void)WeatherService::instance().begin();

    OneDriveSyncService::instance().attachSdStorage(sharedSdFs, sharedSdMtx, /*isRecursiveMutex=*/true);
    (void)OneDriveSyncService::instance().begin(sharedSdFs, sharedSdMtx, /*isRecursiveMutex=*/true);

    // 9. Synchronize authoritative hardware probe states into SystemState::boot for PageInit
    BootCheckState bootReportSnap{};
    {
        StateGuard guard(SystemState::instance());
        auto& st = SystemState::instance().data();

        st.boot.psramOk                  = psramReady;
        st.boot.littlefsOk               = lfsMounted;
        st.boot.rtcOnline                = rtcOk || RtcDriver::instance().isOnline();
        st.boot.ina226Online             = inaOk || Ina226Driver::instance().isOnline();
        st.boot.baroOnline               = baroOk || BaroDriver::instance().isOnline();
        st.boot.imuMasterOnline          = ImuDriver::instance().isMasterOnline();
        st.boot.imuSlaveOnline           = ImuDriver::instance().isSlaveOnline();
        st.boot.gnssUartOnline           = gnssOk || GnssDriver::instance().isOnline();
        st.boot.gnssActiveRxPin          = GnssDriver::instance().activeRxPin();
        st.boot.gnssActiveTxPin          = GnssDriver::instance().activeTxPin();
        st.boot.tfCardMounted            = sdMounted || TrackLogger::instance().isMounted();
        st.boot.tfCardFormatIncompatible = st.boot.tfCardFormatIncompatible || st.storage.formatIncompatible;
        st.boot.wifiCoProcOnline         = wifiOk || st.boot.wifiCoProcOnline;
        st.boot.progressPercent          = 100U;
        st.boot.selfTestComplete         = true;

        const char* finalMsg = "自检完成 · 四星并发搜星中...";
        if (st.boot.tfCardFormatIncompatible) {
            finalMsg = "检测到非FAT32存储卡 · 请一键格式化";
        } else if (!st.boot.gnssUartOnline) {
            finalMsg = "自检完成 · 未检测到 M135 GNSS 模块";
        }
        strncpy(st.boot.statusMessage, finalMsg, sizeof(st.boot.statusMessage) - 1);
        st.boot.statusMessage[sizeof(st.boot.statusMessage) - 1] = '\0';

        bootReportSnap = st.boot;
    }

    // 10. Initialize 60fps PSRAM Double-Buffered UI Engine (mounts all 16 PageId routes)
    (void)UiManager::instance().begin();
    HapticDriver::instance().playTap();

    // Emit structured hardware self-test summary before spawning dual-core FreeRTOS tasks
    printBootSelfTestReport(bootReportSnap);

    // 11. Spawn ESP32-P4 Dual-Core RISC-V FreeRTOS Tasks (strictly pinned per ARCHITECTURE §4)
    const BaseType_t okGnssImu = xTaskCreatePinnedToCore(
        gnssImuTask,
        "gnssImuTask",
        TaskConfig::STACK_GNSS_IMU,
        nullptr,
        TaskConfig::PRIO_GNSS_IMU,
        &g_gnssImuTaskHandle,
        TaskConfig::CORE_GNSS_IMU
    );

    const BaseType_t okNetCloud = xTaskCreatePinnedToCore(
        netCloudTask,
        "netCloudTask",
        TaskConfig::STACK_NET_CLOUD,
        nullptr,
        TaskConfig::PRIO_NET_CLOUD,
        &g_netCloudTaskHandle,
        TaskConfig::CORE_NET_CLOUD
    );

    const BaseType_t okUi = xTaskCreatePinnedToCore(
        uiTask,
        "uiTask",
        TaskConfig::STACK_UI,
        nullptr,
        TaskConfig::PRIO_UI,
        &g_uiTaskHandle,
        TaskConfig::CORE_UI
    );

    const BaseType_t okStorage = xTaskCreatePinnedToCore(
        trackLoggerTask,
        "trackLoggerTask",
        TaskConfig::STACK_STORAGE,
        nullptr,
        TaskConfig::PRIO_STORAGE,
        &g_trackLoggerTaskHandle,
        TaskConfig::CORE_STORAGE
    );

    const BaseType_t okPower = xTaskCreatePinnedToCore(
        powerTask,
        "powerTask",
        TaskConfig::STACK_POWER,
        nullptr,
        TaskConfig::PRIO_POWER,
        &g_powerTaskHandle,
        TaskConfig::CORE_POWER
    );

    Serial.printf(
        "[RTOS] Tasks spawned: gnssImu=%ld(C%d,P%u,%luB) netCloud=%ld(C%d,P%u,%luB) "
        "ui=%ld(C%d,P%u,%luB) trackLogger=%ld(C%d,P%u,%luB) power=%ld(C%d,P%u,%luB)\n",
        static_cast<long>(okGnssImu),  static_cast<int>(TaskConfig::CORE_GNSS_IMU),  static_cast<unsigned>(TaskConfig::PRIO_GNSS_IMU),  static_cast<unsigned long>(TaskConfig::STACK_GNSS_IMU),
        static_cast<long>(okNetCloud), static_cast<int>(TaskConfig::CORE_NET_CLOUD), static_cast<unsigned>(TaskConfig::PRIO_NET_CLOUD), static_cast<unsigned long>(TaskConfig::STACK_NET_CLOUD),
        static_cast<long>(okUi),       static_cast<int>(TaskConfig::CORE_UI),        static_cast<unsigned>(TaskConfig::PRIO_UI),        static_cast<unsigned long>(TaskConfig::STACK_UI),
        static_cast<long>(okStorage),  static_cast<int>(TaskConfig::CORE_STORAGE),   static_cast<unsigned>(TaskConfig::PRIO_STORAGE),   static_cast<unsigned long>(TaskConfig::STACK_STORAGE),
        static_cast<long>(okPower),    static_cast<int>(TaskConfig::CORE_POWER),     static_cast<unsigned>(TaskConfig::PRIO_POWER),     static_cast<unsigned long>(TaskConfig::STACK_POWER)
    );
    Serial.println("All M5Track-Tab5 FreeRTOS tasks started.");
}

void loop() {
    // All real-time workloads run on dedicated Core 0 / Core 1 FreeRTOS tasks; yield Arduino loopTask
    vTaskDelay(pdMS_TO_TICKS(1000));
}
