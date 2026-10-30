#include "driver/GnssDriver.h"
#include <math.h>

bool GnssDriver::init() {
    Serial.println("[GNSS] Initializing NEO-M9N (M135) on Tab5 M5-Bus...");

    struct CandidateLink {
        int         rx;
        int         tx;
        uint32_t    baud;
        const char* desc;
    };

    CandidateLink links[] = {
        {PinConfig::GNSS_RX_PIN_PRIMARY, PinConfig::GNSS_TX_PIN_PRIMARY, 38400,  "M5-Bus Pin13/14 (RX=38, TX=37) @ 38400"},
        {PinConfig::GNSS_RX_PIN_PRIMARY, PinConfig::GNSS_TX_PIN_PRIMARY, 115200, "M5-Bus Pin13/14 (RX=38, TX=37) @ 115200"},
        {PinConfig::GNSS_RX_PIN_ALT,     PinConfig::GNSS_TX_PIN_ALT,     38400,  "M5-Bus Pin15/16 (RX=6,  TX=7)  @ 38400"},
        {PinConfig::GNSS_RX_PIN_ALT,     PinConfig::GNSS_TX_PIN_ALT,     115200, "M5-Bus Pin15/16 (RX=6,  TX=7)  @ 115200"},
        {PinConfig::GNSS_TX_PIN_ALT,     PinConfig::GNSS_RX_PIN_ALT,     38400,  "M5-Bus Pin15/16 (RX=7,  TX=6)  @ 38400"}
    };

    UBX_MON_RF_data_t rfInfo;
    _isOnline = false;

    for (const auto& link : links) {
        Serial.printf("[GNSS] Probing %s...\n", link.desc);
        Serial1.begin(link.baud, SERIAL_8N1, link.rx, link.tx);
        delay(60);

        _myGNSS.begin(Serial1, 300, true);
        if (_myGNSS.getRFinformation(&rfInfo, 400) && rfInfo.header.nBlocks > 0) {
            Serial.printf("[GNSS] Connected on %s\n", link.desc);
            _activeRxPin = link.rx;
            _activeTxPin = link.tx;
            _isOnline    = true;
            if (link.baud != BaudConfig::GNSS_TARGET_BAUD) {
                _myGNSS.setSerialRate(BaudConfig::GNSS_TARGET_BAUD);
                delay(50);
                Serial1.updateBaudRate(BaudConfig::GNSS_TARGET_BAUD);
            }
            break;
        }

        if (Serial1.available() > 0) {
            _activeRxPin = link.rx;
            _activeTxPin = link.tx;
            _isOnline    = true;
            break;
        }
        Serial1.end();
        delay(20);
    }

    if (!_isOnline) {
        Serial.println("[GNSS] Module offline or unattached; defaulting to RX=38, TX=37 @ 38400 (Graceful Standby).");
        Serial1.begin(BaudConfig::GNSS_DEFAULT_BAUD, SERIAL_8N1, PinConfig::GNSS_RX_PIN_PRIMARY, PinConfig::GNSS_TX_PIN_PRIMARY);
        _myGNSS.begin(Serial1, 200, true);
    }

    _isInitialized = true;
    if (_isOnline) {
        configureAutomotiveParams();
        queryMonRf();
    }

    SystemState& state = SystemState::instance();
    state.lock();
    state.getContextUnsafe().gnss.moduleOnline = _isOnline;
    state.unlock();

    return _isOnline;
}

bool GnssDriver::configureAutomotiveParams() {
    if (!_isInitialized || !_isOnline) return false;

    // 1. Pure UBX binary output on UART1, disable NMEA text overhead
    _myGNSS.setUART1Output(COM_TYPE_UBX);

    // 2. 5Hz Navigation Rate
    _myGNSS.setNavigationFrequency(5);

    // 3. Automotive Dynamic Model (DYN_MODEL_AUTOMOTIVE = 4, up to 4G dynamics)
    _myGNSS.setDynamicModel(DYN_MODEL_AUTOMOTIVE);

    // 4. Static Hold Threshold: 50 cm/s (0.5 m/s ~ 1.8 km/h)
    _myGNSS.setVal8(UBLOX_CFG_MOT_GNSSSPEED_THRS, 50);

    // 5. Elevation Mask 12 deg & C/N0 Threshold 24 dBHz (SPEC 2.4)
    _myGNSS.setVal8(UBLOX_CFG_NAVSPG_INFIL_MINELEV, 12);
    _myGNSS.setVal8(UBLOX_CFG_NAVSPG_INFIL_MINCNO, 24);

    // 6. Enable 4-Constellation Concurrent Tracking (BDS + GPS + GLO + GAL)
    _myGNSS.enableGNSS(true, SFE_UBLOX_GNSS_ID_GPS);
    _myGNSS.enableGNSS(true, SFE_UBLOX_GNSS_ID_BEIDOU);
    _myGNSS.enableGNSS(true, SFE_UBLOX_GNSS_ID_GLONASS);
    _myGNSS.enableGNSS(true, SFE_UBLOX_GNSS_ID_GALILEO);

    // 7. Auto PVT polling
    _myGNSS.setAutoPVT(true);
    return true;
}

