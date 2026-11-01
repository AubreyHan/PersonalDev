#include <Arduino.h>

#if defined(CONFIG_IDF_TARGET_ESP32S3)
// =============================================================================
// Waveshare ESP32-S3-Touch-AMOLED-1.75 CarTrack Firmware
// Dual-Core FreeRTOS Architecture (Core 0: GNSS & Network, Core 1: AMOLED UI)
// =============================================================================
#include <Wire.h>
#include <esp_task_wdt.h>
#include "config_s3_amoled.h"
#include "driver/GnssDriver_LC76G.h"
#include "driver/RtcDriver_PCF85063.h"
#include "driver/TouchDriver_CST9217.h"
#include "service/TimeService_Dual.h"
#include "service/CarTrackService.h"
#include "service/NetworkManager_Setup.h"
#include "ui/UiManager_AMOLED.h"

static void initI2cMaster() {
    Wire.begin(BoardConfig::I2C_SDA, BoardConfig::I2C_SCL, BoardConfig::I2C_FREQ);
    Wire.setTimeOut(100);

    Serial.println("[I2C] Scanning I2C bus (SDA=15, SCL=14, 100kHz)...");
    int nDevices = 0;
    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        uint8_t error = Wire.endTransmission();
        if (error == 0) {
            Serial.printf("[I2C] Found device at 0x%02X", address);
            if (address == 0x20) Serial.print(" (TCA9554 IO Expander)");
            else if (address == 0x34) Serial.print(" (AXP2101 PMIC)");
            else if (address == 0x50) Serial.print(" (LC76G GNSS Write)");
            else if (address == 0x51) Serial.print(" (PCF85063 RTC)");
            else if (address == 0x54) Serial.print(" (LC76G GNSS Read)");
            else if (address == 0x5A) Serial.print(" (CST9217 Touch)");
            else if (address == 0x6B || address == 0x6A) Serial.print(" (QMI8658 IMU)");
            Serial.println();
            nDevices++;
        }
    }
    Serial.printf("[I2C] Scan complete: %d devices found.\n", nDevices);
}

// Core 0 Task: Touch Input, GNSS I2C NMEA streaming, Kalman filtering, Distance & History
static void gnssAndSensorTask(void* pvParameters) {
    for (;;) {
        TouchDriver_CST9217::instance().update();
        GnssDriver_LC76G::instance().update();
        CarTrackService::instance().update();
        TimeService_Dual::instance().update();
        vTaskDelay(pdMS_TO_TICKS(15)); // ~66Hz sampling
    }
}

// Core 0 Task: WiFi Captive Portal, WebServer, NTP Sync
static void networkTask(void* pvParameters) {
    for (;;) {
        NetworkManager_Setup::instance().update();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Core 1 Task: 466x466 AMOLED UI Rendering & Touch Swipe Management
static void uiTask(void* pvParameters) {
    for (;;) {
        UiManager_AMOLED::instance().update();
        vTaskDelay(pdMS_TO_TICKS(33)); // ~30 FPS
    }
}

void setup() {
    Serial.begin(115200);
    delay(400);

    Serial.println("\n=======================================================");
    Serial.println("  Waveshare ESP32-S3-Touch-AMOLED-1.75 CarTrack v1.0.0");
    Serial.println("=======================================================");

    // 1. Initialize Shared Master I2C Bus (GPIO15 SDA, GPIO14 SCL)
    initI2cMaster();

    // 2. Initialize QSPI AMOLED Display (CO5300 466x466)
    UiManager_AMOLED::instance().init();

    // 3. Initialize CST9217 Capacitive Touch Controller (GPIO11 INT, GPIO40 RST)
    TouchDriver_CST9217::instance().init();

    // 4. Initialize PCF85063 Hardware RTC & Dual Time Service (GPS + NTP)
    TimeService_Dual::instance().init();

    // 5. Initialize Quectel LC76G GNSS (Quad-Constellation, 15° Mask, Hot Start)
    GnssDriver_LC76G::instance().init();

    // 6. Initialize CarTrack Engine (Kalman Altitude, Speed, Distance, 1h Buffers)
    CarTrackService::instance().init();

    // 7. Initialize WiFi SoftAP & Captive Portal Setup Page
    NetworkManager_Setup::instance().init();

    Serial.println("[System] Launching Dual-Core FreeRTOS Tasks...");

    // Launch FreeRTOS Tasks
    xTaskCreatePinnedToCore(gnssAndSensorTask, "gnssTask", 8192,  nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(networkTask,       "netTask",  8192,  nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(uiTask,            "uiTask",   12288, nullptr, 4, nullptr, 1);

    Serial.println("[System] All systems running!");
}

void loop() {
    // Background supervisory loop
    vTaskDelay(pdMS_TO_TICKS(1000));
}

#else
// =============================================================================
// M5Stack Tab5 (ESP32-P4) Legacy Target
// =============================================================================
#include <M5Unified.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
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
#include "service/OneDriveSyncService.h"
#include "ui/UiManager.h"

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

void gnssTask(void* pvParameters) {
    esp_task_wdt_add(nullptr);
    for (;;) {
        esp_task_wdt_reset();
        g_gnssDriver.update();
        g_altitudeFusion.update();
        g_drService.update();
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

void netTask(void* pvParameters) {
    esp_task_wdt_add(nullptr);
    for (;;) {
        esp_task_wdt_reset();
        AppNetworkManager::instance().update();
        g_timeService.update();
        g_agpsService.update();
        g_weatherService.update();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void uiTask(void* pvParameters) {
    esp_task_wdt_add(nullptr);
    for (;;) {
        esp_task_wdt_reset();
        M5.update();
        g_uiManager.update();
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

void trackLoggerTask(void* pvParameters) {
    esp_task_wdt_add(nullptr);
    for (;;) {
        esp_task_wdt_reset();
        TrackLogger::instance().update();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void pwrTask(void* pvParameters) {
    esp_task_wdt_add(nullptr);
    for (;;) {
        esp_task_wdt_reset();
        g_powerService.update();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void setup() {
    Serial.begin(BaudConfig::SERIAL_DEBUG_BAUD);
    auto cfg = M5.config();
    M5.begin(cfg);
    I2cBusManager::instance().init();
    I2cScanReport i2cReport = I2cBusManager::instance().scanBus();

    g_ina226Driver.init();
    g_rtcDriver.init();
    g_imuDriver.init();
    g_baroDriver.init();
    g_compassDriver.init();
    HapticDriver::instance().init();

    TrackLogger::instance().init();
    g_gnssDriver.init();
    g_uiManager.init();
    g_altitudeFusion.init(&g_gnssDriver, &g_baroDriver, &g_imuDriver);
    g_tunnelService.init();
    g_drService.init(&g_imuDriver, &g_tunnelService);
    g_timeService.init(&g_rtcDriver, &g_gnssDriver);
    g_agpsService.init(&g_gnssDriver);
    g_powerService.init(&g_ina226Driver);
    g_weatherService.init();
    AppNetworkManager::instance().init();
    OneDriveSyncService::instance().init();
    OneDriveSyncService::instance().startTask();

    xTaskCreatePinnedToCore(gnssTask,        "gnssTask",        12288, nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(netTask,         "netTask",         12288, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(uiTask,          "uiTask",          16384, nullptr, 4, nullptr, 1);
    xTaskCreatePinnedToCore(trackLoggerTask, "trackLoggerTask", 12288, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(pwrTask,         "pwrTask",         8192,  nullptr, 1, nullptr, 1);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
#endif
