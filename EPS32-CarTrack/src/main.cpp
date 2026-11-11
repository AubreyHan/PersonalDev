#include <Arduino.h>
#include <LittleFS.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#include <TinyGPS++.h>
#include <SdFat.h>
#include <ArduinoJson.h>

#include "config.h"
#include "model/SystemState.h"
#include "ui/pages/BasePage.h"

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    cfg.clear_display   = true;
    cfg.output_power    = true; // Enables U6 P2 EXT5V_EN for M5-Bus Module GNSS (M135)
    cfg.internal_imu    = true; // Tab5 onboard BMI270 Master (0x68)
    cfg.internal_rtc    = true; // Tab5 onboard RX8130CE RTC (0x32)
    cfg.internal_spk    = true; // Tab5 onboard ES8388 + NS4150B speaker
    cfg.internal_mic    = false;

    M5.begin(cfg);
    delay(Esp32C6SdioConfig::POST_POWERON_DELAY_MS);

    M5.Display.setRotation(Viewport::LANDSCAPE_ROTATION);
    M5.Display.setBrightness(180);
    M5.Display.fillScreen(TFT_BLACK);

    SystemState::instance().init();
    const bool lfsMounted = LittleFS.begin(
        true,
        LittleFsConfig::BASE_PATH,
        10,
        LittleFsConfig::PARTITION_LABEL
    );

    {
        StateGuard lock(SystemState::instance());
        auto& st             = SystemState::instance().data();
        st.boot.psramOk      = (psramFound() && ESP.getPsramSize() > 0);
        st.boot.littlefsOk   = lfsMounted;
        st.boot.rtcOnline    = M5.Rtc.isEnabled();
        st.boot.imuMasterOnline = M5.Imu.isEnabled();
    }

    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextColor(ThemeColors::RGB565_NEON_GREEN, TFT_BLACK);
    M5.Display.drawString("M5Track-Tab5 Ready", Viewport::SCREEN_W / 2, Viewport::SCREEN_H / 2, &fonts::Font4);

    Serial.printf(
        "[PHASE1] M5Track-Tab5 baseline initialized | PSRAM=%u bytes | LittleFS=%s | StateSnap=%u bytes\n",
        static_cast<unsigned>(ESP.getPsramSize()),
        lfsMounted ? "OK" : "FAIL",
        static_cast<unsigned>(sizeof(SystemStateSnapshot))
    );
}

void loop() {
    M5.update();
    vTaskDelay(pdMS_TO_TICKS(20));
}
