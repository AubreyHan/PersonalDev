#include <Arduino.h>
#include <M5Unified.h>
#include <LittleFS.h>
#include "config.h"
#include "model/SystemState.h"
#include "driver/I2cBusManager.h"
#include "driver/GnssDriver.h"
#include "driver/BaroDriver.h"
#include "driver/CompassDriver.h"
#include "driver/ImuDriver.h"
#include "driver/RtcDriver.h"
#include "driver/Ina226Driver.h"
#include "driver/HapticDriver.h"
#include "service/AltitudeFusion.h"
#include "service/TunnelService.h"
#include "service/DeadReckoningService.h"
#include "service/TimeService.h"
#include "service/AgpsService.h"
#include "service/PowerMonitorService.h"
#include "service/TrackLogger.h"
#include "service/WeatherService.h"
#include "service/NetworkManager.h"
#include "ui/UiManager.h"

// Global Driver & Service Instances
GnssDriver           g_gnssDriver;
BaroDriver           g_baroDriver;
CompassDriver        g_compassDriver;
ImuDriver            g_imuDriver;
RtcDriver            g_rtcDriver;
Ina226Driver         g_ina226Driver;

AltitudeFusion       g_altitudeFusion;
TunnelService        g_tunnelService;
DeadReckoningService g_drService;
TimeService          g_timeService;
AgpsService          g_agpsService;
PowerMonitorService  g_powerService;
WeatherService       g_weatherService;
UiManager            g_uiManager;

// =============================================================================
// FreeRTOS Multi-Core Tasks (ESP32-P4 Dual RISC-V 400MHz)
// =============================================================================

// Core 0, Priority 5: High-frequency GNSS UART UBX parsing, Baro & Dual-IMU DR
void gnssTask(void* pvParameters) {
    for (;;) {
        g_gnssDriver.update();
        g_altitudeFusion.update();
        g_drService.update();
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

// Core 0, Priority 2: Wi-Fi 6 Multi-SSID, SoftAP QR/Captive Portal, NTP Sync, AGPS & Weather
void netTask(void* pvParameters) {
    for (;;) {
        AppNetworkManager::instance().update();
        g_timeService.update();
        g_agpsService.update();
        g_weatherService.update();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Core 1, Priority 4: 1280x720 Double-Buffered UI & Touch Gesture Engine
void uiTask(void* pvParameters) {
    for (;;) {
        g_uiManager.update();
        vTaskDelay(pdMS_TO_TICKS(16)); // ~60 fps target
    }
}

// Core 1, Priority 3: 1Hz Dual-Stream (WGS-84 / GCJ-02) GPX Track Logger
void trackLoggerTask(void* pvParameters) {
    for (;;) {
        TrackLogger::instance().update();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// Core 1, Priority 1: 500ms INA226 Coulomb Counter & Standstill Deep-Sleep Monitor
void pwrTask(void* pvParameters) {
    for (;;) {
        g_powerService.update();
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void setup() {
    auto cfg = M5.config();
    cfg.internal_imu = true;
    cfg.internal_rtc = true;
    cfg.external_spk = true;
    M5.begin(cfg);

    Serial.begin(BaudConfig::SERIAL_DEBUG_BAUD);
    delay(100);
    Serial.println("\n=======================================================");
    Serial.println("  M5Track-Tab5 High-Precision Vehicle Tracker & HUD");
    Serial.println("=======================================================");

    // Enable M5-Bus 5V output for stacked Module GNSS (M135)
    M5.Power.setExtOutput(true);

    // Initialize Thread-Safe SystemState Pool
    SystemState::instance().init();

    // Mount On-Chip LittleFS (3MB partition for Tunnel DB)
    if (!LittleFS.begin(true)) {
        Serial.println("[FS] Warning: LittleFS mount failed.");
    }

    // Initialize 1280x720 Widescreen UI first for instant boot visual feedback
    g_uiManager.init();

    // Orthogonal I2C Bus Scan (SDA=G31, SCL=G32)
    I2cScanReport i2cReport = I2cBusManager::instance().scanBus();

    // Initialize Hardware Drivers (Graceful non-blocking fallback if hardware unattached)
    bool rtcOk = g_rtcDriver.init();
    SystemState::instance().lock();
    SystemState::instance().getContextUnsafe().rtcOnline = rtcOk || i2cReport.rx8130ceRtc0x32;
    SystemState::instance().unlock();

    g_compassDriver.init(); // Explicitly bypassed for MagSafe immunity
    g_imuDriver.init();     // Dual BMI270 (0x68 Master + 0x69 Slave)
    g_baroDriver.init();    // BMP280 (0x76)
    g_ina226Driver.init();  // INA226 (0x40)
    g_gnssDriver.init();    // NEO-M9N (M5-Bus UART)
    TrackLogger::instance().init();

    // Initialize Core Services
    g_tunnelService.init();
    g_altitudeFusion.init(&g_baroDriver);
    g_drService.init(&g_imuDriver, &g_tunnelService);
    g_timeService.init(&g_rtcDriver, &g_gnssDriver);
    g_agpsService.init(&g_gnssDriver);
    g_powerService.init(&g_ina226Driver);
    g_weatherService.init();
    AppNetworkManager::instance().init();

    // Launch FreeRTOS Dual-Core Tasks on ESP32-P4
    xTaskCreatePinnedToCore(gnssTask,        "gnssTask",        8192,  nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(netTask,         "netTask",         8192,  nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(uiTask,          "uiTask",          12288, nullptr, 4, nullptr, 1);
    xTaskCreatePinnedToCore(trackLoggerTask, "trackLoggerTask", 8192,  nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(pwrTask,         "pwrTask",         4096,  nullptr, 1, nullptr, 1);

    Serial.println("[BOOT] All M5Track-Tab5 FreeRTOS tasks started.");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
