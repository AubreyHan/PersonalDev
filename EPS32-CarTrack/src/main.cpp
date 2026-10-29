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

        SystemState& state = SystemState::instance();
        state.lock();
        SystemContext ctx = state.getContextUnsafe();
        state.unlock();

        // Sensor fusion
        AltitudeFusion::instance().update(ctx.gnss.altitudeMeters, ctx.gnss.isValid, ctx.gnss.hdop, ctx.gnss.speedKmh);
        DeadReckoningService::instance().update(ctx.gnss.isValid, ctx.gnss.speedKmh, ctx.gnss.headingDeg, ctx.gnss.latE7, ctx.gnss.lonE7);
        TunnelService::instance().update(ctx.gnss.latE7, ctx.gnss.lonE7, ctx.gnss.headingDeg, ctx.gnss.speedKmh, ctx.gnss.isValid);
        PowerMonitorService::instance().update();

        // SD Track Logging
        TrackLogger::instance().update();

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void netTask(void* pvParameters) {
    for (;;) {
        WifiNetworkManager::instance().update();

        SystemState& state = SystemState::instance();
        state.lock();
        bool isWifi = state.getContextUnsafe().isWifiConnected;
        bool gnssValid = state.getContextUnsafe().gnss.isValid;
        double lat = state.getContextUnsafe().gnss.latitude;
        double lon = state.getContextUnsafe().gnss.longitude;
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
    PowerDriver::instance().init();
    ButtonDriver::instance().init();
    RtcDriver::instance().init();

    // External M-Bus Wire1 (SDA 32, SCL 33)
    Wire1.begin(PinConfig::EXT_I2C_SDA, PinConfig::EXT_I2C_SCL, 400000);
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
    xTaskCreatePinnedToCore(gnssTask, "gnssTask", 8192, NULL, 5, &gnssTaskHandle, 0);
    xTaskCreatePinnedToCore(netTask, "netTask", 6144, NULL, 2, &netTaskHandle, 0);

    Serial.println("[SYSTEM] System initialization complete. Starting main loop.");
}

void loop() {
    M5.update();
    PowerDriver::instance().update();
    UiManager::instance().update();
    delay(33); // Target ~30 FPS on Core 1
}