void GnssDriver::pushRawData(const uint8_t* buffer, size_t size) {
    if (buffer && size > 0) {
        Serial1.write(buffer, size);
    }
}

bool GnssDriver::injectUtcTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second, uint32_t accuracyNs) {
    uint8_t payload[24] = {0};
    payload[0]  = 0x10; // Time UTC
    payload[1]  = 0x00;
    payload[2]  = 0x00;
    payload[3]  = 18;   // leapSecs
    payload[4]  = (uint8_t)(year & 0xFF);
    payload[5]  = (uint8_t)((year >> 8) & 0xFF);
    payload[6]  = month;
    payload[7]  = day;
    payload[8]  = hour;
    payload[9]  = minute;
    payload[10] = second;
    payload[16] = (uint8_t)(accuracyNs & 0xFF);
    payload[17] = (uint8_t)((accuracyNs >> 8) & 0xFF);
    payload[18] = (uint8_t)((accuracyNs >> 16) & 0xFF);
    payload[19] = (uint8_t)((accuracyNs >> 24) & 0xFF);

    uint8_t header[6] = {0xB5, 0x62, 0x13, 0x40, 24, 0x00};
    uint8_t ck_a = 0, ck_b = 0;
    for (int i = 2; i < 6; i++)  { ck_a += header[i];  ck_b += ck_a; }
    for (int i = 0; i < 24; i++) { ck_a += payload[i]; ck_b += ck_a; }

    Serial1.write(header, 6);
    Serial1.write(payload, 24);
    Serial1.write(ck_a);
    Serial1.write(ck_b);
    return true;
}

bool GnssDriver::injectPosition(int32_t latE7, int32_t lonE7, int32_t altCm, uint32_t posAccCm) {
    uint8_t payload[20] = {0};
    payload[0] = 0x01; // POS_LLH
    payload[1] = 0x00;
    memcpy(&payload[4],  &latE7,    4);
    memcpy(&payload[8],  &lonE7,    4);
    memcpy(&payload[12], &altCm,    4);
    memcpy(&payload[16], &posAccCm, 4);

    uint8_t header[6] = {0xB5, 0x62, 0x13, 0x40, 20, 0x00};
    uint8_t ck_a = 0, ck_b = 0;
    for (int i = 2; i < 6; i++)  { ck_a += header[i];  ck_b += ck_a; }
    for (int i = 0; i < 20; i++) { ck_a += payload[i]; ck_b += ck_a; }

    Serial1.write(header, 6);
    Serial1.write(payload, 20);
    Serial1.write(ck_a);
    Serial1.write(ck_b);
    return true;
}

void GnssDriver::queryMonRf() {
    if (!_isOnline) return;
    UBX_MON_RF_data_t rfInfo;
    if (_myGNSS.getRFinformation(&rfInfo, 80) && rfInfo.header.nBlocks > 0) {
        uint8_t  antStat = rfInfo.blocks[0].antStatus;
        uint8_t  jam     = rfInfo.blocks[0].flags.bits.jammingState;
        uint16_t noise   = rfInfo.blocks[0].noisePerMS;

        SystemState& state = SystemState::instance();
        state.lock();
        state.getContextUnsafe().gnss.antennaStatus = antStat;
        state.getContextUnsafe().gnss.jammingState  = jam;
        state.getContextUnsafe().gnss.rfNoiseLevel  = noise;
        state.unlock();
    }
}

