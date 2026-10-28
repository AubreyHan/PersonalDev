#include <M5Unified.h>
#include <Wire.h>
#include <TinyGPSPlus.h>
#include <Adafruit_BMP280.h>

// Module GNSS UART Pins (Configured via DIP Switch)
// Default Core2: RX=13 (TXD from GNSS), TX=14 (RXD to GNSS)
static const int GNSS_RX_PIN = 13;
static const int GNSS_TX_PIN = 14;
static const uint32_t GNSS_BAUD_RATES[] = {38400, 9600, 115200};

// Module GNSS I2C Pins (M5-Bus Port A on Core2: SDA=32, SCL=33)
static const int EXT_I2C_SDA = 32;
static const int EXT_I2C_SCL = 33;

HardwareSerial gnssSerial(2);
TinyGPSPlus gps;
Adafruit_BMP280 bmp(&Wire1);

bool bmpFound = false;
uint32_t currentGnssBaud = 38400;
unsigned long lastLogTime = 0;
unsigned long lastBaudCheckTime = 0;
uint32_t gnssCharsReceived = 0;

void scanI2CBus(TwoWire &wire, const char *busName) {
    Serial.printf("\n--- Scanning %s ---\n", busName);
    int count = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        wire.beginTransmission(addr);
        if (wire.endTransmission() == 0) {
            Serial.printf("  Found device at 0x%02X", addr);
            if (addr == 0x76) Serial.print(" (BMP280 Barometer)");
            else if (addr == 0x69) Serial.print(" (BMI270 6-Axis IMU)");
            else if (addr == 0x10) Serial.print(" (BMM150 Magnetometer)");
            else if (addr == 0x34) Serial.print(" (AXP192/AXP2101 PMIC)");
            else if (addr == 0x38) Serial.print(" (FT6336U Touch)");
            else if (addr == 0x51) Serial.print(" (BM8563 RTC)");
            Serial.println();
            count++;
        }
    }
    if (count == 0) {
        Serial.printf("  No I2C devices found on %s.\n", busName);
    }
}

void setup() {
    auto cfg = M5.config();
    cfg.internal_imu = true;
    M5.begin(cfg);

    Serial.begin(115200);
    delay(500);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextColor(WHITE, BLACK);
    M5.Display.setTextSize(1);
    M5.Display.drawString("M5Core2 + Module GNSS Test", 10, 8);
    M5.Display.drawFastHLine(0, 24, 320, 0x7BEF);

    Serial.println("\n============================================");
    Serial.println("  M5Core2 v1.3 + Module GNSS Sensor Diagnostic");
    Serial.println("============================================");

    // 1. Scan Internal I2C
    scanI2CBus(Wire, "Internal I2C (G21/G22)");

    // 2. Initialize and Scan External I2C (Module GNSS sensors)
    Wire1.begin(EXT_I2C_SDA, EXT_I2C_SCL, 400000);
    scanI2CBus(Wire1, "External M-Bus I2C (G32/G33)");

    // 3. Initialize BMP280 Barometer
    if (bmp.begin(0x76)) {
        bmpFound = true;
        Serial.println("[BMP280] Sensor initialized successfully (0x76).");
        bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                        Adafruit_BMP280::SAMPLING_X2,
                        Adafruit_BMP280::SAMPLING_X16,
                        Adafruit_BMP280::FILTER_X16,
                        Adafruit_BMP280::STANDBY_MS_500);
    } else {
        Serial.println("[BMP280] Not found at 0x76 on Wire1. Trying 0x77...");
        if (bmp.begin(0x77)) {
            bmpFound = true;
            Serial.println("[BMP280] Sensor initialized successfully (0x77).");
        } else {
            Serial.println("[BMP280] BMP280 not detected. Check M-Bus connection.");
        }
    }

    // 4. Initialize GNSS UART
    gnssSerial.begin(currentGnssBaud, SERIAL_8N1, GNSS_RX_PIN, GNSS_TX_PIN);
    Serial.printf("[GNSS] UART initialized on RX=G%d, TX=G%d at %lu baud.\n", 
                  GNSS_RX_PIN, GNSS_TX_PIN, (unsigned long)currentGnssBaud);

    M5.Display.drawString("Initializing complete. Reading sensors...", 10, 30);
    delay(1000);
    M5.Display.fillScreen(BLACK);
}

