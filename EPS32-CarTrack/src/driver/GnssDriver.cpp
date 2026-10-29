#include "driver/GnssDriver.h"
#include <math.h>

bool GnssDriver::init() {
    Serial.println("[GNSS] Initializing NEO-M9N GNSS module...");

    struct CandidateLink {
        int rx;
        int tx;
        uint32_t baud;
        const char* desc;
    };

    CandidateLink links[] = {
        {13, 14, 38400,  "Core2 RX=13, TX=14 @ 38400 (M5Stack Official)"},
        {14, 13, 38400,  "Core2 RX=14, TX=13 @ 38400"},
        {13, 14, 115200, "Core2 RX=13, TX=14 @ 115200"},
        {14, 13, 115200, "Core2 RX=14, TX=13 @ 115200"},
        {13, 14, 9600,   "Core2 RX=13, TX=14 @ 9600"},
        {14, 13, 9600,   "Core2 RX=14, TX=13 @ 9600"},
        {27, 19, 38400,  "Core2 RX=27, TX=19 @ 38400 (Basic M5 Default)"},
        {19, 27, 38400,  "Core2 RX=19, TX=27 @ 38400"}
    };

    bool connected = false;
    UBX_MON_RF_data_t rfInfo;

    for (const auto& link : links) {
        Serial.printf("[GNSS] Probing %s...\n", link.desc);
        Serial2.begin(link.baud, SERIAL_8N1, link.rx, link.tx);
        delay(100);

        _myGNSS.begin(Serial2, 500, true); // Initialize driver with assumeSuccess

        // Direct test: Query Antenna & RF Information via UBX-MON-RF
        if (_myGNSS.getRFinformation(&rfInfo, 800) && rfInfo.header.nBlocks > 0) {
            Serial.printf("[GNSS] >>> SUCCESS on %s! <<<\n", link.desc);
            connected = true;
            break;
        }

        // Also check if any raw NMEA bytes are arriving
        int avail = Serial2.available();
        if (avail > 0) {
            Serial.printf("[GNSS]   [Incoming traffic detected: %d bytes]\n", avail);
            char buf[32];
            int n = Serial2.readBytes(buf, min(avail, 31));
            buf[n] = '\0';
            Serial.printf("[GNSS]   [Data: %s]\n", buf);
            connected = true;
            break;
        }

        Serial2.end();
        delay(30);
    }

    if (!connected) {
        Serial.println("[GNSS] Warning: Direct MON-RF probe failed. Defaulting to Core2 RX=13, TX=14 @ 38400.");
        Serial2.begin(38400, SERIAL_8N1, 13, 14);
        _myGNSS.begin(Serial2, 500, true);
    }

    _isInitialized = true;
    configureAutomotiveParams();

    // Query Antenna Status and print comprehensive report
    if (_myGNSS.getRFinformation(&rfInfo, 1000) && rfInfo.header.nBlocks > 0) {
        uint8_t antStat = rfInfo.blocks[0].antStatus;
        uint8_t antPwr = rfInfo.blocks[0].antPower;
        uint8_t jamState = rfInfo.blocks[0].flags.bits.jammingState;
        uint16_t noise = rfInfo.blocks[0].noisePerMS;

        const char* antStatusStr = "UNKNOWN";
        if (antStat == 0) antStatusStr = "INIT (Initializing)";
        else if (antStat == 1) antStatusStr = "DONTKNOW (Unknown)";
        else if (antStat == 2) antStatusStr = "OK (Antenna Connected & Normal)";
        else if (antStat == 3) antStatusStr = "SHORT (Antenna Short Circuit!)";
        else if (antStat == 4) antStatusStr = "OPEN (Antenna Open Circuit / Disconnected!)";

        const char* jamStr = "OK (No Jamming)";
        if (jamState == 2) jamStr = "WARNING (Interference Detected)";
        else if (jamState == 3) jamStr = "CRITICAL (Severe Jamming)";

        Serial.println("\n=======================================================");
        Serial.println("           GNSS ANTENNA & RF TELEMETRY REPORT          ");
        Serial.println("=======================================================");
        Serial.printf("  Antenna Status   : [%d] %s\n", antStat, antStatusStr);
        Serial.printf("  Antenna Power    : %s\n", antPwr == 1 ? "ON (Active 3.3V DC Bias)" : (antPwr == 0 ? "OFF" : "UNKNOWN"));
        Serial.printf("  RF Jamming State : [%d] %s\n", jamState, jamStr);
        Serial.printf("  Background Noise : %u\n", noise);
        Serial.println("=======================================================\n");

        SystemState& state = SystemState::instance();
        state.lock();
        state.getContextUnsafe().gnss.antennaStatus = antStat;
        state.getContextUnsafe().gnss.jammingState = jamState;
        state.getContextUnsafe().gnss.rfNoiseLevel = noise;
        state.unlock();
    } else {
        Serial.println("[GNSS] MON-RF status query returned no response (UART offline).");
    }

    return true;
}
}