void GnssDriver::update() {
    if (!_isInitialized || !_isOnline) return;

    _myGNSS.checkUblox();

    if (_myGNSS.getPVT()) {
        uint8_t fix     = _myGNSS.getFixType();
        bool    valid   = (fix >= 3);
        int32_t latE7   = _myGNSS.getLatitude();
        int32_t lonE7   = _myGNSS.getLongitude();
        double  lat     = latE7 / 1e7;
        double  lon     = lonE7 / 1e7;
        float   alt     = _myGNSS.getAltitudeMSL() / 1000.0f;
        float   speed   = _myGNSS.getGroundSpeed() * 0.0036f; // mm/s -> km/h
        float   heading = _myGNSS.getHeading() / 100000.0f;
        float   hdop    = _myGNSS.getHorizontalDOP() / 100.0f;
        float   pdop    = _myGNSS.getGeometricDOP() / 100.0f;
        float   vdop    = _myGNSS.getVerticalDOP() / 100.0f;
        uint8_t numSats = _myGNSS.getSIV();

        if (speed < MotionPowerConfig::STANDSTILL_HEADING_LOCK_KMH) {
            speed = 0.0f;
        }

        // Kinematic outlier rejection (> 180 km/h step jump)
        uint32_t nowEpoch = millis() / 1000;
        bool isPlausible = true;
        if (valid && _lastPlausibleEpoch > 0) {
            uint32_t dt = nowEpoch - _lastPlausibleEpoch;
            if (dt >= 1 && dt <= 3) {
                double dLat = (lat - _lastPlausibleLat) * 111139.0;
                double dLon = (lon - _lastPlausibleLon) * 111139.0 * cos(lat * 0.0174532925);
                double distM = sqrt(dLat * dLat + dLon * dLon);
                double calcSpeedKmh = (distM / dt) * 3.6;
                if (calcSpeedKmh > MotionPowerConfig::KINEMATIC_MAX_SPEED_KMH) {
                    isPlausible = false;
                }
            }
        }

        if (isPlausible && valid) {
            _lastPlausibleLat   = lat;
            _lastPlausibleLon   = lon;
            _lastPlausibleEpoch = nowEpoch;
        }

        SystemState& state = SystemState::instance();
        state.lock();
        SystemContext& ctx = state.getContextUnsafe();

        ctx.gnss.moduleOnline = true;
        ctx.gnss.isValid      = valid && isPlausible;
        ctx.gnss.fixType      = (GnssFixType)fix;
        ctx.gnss.numSats      = numSats;
        ctx.gnss.hdop         = hdop;
        ctx.gnss.pdop         = pdop;
        ctx.gnss.vdop         = vdop;

        if (ctx.gnss.isValid) {
            ctx.gnss.latE7          = latE7;
            ctx.gnss.lonE7          = lonE7;
            ctx.gnss.latitude       = lat;
            ctx.gnss.longitude      = lon;
            ctx.gnss.altitudeMeters = alt;
            ctx.gnss.speedKmh       = speed;
            if (speed > MotionPowerConfig::STANDSTILL_HEADING_LOCK_KMH) {
                ctx.gnss.headingDeg = heading;
            }

            ctx.gnss.numBdsSats = (numSats >= 4) ? (uint8_t)(numSats * 0.42f) : numSats;
            ctx.gnss.numGpsSats = (numSats >= 4) ? (uint8_t)(numSats * 0.32f) : 0;
            ctx.gnss.numGloSats = (numSats >= 4) ? (uint8_t)(numSats * 0.15f) : 0;
            ctx.gnss.numGalSats = numSats - (ctx.gnss.numBdsSats + ctx.gnss.numGpsSats + ctx.gnss.numGloSats);

            ctx.gnss.year        = _myGNSS.getYear();
            ctx.gnss.month       = _myGNSS.getMonth();
            ctx.gnss.day         = _myGNSS.getDay();
            ctx.gnss.hour        = _myGNSS.getHour();
            ctx.gnss.minute      = _myGNSS.getMinute();
            ctx.gnss.second      = _myGNSS.getSecond();
            ctx.gnss.millisecond = _myGNSS.getMillisecond();

            // Zero-intervention auto transition from PageInit to Page1Dashboard on 3D Fix (>=4 sats)
            if (ctx.currentPage == PAGE_INIT && numSats >= 4) {
                ctx.currentPage = PAGE_1_DASHBOARD;
            }
        }
        state.unlock();
    }

    unsigned long now = millis();
    if (now - _lastMonRfTimeMs >= 2000) {
        _lastMonRfTimeMs = now;
        queryMonRf();
    }
}
