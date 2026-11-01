#include "GnssDriver_LC76G.h"
#include <Preferences.h>
#include <cmath>

#define LC76G_PIN_RX   18
#define LC76G_PIN_TX   17
#define LC76G_BAUD     9600

static uint8_t calcChecksum(const char* sentence) {
    uint8_t checksum = 0;
    if (*sentence == '$') sentence++;
    while (*sentence && *sentence != '*') {
        checksum ^= (uint8_t)*sentence++;
    }
    return checksum;
}

bool GnssDriver_LC76G::init() {
    Serial.println("[LC76G] Initializing Quectel LC76G GNSS via UART (RX:18, TX:17, 9600 baud)...");

    Serial1.begin(LC76G_BAUD, SERIAL_8N1, LC76G_PIN_RX, LC76G_PIN_TX);
    _isOnline = true;

    // Configure GNSS Performance Parameters
    configureAutomotiveModel();

    // Attempt Hot Start injection from NVS
    loadAndInjectHotStart();

    return _isOnline;
}

void GnssDriver_LC76G::sendPairCommand(const char* cmd) {
    char fullCmd[96];
    uint8_t cs = calcChecksum(cmd);
    snprintf(fullCmd, sizeof(fullCmd), "$%s*%02X\r\n", cmd, cs);

    Serial1.print(fullCmd);
    Serial.printf("[LC76G TX] %s", fullCmd);
}

void GnssDriver_LC76G::configureAutomotiveModel() {
    vTaskDelay(pdMS_TO_TICKS(100));

    // 1. Enable Quad-Constellation (GPS=1, GLONASS=1, Galileo=1, BeiDou=1, QZSS=0, NavIC=0)
    sendPairCommand("PAIR066,1,1,1,1,0,0");
    vTaskDelay(pdMS_TO_TICKS(50));

    // 2. Set Elevation Mask Angle to 15 degrees to filter noisy ground multipath
    sendPairCommand("PAIR070,15");
    vTaskDelay(pdMS_TO_TICKS(50));

    // 3. Set Platform Model to Automotive (1: Automotive, 0: Portable, 2: Pedestrian)
    sendPairCommand("PAIR080,1");
    vTaskDelay(pdMS_TO_TICKS(50));

    // 4. Enable EASY (Embedded Assist System - autonomous orbit prediction for up to 3 days)
    sendPairCommand("PAIR432,1");
    vTaskDelay(pdMS_TO_TICKS(50));

    Serial.println("[LC76G] Configured: Quad-Constellation, 15° Mask, Automotive Model, EASY ON");
}

void GnssDriver_LC76G::update() {
    parseNmeaStream();
    updateStabilityFilter();

    // Periodic NVS save every 15s when fix is valid
    if (_gps.location.isValid() && (millis() - _lastNvsSaveMs > GnssFilterConfig::NVS_SNAPSHOT_INTERVAL_MS)) {
        saveNvsSnapshot();
        _lastNvsSaveMs = millis();
    }
}

void GnssDriver_LC76G::parseNmeaStream() {
    while (Serial1.available()) {
        char c = (char)Serial1.read();
        if (c == 0x00 || (uint8_t)c == 0xFF) continue;

        _gps.encode(c);

        // Parse VDOP / GSA sentences manually if available
        if (c == '\n' || c == '\r') {
            if (_nmeaIndex > 0) {
                _nmeaBuffer[_nmeaIndex] = '\0';
                if (strstr(_nmeaBuffer, "GSA") != nullptr) {
                    char* token = strtok(_nmeaBuffer, ",");
                    int fieldIdx = 0;
                    float parsedVdop = 0.0f;
                    while (token != nullptr) {
                        fieldIdx++;
                        if (fieldIdx == 17) {
                            parsedVdop = atof(token);
                            if (parsedVdop > 0.1f && parsedVdop < 50.0f) {
                                _vdop = parsedVdop;
                            }
                        }
                        token = strtok(nullptr, ",");
                    }
                }
                _nmeaIndex = 0;
            }
        } else if (_nmeaIndex < sizeof(_nmeaBuffer) - 1) {
            _nmeaBuffer[_nmeaIndex++] = c;
        }
    }
}

