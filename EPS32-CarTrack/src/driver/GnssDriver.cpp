#include "driver/GnssDriver.h"
#include <math.h>

bool GnssDriver::init() {
    if (!_uartMutex) {
        _uartMutex = xSemaphoreCreateRecursiveMutex();
    }
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

    _isInitialized    = true;
    _lastValidFixMs   = millis();
    _lastHotRestartMs = millis();

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

void GnssDriver::setSignalFilterStage(bool strictTrackingMode) {
    if (!_isOnline) return;
    _strictFilterActive = strictTrackingMode;
    if (strictTrackingMode) {
        // Stage 2 (Stable 3D Fix Tracking): reject multipath reflections
        _myGNSS.setVal8(UBLOX_CFG_NAVSPG_INFIL_MINELEV, 10);
        _myGNSS.setVal8(UBLOX_CFG_NAVSPG_INFIL_MINCNO, 20);
        Serial.println("[GNSS] Filter Stage -> TRACKING (minElev=10 deg, minCNO=20 dBHz)");
    } else {
        // Stage 1 (Fast Acquisition after indoor/garage exit): accept weak emerging satellites
        _myGNSS.setVal8(UBLOX_CFG_NAVSPG_INFIL_MINELEV, 5);
        _myGNSS.setVal8(UBLOX_CFG_NAVSPG_INFIL_MINCNO, 14);
        Serial.println("[GNSS] Filter Stage -> ACQUISITION (minElev=5 deg, minCNO=14 dBHz)");
    }
}

void GnssDriver::triggerHotStartReacquisition() {
    _requestHotRestart = true;
}

void GnssDriver::executeHotStartReacquisition() {
    if (!_isOnline) return;
    if (_uartMutex) xSemaphoreTakeRecursive(_uartMutex, portMAX_DELAY);
    Serial.println("[GNSS] Triggering Hot-Start Baseband Reset (UBX-CFG-RST, navBbrMask=0x0000) for fast indoor->outdoor lock...");

    // UBX-CFG-RST (0x06, 0x04), length 4:
    // navBbrMask = 0x0000 (Hotstart: preserve all ephemeris/almanac)
    // resetMode  = 0x02   (Controlled software reset - GNSS only)
    uint8_t frame[12] = {
        0xB5, 0x62, 0x06, 0x04, 0x04, 0x00,
        0x00, 0x00, 0x02, 0x00,
        0x00, 0x00
    };
    uint8_t ck_a = 0, ck_b = 0;
    for (int i = 2; i < 10; i++) {
        ck_a += frame[i];
        ck_b += ck_a;
    }
    frame[10] = ck_a;
    frame[11] = ck_b;
    Serial1.write(frame, 12);
    delay(80);

    // Ensure Stage 1 open acquisition filter and request fresh UTC + Position injection
    setSignalFilterStage(false);
    _myGNSS.setAutoPVT(true);
    _needAssistReinject = true;
    _lastHotRestartMs   = millis();
    if (_uartMutex) xSemaphoreGiveRecursive(_uartMutex);
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

    // 5. Start in Stage 1 Acquisition Mode so emerging from indoor/garage locks immediately
    setSignalFilterStage(false);

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
        if (_uartMutex) xSemaphoreTakeRecursive(_uartMutex, portMAX_DELAY);
        Serial1.write(buffer, size);
        if (_uartMutex) xSemaphoreGiveRecursive(_uartMutex);
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

    if (_uartMutex) xSemaphoreTakeRecursive(_uartMutex, portMAX_DELAY);
    Serial1.write(header, 6);
    Serial1.write(payload, 24);
    Serial1.write(ck_a);
    Serial1.write(ck_b);
    if (_uartMutex) xSemaphoreGiveRecursive(_uartMutex);
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

    if (_uartMutex) xSemaphoreTakeRecursive(_uartMutex, portMAX_DELAY);
    Serial1.write(header, 6);
    Serial1.write(payload, 20);
    Serial1.write(ck_a);
    Serial1.write(ck_b);
    if (_uartMutex) xSemaphoreGiveRecursive(_uartMutex);
    return true;
}

void GnssDriver::queryMonRf() {
    if (!_isOnline) return;
    UBX_MON_RF_data_t rfInfo;
    if (_myGNSS.getRFinformation(&rfInfo, 50) && rfInfo.header.nBlocks > 0) {
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

    if (_requestHotRestart) {
        _requestHotRestart = false;
        executeHotStartReacquisition();
    }

    if (_uartMutex) xSemaphoreTakeRecursive(_uartMutex, portMAX_DELAY);
    _myGNSS.checkUblox();
    bool hasPvt = _myGNSS.getPVT();
    if (_uartMutex) xSemaphoreGiveRecursive(_uartMutex);

    unsigned long now = millis();

    if (hasPvt) {
        if (_uartMutex) xSemaphoreTakeRecursive(_uartMutex, portMAX_DELAY);
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
        uint16_t yr     = _myGNSS.getYear();
        uint8_t  mo     = _myGNSS.getMonth();
        uint8_t  dy     = _myGNSS.getDay();
        uint8_t  hh     = _myGNSS.getHour();
        uint8_t  mm     = _myGNSS.getMinute();
        uint8_t  ss     = _myGNSS.getSecond();
        uint16_t ms     = _myGNSS.getMillisecond();
        if (_uartMutex) xSemaphoreGiveRecursive(_uartMutex);

        _lastObservedSats = numSats;

        // Two-Stage Dynamic Signal Filter Switching (Solves indoor->outdoor slow lock)
        if (valid) {
            _hadValidFixEver = true;
            _lastValidFixMs  = now;
            if (_firstValidFixMs == 0) _firstValidFixMs = now;
            if (!_strictFilterActive && (now - _firstValidFixMs >= 10000) && numSats >= 6) {
                if (_uartMutex) xSemaphoreTakeRecursive(_uartMutex, portMAX_DELAY);
                setSignalFilterStage(true);
                if (_uartMutex) xSemaphoreGiveRecursive(_uartMutex);
            }
        } else {
            _firstValidFixMs = 0;
            if (_strictFilterActive) {
                if (_uartMutex) xSemaphoreTakeRecursive(_uartMutex, portMAX_DELAY);
                setSignalFilterStage(false);
                if (_uartMutex) xSemaphoreGiveRecursive(_uartMutex);
            }
        }

        if (speed < MotionPowerConfig::STANDSTILL_HEADING_LOCK_KMH) {
            speed = 0.0f;
        }

        // Kinematic outlier rejection (> 180 km/h step jump)
        uint32_t nowEpoch = now / 1000;
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

            ctx.gnss.year        = yr;
            ctx.gnss.month       = mo;
            ctx.gnss.day         = dy;
            ctx.gnss.hour        = hh;
            ctx.gnss.minute      = mm;
            ctx.gnss.second      = ss;
            ctx.gnss.millisecond = ms;

            // Zero-intervention auto transition from PageInit to Page1Dashboard on 3D Fix (>=4 sats)
            if (ctx.currentPage == PAGE_INIT && numSats >= 4) {
                ctx.currentPage = PAGE_1_DASHBOARD;
            }
        }
        state.unlock();
    }

    // 45-Second No-Fix Auto Hot-Start Watchdog:
    // Triggers when emerging from indoor/garage (sats visible or lost prior fix) without locking for 45s
    if ((_firstValidFixMs == 0) && (_lastObservedSats > 0 || _hadValidFixEver) &&
        (now - _lastValidFixMs >= 45000) && (now - _lastHotRestartMs >= 45000)) {
        executeHotStartReacquisition();
    }

    // Only poll MON-RF every 5s when fixed, or every 10s when searching so PVT stream is never blocked
    uint32_t monInterval = (_firstValidFixMs > 0) ? 5000 : 10000;
    if (now - _lastMonRfTimeMs >= monInterval) {
        _lastMonRfTimeMs = now;
        if (_uartMutex) xSemaphoreTakeRecursive(_uartMutex, portMAX_DELAY);
        queryMonRf();
        if (_uartMutex) xSemaphoreGiveRecursive(_uartMutex);
    }
}