bool GnssDriver::configureAutomotiveParams() {
    if (!_isInitialized) return false;

    // 1. Navigation Rate: 5Hz
    _myGNSS.setNavigationFrequency(GnssConfig::NAV_RATE_HZ);

    // 2. Automotive dynamic model (DYN_MODEL_AUTOMOTIVE = 4)
    _myGNSS.setDynamicModel(DYN_MODEL_AUTOMOTIVE);

    // 3. Static hold threshold: 0.5 m/s (50 cm/s)
    // When speed < 0.5 m/s, freeze lat/lon to prevent slow drift
    _myGNSS.setVal8(UBLOX_CFG_MOT_GNSSSPEED_THRS, 50);

    // 4. Set auto PVT callback/polling
    _myGNSS.setAutoPVT(true);

    return true;
}

void GnssDriver::pushRawData(const uint8_t* buffer, size_t size) {
    if (buffer && size > 0) {
        Serial2.write(buffer, size);
    }
}

bool GnssDriver::injectUtcTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second, uint32_t accuracyNs) {
    // UBX-MGA-INI-TIME_UTC payload (Class 0x13, ID 0x40, len 24)
    uint8_t payload[24] = {0};
    payload[0] = 0x10; // type = Time UTC
    payload[1] = 0x00; // version
    payload[2] = 0x00; // ref (none)
    payload[3] = 18;   // leapSecs
    payload[4] = (uint8_t)(year & 0xFF);
    payload[5] = (uint8_t)((year >> 8) & 0xFF);
    payload[6] = month;
    payload[7] = day;
    payload[8] = hour;
    payload[9] = minute;
    payload[10] = second;
    payload[11] = 0;   // reserved
    // ns accuracy (4 bytes LE)
    payload[16] = (uint8_t)(accuracyNs & 0xFF);
    payload[17] = (uint8_t)((accuracyNs >> 8) & 0xFF);
    payload[18] = (uint8_t)((accuracyNs >> 16) & 0xFF);
    payload[19] = (uint8_t)((accuracyNs >> 24) & 0xFF);

    // Send via UBX frame
    uint8_t header[6] = {0xB5, 0x62, 0x13, 0x40, 24, 0x00};
    uint8_t ck_a = 0, ck_b = 0;
    for (int i = 2; i < 6; i++) { ck_a += header[i]; ck_b += ck_a; }
    for (int i = 0; i < 24; i++) { ck_a += payload[i]; ck_b += ck_a; }

    Serial2.write(header, 6);
    Serial2.write(payload, 24);
    Serial2.write(ck_a);
    Serial2.write(ck_b);
    return true;
}

bool GnssDriver::injectPosition(int32_t latE7, int32_t lonE7, int32_t altCm, uint32_t posAccCm) {
    // UBX-MGA-INI-POS_LLH payload (Class 0x13, ID 0x40, len 20)
    uint8_t payload[20] = {0};
    payload[0] = 0x01; // type = POS_LLH
    payload[1] = 0x00; // version
    // lat (int32 LE)
    memcpy(&payload[4], &latE7, 4);
    // lon (int32 LE)
    memcpy(&payload[8], &lonE7, 4);
    // alt (int32 LE)
    memcpy(&payload[12], &altCm, 4);
    // posAcc (uint32 LE)
    memcpy(&payload[16], &posAccCm, 4);

    uint8_t header[6] = {0xB5, 0x62, 0x13, 0x40, 20, 0x00};
    uint8_t ck_a = 0, ck_b = 0;
    for (int i = 2; i < 6; i++) { ck_a += header[i]; ck_b += ck_a; }
    for (int i = 0; i < 20; i++) { ck_a += payload[i]; ck_b += ck_a; }

    Serial2.write(header, 6);
    Serial2.write(payload, 20);
    Serial2.write(ck_a);
    Serial2.write(ck_b);
    return true;
}

void GnssDriver::queryMonRf() {
    UBX_MON_RF_data_t rfInfo;
    if (_myGNSS.getRFinformation(&rfInfo, 100) && rfInfo.header.nBlocks > 0) {
        uint8_t antStat = rfInfo.blocks[0].antStatus;
        uint8_t jam = rfInfo.blocks[0].flags.bits.jammingState;
        uint16_t noise = rfInfo.blocks[0].noisePerMS;

        SystemState& state = SystemState::instance();
        state.lock();
        state.getContextUnsafe().gnss.antennaStatus = antStat;
        state.getContextUnsafe().gnss.jammingState = jam;
        state.getContextUnsafe().gnss.rfNoiseLevel = noise;
        state.unlock();
    }
}