void loop() {
    M5.update();

    // Read incoming GNSS characters
    while (gnssSerial.available() > 0) {
        char c = gnssSerial.read();
        gnssCharsReceived++;
        gps.encode(c);
    }

    // Auto-detect GNSS baud rate if no data is received after 4 seconds
    unsigned long now = millis();
    if (gnssCharsReceived == 0 && (now - lastBaudCheckTime > 4000)) {
        lastBaudCheckTime = now;
        static int baudIdx = 0;
        baudIdx = (baudIdx + 1) % 3;
        currentGnssBaud = GNSS_BAUD_RATES[baudIdx];
        gnssSerial.flush();
        gnssSerial.end();
        gnssSerial.begin(currentGnssBaud, SERIAL_8N1, GNSS_RX_PIN, GNSS_TX_PIN);
        Serial.printf("[GNSS] No data yet, trying baud %d on RX:G%d, TX:G%d...\n", 
                      currentGnssBaud, GNSS_RX_PIN, GNSS_TX_PIN);
    }

    // Update display and serial log every 1 second
    if (now - lastLogTime >= 1000) {
        lastLogTime = now;

        // --- Read IMU ---
        float ax = 0, ay = 0, az = 0;
        float gx = 0, gy = 0, gz = 0;
        if (M5.Imu.isEnabled()) {
            M5.Imu.getAccel(&ax, &ay, &az);
            M5.Imu.getGyro(&gx, &gy, &gz);
        }

        // --- Read BMP280 ---
        float temp = 0, press = 0, alt = 0;
        if (bmpFound) {
            temp = bmp.readTemperature();
            press = bmp.readPressure() / 100.0F; // hPa
            alt = bmp.readAltitude(1013.25);
        }

        // --- Read Battery ---
        int batLevel = M5.Power.getBatteryLevel();
        float batVoltage = M5.Power.getBatteryVoltage() / 1000.0f;

        // --- Output to Serial ---
        Serial.println("-------------------------------------------------------");
        Serial.printf("[SYSTEM] Uptime: %lus | Bat: %d%% (%.2fV)\n", now / 1000, batLevel, batVoltage);
        
        if (bmpFound) {
            Serial.printf("[BMP280] Temp: %.2f °C | Press: %.2f hPa | Alt: %.1f m\n", temp, press, alt);
        } else {
            Serial.println("[BMP280] Not connected");
        }

        Serial.printf("[IMU] Accel: [%.2f, %.2f, %.2f] g | Gyro: [%.1f, %.1f, %.1f] dps\n", 
                      ax, ay, az, gx, gy, gz);

        Serial.printf("[GNSS] Chars: %u | Sats: %d | Fix: %s\n", 
                      gnssCharsReceived, gps.satellites.value(),
                      gps.location.isValid() ? "YES" : "WAITING FOR LOCK");

        if (gps.location.isValid()) {
            Serial.printf("       Lat: %.6f | Lon: %.6f | Alt: %.1fm | Speed: %.1f km/h\n",
                          gps.location.lat(), gps.location.lng(), 
                          gps.altitude.meters(), gps.speed.kmph());
        }

        // --- Update Screen ---
        M5.Display.startWrite();
        M5.Display.setTextSize(1);

        // Header
        M5.Display.fillRect(0, 0, 320, 22, NAVY);
        M5.Display.setTextColor(WHITE, NAVY);
        M5.Display.drawString("Core2 v1.3 + GNSS Module", 8, 4);
        char batBuf[20];
        snprintf(batBuf, sizeof(batBuf), "%d%% %.2fV", batLevel, batVoltage);
        M5.Display.drawString(batBuf, 240, 4);

        // GNSS Area
        M5.Display.fillRect(0, 24, 320, 80, BLACK);
        M5.Display.setTextColor(YELLOW, BLACK);
        M5.Display.drawString("--- GNSS (NEO-M9N) ---", 8, 26);
        M5.Display.setTextColor(WHITE, BLACK);
        char gnssLine1[60], gnssLine2[60], gnssLine3[60];
        snprintf(gnssLine1, sizeof(gnssLine1), "Sats: %2d  Fix: %s  Baud: %d", 
                 gps.satellites.value(), gps.location.isValid() ? "LOCKED" : "SEARCHING", currentGnssBaud);
        if (gps.location.isValid()) {
            snprintf(gnssLine2, sizeof(gnssLine2), "Lat: %.6f  Lon: %.6f", 
                     gps.location.lat(), gps.location.lng());
            snprintf(gnssLine3, sizeof(gnssLine3), "Alt: %.1fm  Spd: %.1fkm/h", 
                     gps.altitude.meters(), gps.speed.kmph());
        } else {
            snprintf(gnssLine2, sizeof(gnssLine2), "Waiting for satellite lock...");
            snprintf(gnssLine3, sizeof(gnssLine3), "Chars received: %u (Antenna plugged?)", gnssCharsReceived);
        }
        M5.Display.drawString(gnssLine1, 8, 42);
        M5.Display.drawString(gnssLine2, 8, 58);
        M5.Display.drawString(gnssLine3, 8, 74);

        // Barometer Area
        M5.Display.fillRect(0, 96, 320, 60, BLACK);
        M5.Display.setTextColor(CYAN, BLACK);
        M5.Display.drawString("--- Barometer (BMP280) ---", 8, 98);
        M5.Display.setTextColor(WHITE, BLACK);
        char bmpLine1[60], bmpLine2[60];
        if (bmpFound) {
            snprintf(bmpLine1, sizeof(bmpLine1), "Pressure: %.2f hPa", press);
            snprintf(bmpLine2, sizeof(bmpLine2), "Temp: %.1f C   Est. Alt: %.1f m", temp, alt);
        } else {
            snprintf(bmpLine1, sizeof(bmpLine1), "BMP280: NOT DETECTED (Check M-Bus)");
            bmpLine2[0] = '\0';
        }
        M5.Display.drawString(bmpLine1, 8, 114);
        M5.Display.drawString(bmpLine2, 8, 130);

        // IMU Area
        M5.Display.fillRect(0, 150, 320, 80, BLACK);
        M5.Display.setTextColor(GREEN, BLACK);
        M5.Display.drawString("--- IMU (BMI270) ---", 8, 152);
        M5.Display.setTextColor(WHITE, BLACK);
        char imuLine1[60], imuLine2[60];
        snprintf(imuLine1, sizeof(imuLine1), "Accel: X:%+.2f Y:%+.2f Z:%+.2f g", ax, ay, az);
        snprintf(imuLine2, sizeof(imuLine2), "Gyro : X:%+.1f Y:%+.1f Z:%+.1f dps", gx, gy, gz);
        M5.Display.drawString(imuLine1, 8, 168);
        M5.Display.drawString(imuLine2, 8, 184);

        M5.Display.endWrite();
    }
}
