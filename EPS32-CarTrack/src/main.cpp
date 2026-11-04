#include <Arduino.h>
#include <WiFi.h>
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
#include "service/OneDriveSyncService.h"
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

// Core 1, Priority 5: Hardware Touch Engine (~66 Hz touch polling)
void touchTask(void* pvParameters) {
    for (;;) {
        g_uiManager.handleTouch();
        vTaskDelay(pdMS_TO_TICKS(15)); // 15ms (~66 Hz) responsive touch polling without I2C bus starvation
    }
}

// Core 1, Priority 3: 1280x720 Widescreen Double-Buffered UI Renderer (~33 FPS smooth)
void uiTask(void* pvParameters) {
    for (;;) {
        g_uiManager.render();
        vTaskDelay(pdMS_TO_TICKS(30)); // ~33 FPS smooth refresh, saving 50% PSRAM bus bandwidth
    }
}

// Core 1, Priority 2: 1Hz Dual-Stream (WGS-84 / GCJ-02) GPX Track Logger
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

#if defined(CONFIG_IDF_TARGET_ESP32P4)
// Hook for M5Unified to inject Tab5 ESP32-C6 SDIO pins before WiFi initialization
extern "C" bool hostedSetPins(int8_t clk, int8_t cmd, int8_t d0, int8_t d1, int8_t d2, int8_t d3, int8_t rst) {
    return true; // Configured natively in wifiHostedInit with 20 MHz GPIO matrix timing
}
#endif