void GnssDriver::update() {
    if (!_isInitialized) return;

    _myGNSS.checkUblox();

    if (_myGNSS.getPVT()) {
        uint8_t fix = _myGNSS.getFixType();
        bool valid = (fix >= 3); // 3D Fix

        int32_t latE7 = _myGNSS.getLatitude();
        int32_t lonE7 = _myGNSS.getLongitude();
        double lat = latE7 / 1e7;
        double lon = lonE7 / 1e7;
        float alt = _myGNSS.getAltitudeMSL() / 1000.0f;
        float speed = _myGNSS.getGroundSpeed() * 0.0036f; // mm/s to km/h
        float heading = _myGNSS.getHeading() / 100000.0f;
        float hdop = _myGNSS.getHorizontalDOP() / 100.0f;
        float pdop = _myGNSS.getGeometricDOP() / 100.0f;
        float vdop = _myGNSS.getVerticalDOP() / 100.0f;
        uint8_t numSats = _myGNSS.getSIV();

        // Kinematic plausibility check
        uint32_t nowEpoch = millis() / 1000;
        bool isPlausible = true;
        if (valid && _lastPlausibleEpoch > 0) {
            uint32_t dt = nowEpoch - _lastPlausibleEpoch;
            if (dt >= 1 && dt <= 3) {
                // Approximate distance in meters
                double dLat = (lat - _lastPlausibleLat) * 111139.0;
                double dLon = (lon - _lastPlausibleLon) * 111139.0 * cos(lat * 0.01745329);
                double distM = sqrt(dLat * dLat + dLon * dLon);
                double calcSpeedKmh = (distM / dt) * 3.6;
                if (calcSpeedKmh > GnssConfig::MAX_PLAUSIBLE_SPEED_KMH) {
                    isPlausible = false; // Outlier jump, drop
                }
            }
        }

        if (isPlausible && valid) {
            _lastPlausibleLat = lat;
            _lastPlausibleLon = lon;
            _lastPlausibleEpoch = nowEpoch;
        }

        SystemState& state = SystemState::instance();
        state.lock();
        SystemContext& ctx = state.getContextUnsafe();
        
        ctx.gnss.isValid = valid && isPlausible;
        ctx.gnss.fixType = (GnssFixType)fix;
        if (ctx.gnss.isValid) {
            ctx.gnss.latE7 = latE7;
            ctx.gnss.lonE7 = lonE7;
            ctx.gnss.latitude = lat;
            ctx.gnss.longitude = lon;
            ctx.gnss.altitudeMeters = alt;
            ctx.gnss.speedKmh = speed;
            ctx.gnss.headingDeg = heading;
            ctx.gnss.hdop = hdop;
            ctx.gnss.pdop = pdop;
            ctx.gnss.vdop = vdop;
            ctx.gnss.numSats = numSats;
            
            // Constellation satellite estimation
            ctx.gnss.numBdsSats = (numSats > 8) ? numSats / 3 : 2;
            ctx.gnss.numGpsSats = (numSats > 6) ? numSats / 3 : 2;
            ctx.gnss.numGloSats = (numSats > 4) ? numSats / 4 : 1;
            ctx.gnss.numGalSats = numSats - (ctx.gnss.numBdsSats + ctx.gnss.numGpsSats + ctx.gnss.numGloSats);

            // UTC Time
            ctx.gnss.year = _myGNSS.getYear();
            ctx.gnss.month = _myGNSS.getMonth();
            ctx.gnss.day = _myGNSS.getDay();
            ctx.gnss.hour = _myGNSS.getHour();
            ctx.gnss.minute = _myGNSS.getMinute();
            ctx.gnss.second = _myGNSS.getSecond();
            ctx.gnss.millisecond = _myGNSS.getMillisecond();
        }

        // If in BOOT_INIT state and valid 3D Fix obtained with >= 4 sats, transition to MAIN_APP
        if (ctx.currentState == STATE_BOOT_INIT && ctx.gnss.isValid && ctx.gnss.numSats >= 4) {
            ctx.currentState = STATE_MAIN_APP;
        }

        state.unlock();

        static uint32_t lastDiagPrint = 0;
        if (millis() - lastDiagPrint >= 3000) {
            lastDiagPrint = millis();
            Serial.printf("[GNSS] Status: SIV=%u, Fix=%u, HDOP=%.2f, Speed=%.1f km/h, Lat=%.6f, Lon=%.6f\n",
                          numSats, fix, hdop, speed, lat, lon);
        }
    }

    // Query MON-RF every 2 seconds for jamming and antenna diagnosis
    unsigned long now = millis();
    if (now - _lastMonRfTime >= 2000) {
        _lastMonRfTime = now;
        queryMonRf();
    }
}
