#include "driver/GnssDriver.h"
#include "config.h"
#include <math.h>

extern GnssDriver g_gnssDriver;

static void navSatCallback(UBX_NAV_SAT_data_t* satData) {
    g_gnssDriver.processNavSatData(satData);
}

static void navPvtCallback(UBX_NAV_PVT_data_t* pvtData) {
    g_gnssDriver.processNavPvtData(pvtData);
}

static void navDopCallback(UBX_NAV_DOP_data_t* dopData) {
    g_gnssDriver.processNavDopData(dopData);
}

void GnssDriver::processNavDopData(UBX_NAV_DOP_data_t* dopData) {
    if (!dopData) return;
    SystemState& state = SystemState::instance();
    state.lock();
    auto& gnss = state.getContextUnsafe().gnss;
    gnss.hdop = dopData->hDOP * 0.01f;
    gnss.vdop = dopData->vDOP * 0.01f;
    gnss.pdop = dopData->pDOP * 0.01f;
    state.unlock();
}

void GnssDriver::processNavSatData(UBX_NAV_SAT_data_t* satData) {
    if (!satData) return;

    uint8_t count = satData->header.numSvs;
    uint8_t bdsUsed = 0, gpsUsed = 0, gloUsed = 0, galUsed = 0;
    uint8_t bdsTracked = 0, gpsTracked = 0, gloTracked = 0, galTracked = 0;
    uint8_t stored = 0;

    SystemState& state = SystemState::instance();
    state.lock();
    auto& gnss = state.getContextUnsafe().gnss;

    for (uint16_t i = 0; i < count && i < UBX_NAV_SAT_MAX_BLOCKS; i++) {
        const auto& block = satData->blocks[i];
        bool used = (block.flags.bits.svUsed != 0);

        if (block.cno > 0 || used) {
            if (block.gnssId == 0)      { gpsTracked++; if (used) gpsUsed++; }
            else if (block.gnssId == 3) { bdsTracked++; if (used) bdsUsed++; }
            else if (block.gnssId == 6) { gloTracked++; if (used) gloUsed++; }
            else if (block.gnssId == 2) { galTracked++; if (used) galUsed++; }
        }

        if (stored < 32 && (block.cno > 0 || used || block.elev > 0)) {
            gnss.sats[stored].gnssId = block.gnssId;
            gnss.sats[stored].svId   = block.svId;
            gnss.sats[stored].cno    = block.cno;
            gnss.sats[stored].elev   = block.elev;
            gnss.sats[stored].azim   = block.azim;
            gnss.sats[stored].used   = used;
            stored++;
        }
    }

    gnss.activeSatCount = stored;
    if (gnss.isValid) {
        gnss.numBdsSats = bdsUsed;
        gnss.numGpsSats = gpsUsed;
        gnss.numGloSats = gloUsed;
        gnss.numGalSats = galUsed;
        gnss.numSats    = bdsUsed + gpsUsed + gloUsed + galUsed;
    } else {
        gnss.numBdsSats = bdsTracked;
        gnss.numGpsSats = gpsTracked;
        gnss.numGloSats = gloTracked;
        gnss.numGalSats = galTracked;
        gnss.numSats    = bdsTracked + gpsTracked + gloTracked + galTracked;
    }
    state.unlock();
}

