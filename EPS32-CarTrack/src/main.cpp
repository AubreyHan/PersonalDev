#include <M5Unified.h>
#include <Wire.h>
#include "config.h"
#include "model/SystemState.h"
#include "driver/PowerDriver.h"
#include "driver/BaroDriver.h"
#include "driver/RtcDriver.h"
#include "driver/GnssDriver.h"
#include "driver/ButtonDriver.h"
#include "service/AltitudeFusion.h"
#include "service/DeadReckoningService.h"
#include "service/TunnelService.h"
#include "service/PowerMonitorService.h"
#include "service/TimeService.h"
#include "service/WeatherService.h"
#include "service/TrackLogger.h"
#include "service/NetworkManager.h"
#include "ui/UiManager.h"

// FreeRTOS Task Handles
TaskHandle_t gnssTaskHandle = NULL;
TaskHandle_t netTaskHandle = NULL;
TaskHandle_t uiTaskHandle = NULL;

void gnssTask(void* pvParameters) {
    for (;;) {
        // High frequency sensor reading & navigation processing (approx 20Hz)
        GnssDriver::instance().update();
        BaroDriver::instance().update();

        float altM = 0.0f, spdKmh = 0.0f, hdop = 99.0f, heading = 0.0f;
        bool isValid = false;
        int32_t latE7 = 0, lonE7 = 0;

        SystemState& state = SystemState::instance();
        state.lock();
        const GnssData& g = state.getContextUnsafe().gnss;
        altM = g.altitudeMeters;
        spdKmh = g.speedKmh;
        hdop = g.hdop;
        heading = g.headingDeg;
        isValid = g.isValid;
        latE7 = g.latE7;
        lonE7 = g.lonE7;
        state.unlock();

        // Sensor fusion
        AltitudeFusion::instance().update(altM, isValid, hdop, spdKmh);
        DeadReckoningService::instance().update(isValid, spdKmh, heading, latE7, lonE7);
        TunnelService::instance().update(latE7, lonE7, heading, spdKmh, isValid);
        PowerMonitorService::instance().update();

        // SD Track Logging
        TrackLogger::instance().update();

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void netTask(void* pvParameters) {
    for (;;) {
        WifiNetworkManager::instance().update();

        bool isWifi = false;
        bool gnssValid = false;
        double lat = 0.0, lon = 0.0;

        SystemState& state = SystemState::instance();
        state.lock();
        const SystemContext& ctx = state.getContextUnsafe();
        isWifi = ctx.isWifiConnected;
        gnssValid = ctx.gnss.isValid;
        lat = ctx.gnss.latitude;
        lon = ctx.gnss.longitude;
        state.unlock();

        TimeService::instance().update(isWifi, gnssValid);
        WeatherService::instance().update(isWifi, gnssValid, lat, lon);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void setup() {
    auto cfg = M5.config();
    cfg.internal_imu = true;
    M5.begin(cfg);

    Serial.begin(BaudConfig::SERIAL_DEBUG_BAUD);
    delay(200);

    Serial.println("\n=======================================================");
    Serial.println("  M5Track-M9N High-Precision Vehicle Dashboard v1.0.0");
    Serial.println("=======================================================");

    // 1. Initialize System State
    SystemState::instance().init();

    // 2. Initialize Hardware Drivers
    // Enable external power to M-Bus (supplies 5V via EXTEN and 3.3V to GNSS module)
    M5.Power.setExtOutput(true);
    delay(500); // Allow GNSS module & sensors to stabilize after power-on

    PowerDriver::instance().init();
    ButtonDriver::instance().init();
    RtcDriver::instance().init();

    // M-Bus I2C shares the Core2 internal bus (G21/G22); Port A (G32/G33) is NOT the M-Bus.
    // Diagnostic scan of M-Bus I2C (Wire1: SDA 21, SCL 22)
    Serial.println("\n[I2C] Scanning M-Bus I2C (Wire1: SDA 21, SCL 22)...");
    uint8_t nDevices = 0;
    for (uint8_t address = 1; address < 127; ++address) {
        Wire1.beginTransmission(address);
        uint8_t error = Wire1.endTransmission();
        if (error == 0) {
            Serial.printf("[I2C] Device found at 0x%02X", address);
            if (address == 0x76 || address == 0x77) Serial.print(" -> BMP280 Barometer (Module GNSS)");
            else if (address == 0x68 || address == 0x69) Serial.print(" -> BMI270 6-Axis IMU (Module GNSS)");
            else if (address == 0x10) Serial.print(" -> BMM150 Magnetometer (Module GNSS)");
            else if (address == 0x40 || address == 0x45) Serial.print(" -> INA226 Power Monitor (Base 16340)");
            else if (address == 0x34) Serial.print(" -> AXP192 PMIC (Core2 Internal)");
            else if (address == 0x38) Serial.print(" -> FT6336U Touch (Core2 Internal)");
            else if (address == 0x51) Serial.print(" -> BM8563 RTC (Core2 Internal)");
            Serial.println();
            nDevices++;
        }
    }
    Serial.printf("[I2C] Scan complete: %d device(s) detected.\n\n", nDevices);

    BaroDriver::instance().init(&Wire1);
    PowerMonitorService::instance().init(&Wire1);

    // GNSS NEO-M9N
    GnssDriver::instance().init();

    // 3. Initialize Services
    AltitudeFusion::instance().init();
    DeadReckoningService::instance().init();
    TunnelService::instance().init();
    TimeService::instance().init();
    WeatherService::instance().init();
    TrackLogger::instance().init();
    WifiNetworkManager::instance().init();

    // 4. Initialize UI Manager
    UiManager::instance().init();

    // 5. Create Background FreeRTOS Tasks on Core 0
    xTaskCreatePinnedToCore(gnssTask, "gnssTask", 12288, NULL, 5, &gnssTaskHandle, 0);
    xTaskCreatePinnedToCore(netTask, "netTask", 8192, NULL, 2, &netTaskHandle, 0);

    Serial.println("[SYSTEM] System initialization complete. Starting main loop.");
}

void loop() {
    M5.update();
    PowerDriver::instance().update();
    UiManager::instance().update();

    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 2000) {
        lastPrint = millis();
        Serial.printf("[LOOP] Heartbeat: Free Heap = %d KB, VBUS = %d mV\n", 
                      (int)(ESP.getFreeHeap() / 1024), 
                      (int)M5.Power.getVBUSVoltage());
    }

    delay(33); // Target ~30 FPS on Core 1
}
