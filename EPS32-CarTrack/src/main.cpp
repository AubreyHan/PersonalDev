#include <M5Unified.h>

void setup() {
    auto cfg = M5.config();
    cfg.internal_imu = false;
    M5.begin(cfg);

    Serial.begin(115200);
    delay(200);

    Serial.println("\n=======================================================");
    Serial.println("  M5Core2 + M5GO Bottom2 Battery Diagnostic Tool");
    Serial.println("=======================================================");

    // Make sure AXP192 battery detection and ADC are active
    // Reg 0x32: bit 6 is Battery detection enable
    uint8_t reg32 = M5.In_I2C.readRegister8(0x34, 0x32, 400000);
    M5.In_I2C.writeRegister8(0x34, 0x32, reg32 | 0b01000010, 400000);

    // Reg 0x82: ADC enable 1 (all on = 0xFF)
    M5.In_I2C.writeRegister8(0x34, 0x82, 0xFF, 400000);

    // Enable charging: 4.2V, 500mA
    M5.Power.setBatteryCharge(true);
    M5.Power.setChargeVoltage(4200);
    M5.Power.setChargeCurrent(500);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void loop() {
    M5.update();

    // Read AXP192 status
    int16_t vbatMv = M5.Power.getBatteryVoltage();
    int32_t ibatMa = M5.Power.getBatteryCurrent();
    int32_t soc    = M5.Power.getBatteryLevel();
    bool    chg    = M5.Power.isCharging();
    int16_t vbusMv = M5.Power.getVBUSVoltage();
    float   ibusMa = M5.Power.Axp192.getVBUSCurrent();

    uint8_t reg00 = M5.In_I2C.readRegister8(0x34, 0x00, 400000);
    uint8_t reg01 = M5.In_I2C.readRegister8(0x34, 0x01, 400000);
    uint8_t reg32 = M5.In_I2C.readRegister8(0x34, 0x32, 400000);
    uint8_t reg33 = M5.In_I2C.readRegister8(0x34, 0x33, 400000);
    uint8_t reg82 = M5.In_I2C.readRegister8(0x34, 0x82, 400000);

    bool batPresent = (reg01 & 0x20) != 0;

    // Print to Serial every 1 second
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 1000) {
        lastPrint = millis();
        Serial.printf("[BATTERY] VBAT=%d mV | IBAT=%+d mA | SOC=%d%% | Chg=%d | Present=%d | VBUS=%d mV (%.1f mA) | Reg[00=%02X, 01=%02X, 32=%02X, 33=%02X, 82=%02X]\n",
                      (int)vbatMv, (int)ibatMa, (int)soc, (int)chg, (int)batPresent, (int)vbusMv, ibusMa,
                      reg00, reg01, reg32, reg33, reg82);
    }

    // Refresh LCD
    static uint32_t lastDraw = 0;
    if (millis() - lastDraw > 250) {
        lastDraw = millis();
        char buf[64];

        M5.Display.startWrite();
        // Title banner
        M5.Display.fillRect(0, 0, 320, 28, 0x18E3);
        M5.Display.setTextColor(TFT_WHITE, 0x18E3);
        M5.Display.setTextSize(1);
        M5.Display.drawString("  Core2 + M5GO Bottom2 Power Monitor", 8, 6);

        // Battery Voltage Card
        uint16_t cardBg = (vbatMv > 3000) ? 0x0320 : 0x4000;
        uint16_t vbatColor = (vbatMv > 3200) ? TFT_GREEN : ((vbatMv > 2500) ? TFT_YELLOW : TFT_RED);
        M5.Display.fillRoundRect(8, 34, 150, 75, 6, cardBg);
        M5.Display.drawRoundRect(8, 34, 150, 75, 6, vbatColor);
        M5.Display.setTextColor(0xBDD7, cardBg);
        M5.Display.drawString("BATTERY VOLTAGE", 16, 40);
        M5.Display.setTextColor(vbatColor, cardBg);
        M5.Display.setTextSize(2);
        snprintf(buf, sizeof(buf), "%d mV", (int)vbatMv);
        M5.Display.drawString(buf, 16, 56);
        M5.Display.setTextSize(1);
        snprintf(buf, sizeof(buf), "%.2f V | %d%%", vbatMv / 1000.0f, (int)soc);
        M5.Display.drawString(buf, 16, 80);

        // Status Card
        M5.Display.fillRoundRect(164, 34, 148, 75, 6, 0x18E3);
        M5.Display.drawRoundRect(164, 34, 148, 75, 6, TFT_WHITE);
        M5.Display.setTextColor(0xBDD7, 0x18E3);
        M5.Display.drawString("BATTERY STATUS", 172, 40);
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(batPresent ? TFT_GREEN : TFT_RED, 0x18E3);
        M5.Display.drawString(batPresent ? "Present: YES" : "Present: NO (0V)", 172, 58);
        M5.Display.setTextColor(chg ? TFT_CYAN : TFT_WHITE, 0x18E3);
        snprintf(buf, sizeof(buf), "Chg: %s (%+d mA)", chg ? "CHARGING" : "STANDBY", (int)ibatMa);
        M5.Display.drawString(buf, 172, 75);

        // USB / VBUS Card
        M5.Display.fillRoundRect(8, 114, 304, 40, 4, 0x10A2);
        M5.Display.setTextColor(TFT_WHITE, 0x10A2);
        snprintf(buf, sizeof(buf), "VBUS (USB): %d mV | Current: %.1f mA", (int)vbusMv, ibusMa);
        M5.Display.drawString(buf, 16, 126);

        // Raw Registers Card
        M5.Display.fillRoundRect(8, 158, 304, 45, 4, 0x0841);
        M5.Display.setTextColor(TFT_YELLOW, 0x0841);
        snprintf(buf, sizeof(buf), "AXP192: [00]=0x%02X [01]=0x%02X [32]=0x%02X", reg00, reg01, reg32);
        M5.Display.drawString(buf, 16, 165);
        snprintf(buf, sizeof(buf), "        [33]=0x%02X [82]=0x%02X", reg33, reg82);
        M5.Display.drawString(buf, 16, 182);

        // Bottom Touch Buttons Indicators
        M5.Display.fillRect(0, 208, 320, 32, TFT_BLACK);
        M5.Display.drawFastHLine(0, 208, 320, 0x39E7);
        M5.Display.setTextColor(M5.BtnA.isPressed() ? TFT_GREEN : 0x7BEF, TFT_BLACK);
        M5.Display.drawString("BtnA: 500mA", 20, 218);
        M5.Display.setTextColor(M5.BtnB.isPressed() ? TFT_GREEN : 0x7BEF, TFT_BLACK);
        M5.Display.drawString("BtnB: Chg On/Off", 120, 218);
        M5.Display.setTextColor(M5.BtnC.isPressed() ? TFT_GREEN : 0x7BEF, TFT_BLACK);
        M5.Display.drawString("BtnC: Vib Test", 230, 218);

        M5.Display.endWrite();
    }

    if (M5.BtnC.wasPressed()) {
        M5.Power.setVibration(200);
        delay(150);
        M5.Power.setVibration(0);
    }

    if (M5.BtnB.wasPressed()) {
        static bool chgEnable = true;
        chgEnable = !chgEnable;
        M5.Power.setBatteryCharge(chgEnable);
    }

    delay(10);
}