void setup() {
    auto cfg = M5.config();
    cfg.internal_imu = true;
    cfg.internal_rtc = true;
    cfg.internal_spk = false;
    cfg.external_spk = false;
    M5.begin(cfg);

    Serial.begin(BaudConfig::SERIAL_DEBUG_BAUD);
    delay(100);

    Serial.println("\n=======================================================");
    Serial.println("  M5Track-Tab5 High-Precision Vehicle Tracker & HUD");
    Serial.println("=======================================================");

    // Enable M5-Bus 5V output for stacked Module GNSS (M135)
    M5.Power.setExtOutput(true);
    auto& ioe0 = M5.getIOExpander(0);
    ioe0.setHighImpedance(2, false);
    ioe0.setDirection(2, true);
    ioe0.digitalWrite(2, true);  // EXT5V_EN (Pin 2: 5V Rail to M5-Bus)

    ioe0.setHighImpedance(3, false);
    ioe0.setDirection(3, true);
    ioe0.digitalWrite(3, true);  // Tab5/Tab5X Hat EXT 5V Power

    // Power on ESP32-C6 Wi-Fi co-processor (WLAN_PWR_EN: IOE1 Pin 0)
    auto& ioe1 = M5.getIOExpander(1);
    ioe1.setHighImpedance(0, false);
    ioe1.setDirection(0, true);
    ioe1.digitalWrite(0, true);  // WLAN_PWR_EN (Pin 0: Power to ESP32-C6)

    ioe1.setHighImpedance(3, false);
    ioe1.setDirection(3, true);
    ioe1.digitalWrite(3, true);  // USB5V_EN

    // Set Wi-Fi RF Path to internal antenna (IOE0 Pin 0 = 0)
    ioe0.setHighImpedance(0, false);
    ioe0.setDirection(0, true);
    ioe0.digitalWrite(0, false); // Internal antenna path

    delay(250); // Allow NEO-M9N and ESP32-C6 power rails to fully stabilize

    // Initialize Network Manager early so ESP32-C6 SDIO slave handshake occurs immediately
    AppNetworkManager::instance().init();

    // Initialize Thread-Safe SystemState Pool
    SystemState::instance().init();

    // Mount On-Chip LittleFS (3MB partition for Tunnel DB)
    if (!LittleFS.begin(true)) {
        Serial.println("[FS] Warning: LittleFS mount failed.");
    }

    // Initialize 1280x720 Widescreen UI first for instant boot visual feedback
    g_uiManager.init();

    // Orthogonal I2C Bus Scan (SDA=G31, SCL=G32)
    I2cBusManager::instance().initMutex();
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
    OneDriveSyncService::instance().init();
    OneDriveSyncService::instance().startTask();

    // Structured Hardware & Sensor Diagnostic Summary
    SystemContext snap = SystemState::instance().getSnapshot();
    const char* antStr = "INIT";
    if (snap.gnss.antennaStatus == 1) antStr = "DONTKNOW";
    else if (snap.gnss.antennaStatus == 2) antStr = "OK (Connected & Powered)";
    else if (snap.gnss.antennaStatus == 3) antStr = "SHORT";
    else if (snap.gnss.antennaStatus == 4) antStr = "OPEN (Disconnected)";

    Serial.println("-------------------------------------------------------");
    Serial.println("[HW_SELF_TEST] Device & Sensor Diagnostic Summary:");
    Serial.printf("  [MCU]   ESP32-P4 Flash: %u MB | PSRAM Free: %u KB\n",
                  ESP.getFlashChipSize() / (1024 * 1024), ESP.getFreePsram() / 1024);
    Serial.printf("  [IMU1]  Tab5 BMI270 Master (0x68) : %s\n", snap.imu.masterOnline ? "ONLINE (OK)" : "FAIL");
    Serial.printf("  [IMU2]  M135 BMI270 Slave  (0x69) : %s\n", snap.imu.slaveOnline  ? "ONLINE (OK)" : "FAIL");
    Serial.printf("  [RTC]   Tab5 RX8130CE      (0x32) : %s\n", snap.rtcOnline        ? "ONLINE (OK)" : "FAIL");
    Serial.printf("  [PWR]   Tab5 INA226 Gauge  (0x40) : %s\n", snap.power.isAvailable ? "ONLINE (OK)" : "FAIL");
    Serial.printf("  [BARO]  M135 BMP280        (0x76) : %s\n", snap.baro.isAvailable ? "ONLINE (OK)" : "FAIL");
    Serial.printf("  [MAG]   M135 BMM150        (0x10) : %s (MagSafe Bypassed)\n", i2cReport.bmm150Mag0x10 ? "HW_ACK (OK)" : "OFFLINE");
    Serial.printf("  [GNSS]  M135 NEO-M9N       (UART) : %s (RX=%d, TX=%d)\n",
                  snap.gnss.moduleOnline ? "ONLINE (OK)" : "FAIL",
                  g_gnssDriver.getActiveRxPin(), g_gnssDriver.getActiveTxPin());
    Serial.printf("  [ANT]   GNSS Active Antenna Status : %s | Jamming: %u | Noise: %u\n",
                  antStr, snap.gnss.antennaStatus, snap.gnss.rfNoiseLevel);
    Serial.printf("  [TOUCH] Tab5 Touch Controller      : %s (Rotated 180 deg)\n",
                  M5.Touch.isEnabled() ? "ONLINE (OK)" : "STANDBY");
    Serial.printf("  [SD]    MicroSD Slot    (G39~G44) : %s\n", snap.storage.isMounted ? "MOUNTED (OK)" : "NO_CARD");
    Serial.println("-------------------------------------------------------");

    // Record System Boot / Startup Event into TF Card Power FIFO Log
    esp_reset_reason_t rstReason = esp_reset_reason();
    char bootMsg[128];
    snprintf(bootMsg, sizeof(bootMsg), "System startup (Reset: %d, Batt: %.2fV, %u%%, Pwr: %s, TF: %s)",
             (int)rstReason, snap.power.busVoltageV, (unsigned)snap.power.batterySocPercent,
             snap.power.hasExternalPower ? "ExtUSB" : "Battery",
             snap.storage.isMounted ? "Mounted" : "NoCard");
    TrackLogger::instance().logPowerEvent("BOOT", bootMsg);

    // Launch FreeRTOS Dual-Core Tasks on ESP32-P4 with generous stack headroom for 100+ hours stability
    xTaskCreatePinnedToCore(gnssTask,        "gnssTask",        12288, nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(netTask,         "netTask",         16384, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(touchTask,       "touchTask",        8192, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(uiTask,          "uiTask",          24576, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(trackLoggerTask, "trackLoggerTask", 12288, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(pwrTask,         "pwrTask",         8192,  nullptr, 1, nullptr, 1);

    Serial.println("[BOOT] All M5Track-Tab5 FreeRTOS tasks started.");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(2000));
    SystemContext snap = SystemState::instance().getSnapshot();
    const char* antStr = "INIT";
    if (snap.gnss.antennaStatus == 1) antStr = "DONTKNOW";
    else if (snap.gnss.antennaStatus == 2) antStr = "OK";
    else if (snap.gnss.antennaStatus == 3) antStr = "SHORT";
    else if (snap.gnss.antennaStatus == 4) antStr = "OPEN";

    Serial.printf("[STATUS] GNSS:%s(RX=%d,TX=%d,Sats=%u,Fix=%d,Ant=%s[%d],Noise=%u) ALT:%.1fm(Baro:%.1f,GPS:%.1f,P0:%.1f) IMU:%s/%s BARO:%.1fhPa PWR:%.2fV(%.0f%%)\n",
                  snap.gnss.moduleOnline ? "ONLINE" : "FAIL",
                  g_gnssDriver.getActiveRxPin(),
                  g_gnssDriver.getActiveTxPin(),
                  (unsigned)snap.gnss.numSats,
                  (int)snap.gnss.fixType,
                  antStr,
                  (int)snap.gnss.antennaStatus,
                  (unsigned)snap.gnss.rfNoiseLevel,
                  snap.baro.fusedAltitudeM,
                  snap.baro.rawBaroAltitudeM,
                  snap.gnss.altitudeMeters,
                  snap.baro.seaLevelPressureHpa,
                  snap.imu.masterOnline ? "OK" : "FAIL",
                  snap.imu.slaveOnline ? "OK" : "FAIL",
                  snap.baro.pressureHpa,
                  snap.power.busVoltageV,
                  snap.power.batterySocPercent);
}