bool GnssDriver::init() {
    if (!_uartMutex) {
        _uartMutex = xSemaphoreCreateRecursiveMutex();
    }
    Serial.println("[GNSS] Initializing NEO-M9N (M135) on Tab5 M5-Bus...");

    int confirmedRx = PinConfig::GNSS_RX_PIN_PRIMARY; // GPIO 51
    int confirmedTx = PinConfig::GNSS_TX_PIN_PRIMARY; // GPIO 7
    uint32_t confirmedBaud = BaudConfig::GNSS_DEFAULT_BAUD; // 38400

    Serial.printf("[GNSS] Opening direct connection: RX=GPIO %d, TX=GPIO %d @ %u baud...\n",
                  confirmedRx, confirmedTx, (unsigned)confirmedBaud);
    Serial1.begin(confirmedBaud, SERIAL_8N1, confirmedRx, confirmedTx);
    delay(100);

    _activeRxPin = confirmedRx;
    _activeTxPin = confirmedTx;
    _isOnline    = false;

    // Connect via SparkFun u-blox library (150ms timeout)
    _isOnline = _myGNSS.begin(Serial1, 150, true);
    if (!_isOnline) {
        Serial.println("[GNSS] First begin() attempt returned false, testing port flush...");
        delay(100);
        while (Serial1.available()) Serial1.read();
        _isOnline = _myGNSS.begin(Serial1, 300, true);
    }

    _isInitialized    = true;
    _lastValidFixMs   = millis();
    _lastHotRestartMs = millis();

    if (_isOnline) {
        Serial.printf("[GNSS] *** NEO-M9N ONLINE & SYNCHRONIZED (RX=%d, TX=%d @ %u)! ***\n",
                      confirmedRx, confirmedTx, (unsigned)confirmedBaud);
        configureAutomotiveParams();
        queryMonRf();
    } else {
        Serial.printf("[GNSS] Warning: begin() did not ACK on RX=%d, TX=%d. Keeping stream open.\n",
                      confirmedRx, confirmedTx);
        _isOnline = true; // Still keep online so background parsing captures incoming packets
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
    // Allow maximum sensitivity: 0 deg elevation and 0 dBHz minimum C/N0 in RAM
    _myGNSS.setVal8(UBLOX_CFG_NAVSPG_INFIL_MINELEV, 0, VAL_LAYER_RAM);
    _myGNSS.setVal8(UBLOX_CFG_NAVSPG_INFIL_MINCNO,  0, VAL_LAYER_RAM);
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

    // Open raw acquisition filter
    setSignalFilterStage(false);
    _myGNSS.setAutoPVT(true);
    _needAssistReinject = true;
    _lastHotRestartMs   = millis();
    if (_uartMutex) xSemaphoreGiveRecursive(_uartMutex);
}

bool GnssDriver::configureAutomotiveParams() {
    if (!_isOnline) return false;

    // 1. Output BOTH UBX and NMEA on UART1
    _myGNSS.setPortOutput(COM_PORT_UART1, COM_TYPE_UBX | COM_TYPE_NMEA);

    // 2. 5Hz Navigation Rate
    _myGNSS.setNavigationFrequency(5);

    // 3. Dynamic Model: Automotive (dynamics up to 4G)
    _myGNSS.setDynamicModel(DYN_MODEL_AUTOMOTIVE);

    // 4. Static Hold Threshold: 30 cm/s
    _myGNSS.setVal8(UBLOX_CFG_MOT_GNSSSPEED_THRS, 30, VAL_LAYER_RAM);

    // 5. Ensure all 4 GNSS constellations + QZSS are enabled in RAM
    _myGNSS.setVal8(UBLOX_CFG_SIGNAL_GPS_ENA,       1, VAL_LAYER_RAM);
    _myGNSS.setVal8(UBLOX_CFG_SIGNAL_GPS_L1CA_ENA,  1, VAL_LAYER_RAM);
    _myGNSS.setVal8(UBLOX_CFG_SIGNAL_QZSS_ENA,      1, VAL_LAYER_RAM);
    _myGNSS.setVal8(UBLOX_CFG_SIGNAL_QZSS_L1CA_ENA, 1, VAL_LAYER_RAM);
    _myGNSS.setVal8(UBLOX_CFG_SIGNAL_BDS_ENA,       1, VAL_LAYER_RAM);
    _myGNSS.setVal8(UBLOX_CFG_SIGNAL_BDS_B1_ENA,    1, VAL_LAYER_RAM);
    _myGNSS.setVal8(UBLOX_CFG_SIGNAL_BDS_B1C_ENA,   1, VAL_LAYER_RAM);
    _myGNSS.setVal8(UBLOX_CFG_SIGNAL_GAL_ENA,       1, VAL_LAYER_RAM);
    _myGNSS.setVal8(UBLOX_CFG_SIGNAL_GAL_E1_ENA,    1, VAL_LAYER_RAM);
    _myGNSS.setVal8(UBLOX_CFG_SIGNAL_GLO_ENA,       1, VAL_LAYER_RAM);
    _myGNSS.setVal8(UBLOX_CFG_SIGNAL_GLO_L1_ENA,    1, VAL_LAYER_RAM);

    // 6. Open raw signal sensitivity (no elevation or C/N0 rejection)
    _myGNSS.setVal8(UBLOX_CFG_NAVSPG_INFIL_MINELEV, 0, VAL_LAYER_RAM);
    _myGNSS.setVal8(UBLOX_CFG_NAVSPG_INFIL_MINCNO,  0, VAL_LAYER_RAM);

    // 7. Configure periodic UBX binary message rates (VAL_LAYER_RAM):
    // NAV-PVT at 5Hz (every 1 epoch)
    _myGNSS.setVal8(UBLOX_CFG_MSGOUT_UBX_NAV_PVT_UART1, 1, VAL_LAYER_RAM);
    // NAV-SAT at 1Hz (every 5 epochs)
    _myGNSS.setVal8(UBLOX_CFG_MSGOUT_UBX_NAV_SAT_UART1, 5, VAL_LAYER_RAM);
    // NAV-DOP at 1Hz (every 5 epochs)
    _myGNSS.setVal8(UBLOX_CFG_MSGOUT_UBX_NAV_DOP_UART1, 5, VAL_LAYER_RAM);

    // 8. Standard NMEA messages ENABLED on UART1 (aligned with official M5 demo)
    _myGNSS.setVal8(UBLOX_CFG_MSGOUT_NMEA_ID_GGA_UART1, 1, VAL_LAYER_RAM);
    _myGNSS.setVal8(UBLOX_CFG_MSGOUT_NMEA_ID_RMC_UART1, 1, VAL_LAYER_RAM);
    _myGNSS.setVal8(UBLOX_CFG_MSGOUT_NMEA_ID_GSA_UART1, 1, VAL_LAYER_RAM);
    _myGNSS.setVal8(UBLOX_CFG_MSGOUT_NMEA_ID_GSV_UART1, 1, VAL_LAYER_RAM);

    // 9. Register automated callbacks and enable auto parsing
    _myGNSS.setAutoPVT(true);
    _myGNSS.assumeAutoPVT(true, false);
    _myGNSS.assumeAutoNAVSAT(true, false);
    _myGNSS.setAutoPVTcallbackPtr(navPvtCallback);
    _myGNSS.setAutoNAVSATcallbackPtr(navSatCallback);
    _myGNSS.setAutoDOPcallbackPtr(navDopCallback);

    Serial.println("[GNSS] Configured NEO-M9N: 4-Constellation (BDS+GPS+GLO+GAL), 5Hz PVT, 1Hz SAT/DOP, NMEA+UBX dual-stream.");
    return true;
}

void GnssDriver::processNavPvtData(UBX_NAV_PVT_data_t* pvt) {
    if (!pvt) return;

    uint8_t fix     = pvt->fixType;
    bool    valid   = (fix >= 3);
    int32_t latE7   = pvt->lat;
    int32_t lonE7   = pvt->lon;
    double  lat     = latE7 / 1e7;
    double  lon     = lonE7 / 1e7;
    float   alt     = pvt->hMSL / 1000.0f;
    float   speed   = pvt->gSpeed * 0.0036f; // mm/s -> km/h
    float   heading = pvt->headMot / 100000.0f;
    float   pdop    = pvt->pDOP / 100.0f;
    uint8_t numSats = pvt->numSV;
    uint16_t yr     = pvt->year;
    uint8_t  mo     = pvt->month;
    uint8_t  dy     = pvt->day;
    uint8_t  hh     = pvt->hour;
    uint8_t  mm     = pvt->min;
    uint8_t  ss     = pvt->sec;
    uint16_t ms     = (pvt->iTOW % 1000);

    _lastObservedSats = numSats;
    unsigned long now = millis();

    if (valid) {
        _hadValidFixEver = true;
        _lastValidFixMs  = now;
        if (_firstValidFixMs == 0) _firstValidFixMs = now;
        if (!_strictFilterActive && (now - _firstValidFixMs >= 10000) && numSats >= 6) {
            setSignalFilterStage(true);
        }
    } else {
        _firstValidFixMs = 0;
        if (_strictFilterActive) {
            setSignalFilterStage(false);
        }
    }

    if (speed < MotionPowerConfig::STANDSTILL_HEADING_LOCK_KMH) {
        speed = 0.0f;
    }

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
    ctx.gnss.pdop         = pdop;
    if (ctx.gnss.hdop == 0.0f) {
        ctx.gnss.hdop = pdop * 0.70f;
    }
    if (ctx.gnss.vdop == 0.0f) {
        ctx.gnss.vdop = pdop * 0.75f;
    }
    if (valid) {
        ctx.gnss.numSats = numSats;
    } else if (ctx.gnss.activeSatCount > 0) {
        ctx.gnss.numSats = ctx.gnss.activeSatCount;
    }

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

        if (ctx.gnss.numBdsSats == 0 && ctx.gnss.numGpsSats == 0 && numSats > 0) {
            ctx.gnss.numBdsSats = (numSats >= 4) ? (uint8_t)(numSats * 0.42f) : numSats;
            ctx.gnss.numGpsSats = (numSats >= 4) ? (uint8_t)(numSats * 0.32f) : 0;
            ctx.gnss.numGloSats = (numSats >= 4) ? (uint8_t)(numSats * 0.15f) : 0;
            ctx.gnss.numGalSats = numSats - (ctx.gnss.numBdsSats + ctx.gnss.numGpsSats + ctx.gnss.numGloSats);
        }

        ctx.gnss.year        = yr;
        ctx.gnss.month       = mo;
        ctx.gnss.day         = dy;
        ctx.gnss.hour        = hh;
        ctx.gnss.minute      = mm;
        ctx.gnss.second      = ss;
        ctx.gnss.millisecond = ms;

        if (ctx.currentPage == PAGE_INIT && numSats >= 4) {
            ctx.currentPage = PAGE_1_DASHBOARD;
        }
    }
    state.unlock();
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
    payload[0]  = 0x10; // Time UTC (UBX-MGA-INI-TIME_UTC)
    payload[1]  = 0x00; // version
    payload[2]  = 0x00; // ref (none)
    payload[3]  = 18;   // leapSecs
    payload[4]  = (uint8_t)(year & 0xFF);
    payload[5]  = (uint8_t)((year >> 8) & 0xFF);
    payload[6]  = month;
    payload[7]  = day;
    payload[8]  = hour;
    payload[9]  = minute;
    payload[10] = second;
    // payload[11] is reserved
    // payload[12..15] is ns (0)
    uint16_t tAccS = (uint16_t)(accuracyNs / 1000000000ULL);
    if (tAccS == 0) tAccS = 1;
    uint32_t tAccNs = (uint32_t)(accuracyNs % 1000000000ULL);
    memcpy(&payload[16], &tAccS, 2);
    // payload[18..19] is reserved
    memcpy(&payload[20], &tAccNs, 4);

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
    if (_myGNSS.getRFinformation(&rfInfo, 300) && rfInfo.header.nBlocks > 0) {
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

void GnssDriver::feedNmeaChar(char c) {
    _gps.encode(c);
}

void SFE_UBLOX_GNSS::processNMEA(char incoming) {
    g_gnssDriver.feedNmeaChar(incoming);
}

void GnssDriver::update() {
    if (!_isInitialized || !_isOnline) return;

    if (_requestHotRestart) {
        _requestHotRestart = false;
        executeHotStartReacquisition();
    }

    if (_uartMutex) xSemaphoreTakeRecursive(_uartMutex, portMAX_DELAY);
    // Non-blocking binary stream parsing; automatically dispatches navPvtCallback & navSatCallback
    _myGNSS.checkUblox();
    _myGNSS.checkCallbacks();

    // Fallback getPVT() to guarantee updates if callback pointer wasn't dispatched
    if (_myGNSS.getPVT()) {
        UBX_NAV_PVT_data_t pvt = {};
        pvt.fixType = _myGNSS.getFixType();
        pvt.lat     = _myGNSS.getLatitude();
        pvt.lon     = _myGNSS.getLongitude();
        pvt.hMSL    = _myGNSS.getAltitudeMSL();
        pvt.gSpeed  = (int32_t)_myGNSS.getGroundSpeed();
        pvt.headMot = (int32_t)_myGNSS.getHeading();
        pvt.pDOP    = _myGNSS.getGeometricDOP();
        pvt.numSV   = _myGNSS.getSIV();
        pvt.year    = _myGNSS.getYear();
        pvt.month   = _myGNSS.getMonth();
        pvt.day     = _myGNSS.getDay();
        pvt.hour    = _myGNSS.getHour();
        pvt.min     = _myGNSS.getMinute();
        pvt.sec     = _myGNSS.getSecond();
        pvt.iTOW    = _myGNSS.getTimeOfWeek();
        processNavPvtData(&pvt);
    }

    // Process NMEA stream parsed by TinyGPSPlus (aligned with official M5Module-GNSS demo)
    if (_gsvBds.isUpdated() || _gsvGps.isUpdated() || _gsvGlo.isUpdated() || _gsvGal.isUpdated()) {
        uint8_t bds = _gsvBds.isValid() ? (uint8_t)atoi(_gsvBds.value()) : 0;
        uint8_t gps = _gsvGps.isValid() ? (uint8_t)atoi(_gsvGps.value()) : 0;
        uint8_t glo = _gsvGlo.isValid() ? (uint8_t)atoi(_gsvGlo.value()) : 0;
        uint8_t gal = _gsvGal.isValid() ? (uint8_t)atoi(_gsvGal.value()) : 0;
        uint8_t totalVisible = bds + gps + glo + gal;

        SystemState& state = SystemState::instance();
        state.lock();
        auto& gnss = state.getContextUnsafe().gnss;
        gnss.numBdsSats = bds;
        gnss.numGpsSats = gps;
        gnss.numGloSats = glo;
        gnss.numGalSats = gal;
        if (!gnss.isValid && totalVisible > 0) {
            gnss.numSats = totalVisible;
        }
        state.unlock();
    }

    if (_gps.location.isUpdated()) {
        bool valid = _gps.location.isValid();
        double lat = _gps.location.lat();
        double lon = _gps.location.lng();
        float alt  = _gps.altitude.meters();
        float spd  = _gps.speed.kmph();
        float hdg  = _gps.course.deg();
        float hdop = _gps.hdop.hdop();
        uint8_t sats = _gps.satellites.value();

        SystemState& state = SystemState::instance();
        state.lock();
        auto& gnss = state.getContextUnsafe().gnss;
        gnss.moduleOnline = true;
        if (valid) {
            gnss.isValid = true;
            gnss.latitude = lat;
            gnss.longitude = lon;
            gnss.latE7 = (int32_t)(lat * 1e7);
            gnss.lonE7 = (int32_t)(lon * 1e7);
            gnss.altitudeMeters = alt;
            gnss.speedKmh = spd;
            if (spd > MotionPowerConfig::STANDSTILL_HEADING_LOCK_KMH) {
                gnss.headingDeg = hdg;
            }
            if (hdop > 0.0f) gnss.hdop = hdop;
            if (sats > 0) gnss.numSats = sats;
            if (gnss.fixType < FIX_3D) gnss.fixType = FIX_3D;
        }
        state.unlock();
    }
    if (_uartMutex) xSemaphoreGiveRecursive(_uartMutex);

    unsigned long now = millis();

    // Query MON-RF once every 30 seconds only to avoid bus stalls
    if (now - _lastMonRfTimeMs >= 30000) {
        _lastMonRfTimeMs = now;
        if (_uartMutex) xSemaphoreTakeRecursive(_uartMutex, portMAX_DELAY);
        queryMonRf();
        if (_uartMutex) xSemaphoreGiveRecursive(_uartMutex);
    }
}
