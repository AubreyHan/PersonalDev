#include "driver/GnssDriver.h"
#include <math.h>

bool GnssDriver::init() {
    Serial2.begin(BaudConfig::GNSS_DEFAULT_BAUD, SERIAL_8N1, PinConfig::GNSS_RX_PIN, PinConfig::GNSS_TX_PIN);
    delay(50);

    // Try connecting at default baud with short 250ms wait
    if (!_myGNSS.begin(Serial2, 250)) {
        // Try target baud in case already configured
        Serial2.end();
        Serial2.begin(BaudConfig::GNSS_TARGET_BAUD, SERIAL_8N1, PinConfig::GNSS_RX_PIN, PinConfig::GNSS_TX_PIN);
        delay(50);
        if (!_myGNSS.begin(Serial2, 250)) {
            Serial.println("[GNSS] Failed to communicate with NEO-M9N!");
            return false;
        }
    } else {
        // Switch to target baud rate 115200 for high-rate navigation
        _myGNSS.setSerialRate(BaudConfig::GNSS_TARGET_BAUD);
        Serial2.end();
        Serial2.begin(BaudConfig::GNSS_TARGET_BAUD, SERIAL_8N1, PinConfig::GNSS_RX_PIN, PinConfig::GNSS_TX_PIN);
        delay(50);
        _myGNSS.begin(Serial2, 250);
    }

    _isInitialized = true;
    configureAutomotiveParams();
    return true;
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
    // Poll UBX-MON-RF (Class 0x0A, ID 0x38, len 0)
    uint8_t pollMsg[8] = {0xB5, 0x62, 0x0A, 0x38, 0x00, 0x00, 0x42, 0xC4};
    Serial2.write(pollMsg, 8);
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
    }

    // Query MON-RF every 2 seconds for jamming and antenna diagnosis
    unsigned long now = millis();
    if (now - _lastMonRfTime >= 2000) {
        _lastMonRfTime = now;
        queryMonRf();
    }
}