void GnssDriver_LC76G::updateStabilityFilter() {
    uint32_t now = millis();

    // Read satellites and DOP
    if (_gps.satellites.isValid()) {
        _activeSats = _gps.satellites.value();
    }
    if (_gps.hdop.isValid()) {
        _hdop = _gps.hdop.hdop();
    }

    bool meetsCriteria = (_gps.location.isValid() &&
                          _activeSats >= GnssFilterConfig::MIN_SVS_FOR_RECORDING &&
                          _hdop <= GnssFilterConfig::MAX_HDOP_FOR_RECORDING &&
                          _vdop <= GnssFilterConfig::MAX_VDOP_FOR_RECORDING);

    if (meetsCriteria) {
        if (_stableStartTimeMs == 0) {
            _stableStartTimeMs = now;
        } else if (now - _stableStartTimeMs >= GnssFilterConfig::STABILITY_REQUIRED_TIME_MS) {
            _isStable = true;
        }
    } else {
        _stableStartTimeMs = 0;
        _isStable = false;
    }

    // Zero-velocity clamping
    float rawSpeedKmh = _gps.speed.isValid() ? _gps.speed.kmph() : 0.0f;
    if (rawSpeedKmh < GnssFilterConfig::STATIC_SPEED_THRESHOLD_KMH) {
        _isStaticLocked = true;
    } else {
        _isStaticLocked = false;
    }

    // Process Altitude filtering
    if (_gps.altitude.isValid()) {
        updateAltitudeFilter((float)_gps.altitude.meters());
    }

    // Update global state
    auto& gnssData = SystemState::instance().getContextUnsafe().gnss;
    gnssData.isValid = _gps.location.isValid();
    gnssData.latitude = _gps.location.lat();
    gnssData.longitude = _gps.location.lng();
    gnssData.speedKmh = _isStaticLocked ? 0.0f : rawSpeedKmh;
    gnssData.altitudeMeters = _filteredAltitude;
    gnssData.headingDeg = _gps.course.isValid() ? (float)_gps.course.deg() : 0.0f;
    gnssData.hdop = _hdop;
    gnssData.vdop = _vdop;
    gnssData.numSats = _activeSats;

    if (_gps.time.isValid() && _gps.date.isValid()) {
        gnssData.year   = _gps.date.year();
        gnssData.month  = _gps.date.month();
        gnssData.day    = _gps.date.day();
        gnssData.hour   = _gps.time.hour();
        gnssData.minute = _gps.time.minute();
        gnssData.second = _gps.time.second();
    }
}

void GnssDriver_LC76G::updateAltitudeFilter(float rawMslAlt) {
    uint32_t now = millis();
    if (!_altFilterInitialized) {
        _filteredAltitude = rawMslAlt;
        _lastRawAltitude = rawMslAlt;
        _lastAltUpdateMs = now;
        _altFilterInitialized = true;
        return;
    }

    float dt = (now - _lastAltUpdateMs) / 1000.0f;
    if (dt <= 0.0f) dt = 0.05f;
    _lastAltUpdateMs = now;

    // Outlier rejection: reject physical jumps > MAX_CLIMB_RATE_M_S * dt
    float diff = rawMslAlt - _filteredAltitude;
    float maxDelta = GnssFilterConfig::MAX_CLIMB_RATE_MPS * dt + 1.0f;
    if (fabs(diff) > maxDelta && !_isStaticLocked) {
        // Dampen extreme jumps
        rawMslAlt = _filteredAltitude + (diff > 0 ? maxDelta : -maxDelta);
    }

    // If static locked (vehicle stationary), clamp altitude to prevent drift
    if (_isStaticLocked) {
        // High process stability: altitude shouldn't move when stationary
        return;
    }

    // Dynamic measurement variance based on VDOP
    float R = (_vdop > 0.5f) ? (_vdop * _vdop * 0.8f) : 2.0f;
    float Q = 0.04f * dt; // Process noise

    // Prediction
    float pPredicted = _altVarianceEstimate + Q;

    // Measurement update
    float K = pPredicted / (pPredicted + R);
    _filteredAltitude = _filteredAltitude + K * (rawMslAlt - _filteredAltitude);
    _altVarianceEstimate = (1.0f - K) * pPredicted;
    _lastRawAltitude = rawMslAlt;
}

void GnssDriver_LC76G::saveNvsSnapshot() {
    if (!_gps.location.isValid()) return;

    Preferences pref;
    if (pref.begin("gnss_hot", false)) {
        pref.putDouble("lat", _gps.location.lat());
        pref.putDouble("lon", _gps.location.lng());
        pref.putFloat("alt", _filteredAltitude);
        if (_gps.date.isValid() && _gps.time.isValid()) {
            pref.putUInt("yr", _gps.date.year());
            pref.putUChar("mo", _gps.date.month());
            pref.putUChar("da", _gps.date.day());
            pref.putUChar("hr", _gps.time.hour());
            pref.putUChar("mi", _gps.time.minute());
            pref.putUChar("se", _gps.time.second());
        }
        pref.end();
        Serial.println("[LC76G] Saved Ephemeris & Position snapshot to NVS");
    }
}

bool GnssDriver_LC76G::loadAndInjectHotStart() {
    Preferences pref;
    if (!pref.begin("gnss_hot", true)) return false;

    double lat = pref.getDouble("lat", 0.0);
    double lon = pref.getDouble("lon", 0.0);
    float  alt = pref.getFloat("alt", 0.0f);
    uint32_t yr = pref.getUInt("yr", 0);
    uint8_t mo  = pref.getUChar("mo", 0);
    uint8_t da  = pref.getUChar("da", 0);
    uint8_t hr  = pref.getUChar("hr", 0);
    uint8_t mi  = pref.getUChar("mi", 0);
    uint8_t se  = pref.getUChar("se", 0);
    pref.end();

    if (yr < 2024 || (lat == 0.0 && lon == 0.0)) {
        return false;
    }

    // Inject PAIR006: Position & Time initialization for 1-3s hot start TTFF
    char injectCmd[80];
    snprintf(injectCmd, sizeof(injectCmd),
             "PAIR006,%04d,%02d,%02d,%02d,%02d,%02d,%.6f,%.6f,%.1f",
             yr, mo, da, hr, mi, se, lat, lon, alt);

    vTaskDelay(pdMS_TO_TICKS(150));
    sendPairCommand(injectCmd);
    Serial.println("[LC76G] Injected Hot-Start Ephemeris snapshot via PAIR006");
    return true;
}
