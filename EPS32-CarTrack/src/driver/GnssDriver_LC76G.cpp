#include "GnssDriver_LC76G.h"
#include <Wire.h>
#include <Preferences.h>
#include <cmath>

#define LC76G_ADDR_W   0x50
#define LC76G_ADDR_R   0x54
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
    Serial.println("[LC76G] Initializing Quectel LC76G GNSS (I2C + UART)...");

    // Initialize UART as primary/dual (LC76G default 9600 8N1)
    Serial1.begin(LC76G_BAUD, SERIAL_8N1, LC76G_PIN_RX, LC76G_PIN_TX);

    // Test probe LC76G I2C with query command
    uint8_t queryData[] = { 0x08, 0x00, 0x51, 0xAA, 0x04, 0x00, 0x00, 0x00 };
    Wire.beginTransmission(LC76G_ADDR_W);
    Wire.write(queryData, sizeof(queryData));
    byte err = Wire.endTransmission();
    if (err == 0) {
        _isOnline = true;
        _usingI2c = true;
        Serial.println("[LC76G] LC76G responded on I2C (0x50/0x54)!");
    } else {
        Serial.printf("[LC76G] I2C probe returned %d (LC76G may use UART or require power-on)\n", err);
        _isOnline = true;
    }

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

    // Wakeup / Ensure Active Mode
    sendPairCommand("PAIR002");
    vTaskDelay(pdMS_TO_TICKS(40));

    // Enable Quad-Constellation (GPS=1, GLONASS=1, Galileo=1, BeiDou=1)
    sendPairCommand("PAIR066,1,1,1,1,0,0");
    vTaskDelay(pdMS_TO_TICKS(40));

    // 15° Elevation Mask Angle
    sendPairCommand("PAIR070,15");
    vTaskDelay(pdMS_TO_TICKS(40));

    // Platform Model: Automotive
    sendPairCommand("PAIR080,1");
    vTaskDelay(pdMS_TO_TICKS(40));

    // Autonomous Orbit Prediction (EASY)
    sendPairCommand("PAIR432,1");
    vTaskDelay(pdMS_TO_TICKS(40));

    // Enable all standard NMEA sentences output at 1Hz
    sendPairCommand("PAIR062,-1,1");
    vTaskDelay(pdMS_TO_TICKS(40));

    Serial.println("[LC76G] Configured: Quad-Constellation, 15° Mask, Automotive Model, EASY ON");
}

void GnssDriver_LC76G::pollI2cStream() {
    // Poll every 500ms (standard NMEA refresh interval)
    static uint32_t lastPollMs = 0;
    uint32_t now = millis();
    if (now - lastPollMs < 500) {
        return;
    }
    lastPollMs = now;

    // 1. Handshake / Poll: send length query to 0x50
    uint8_t queryData[] = { 0x08, 0x00, 0x51, 0xAA, 0x04, 0x00, 0x00, 0x00 };
    Wire.beginTransmission(LC76G_ADDR_W);
    Wire.write(queryData, sizeof(queryData));
    byte wErr = Wire.endTransmission();
    if (wErr != 0) {
        Serial.printf("[LC76G I2C] Query write err=%d\n", wErr);
        return;
    }

    delay(80);

    // 2. Header Read: 4 bytes from 0x54 (Little-Endian Length)
    size_t reqLen = Wire.requestFrom((uint8_t)LC76G_ADDR_R, (size_t)4);
    if (reqLen != 4) {
        static uint32_t lastWarn = 0;
        if (millis() - lastWarn > 3000) {
            Serial.printf("[LC76G I2C] reqFrom 0x54 returned %d (expected 4)\n", reqLen);
            lastWarn = millis();
        }
        return;
    }

    uint8_t header[4];
    for (int i = 0; i < 4; i++) {
        header[i] = Wire.read();
    }

    uint32_t dataLength = (uint32_t)header[0] |
                          ((uint32_t)header[1] << 8) |
                          ((uint32_t)header[2] << 16) |
                          ((uint32_t)header[3] << 24);

    Serial.printf("[LC76G I2C] Header bytes: 0x%02X 0x%02X 0x%02X 0x%02X -> len=%u\n",
                  header[0], header[1], header[2], header[3], dataLength);

    if (dataLength == 0 || dataLength > 16384) {
        return;
    }

    // 3. Acknowledgment (Ack): Write 4-byte magic + 4-byte header back to 0x50
    uint8_t data2[] = { 0x00, 0x20, 0x51, 0xAA };
    uint8_t ackPayload[8];
    memcpy(ackPayload, data2, 4);
    memcpy(ackPayload + 4, header, 4);

    delay(50);
    Wire.beginTransmission(LC76G_ADDR_W);
    Wire.write(ackPayload, sizeof(ackPayload));
    byte aErr = Wire.endTransmission();
    if (aErr != 0) {
        Serial.printf("[LC76G I2C] Ack write err=%d\n", aErr);
        return;
    }

    delay(80);

    // 4. Body Read: Read payload matching dataLength from 0x54
    _usingI2c = true;
    uint8_t* buf = (uint8_t*)malloc(dataLength);
    if (buf) {
        size_t received = Wire.requestFrom((uint8_t)LC76G_ADDR_R, (size_t)dataLength);
        Serial.printf("[LC76G I2C] Read %d/%u bytes\n", received, dataLength);
        for (size_t i = 0; i < received; i++) {
            char c = (char)Wire.read();
            _totalBytes++;
            _gps.encode(c);

            if (c == '\n' || c == '\r') {
                if (_nmeaIndex > 0) {
                    _nmeaBuffer[_nmeaIndex] = '\0';
                    parseNmeaSentence(_nmeaBuffer);
                    _nmeaIndex = 0;
                }
            } else if (_nmeaIndex < sizeof(_nmeaBuffer) - 1) {
                _nmeaBuffer[_nmeaIndex++] = c;
            }
        }
        free(buf);
    }
}

void GnssDriver_LC76G::update() {
    // 1. Check I2C Mailbox
    pollI2cStream();

    // 2. Check UART stream
    parseNmeaStream();

    updateStabilityFilter();

    // Periodic NVS save every 15s when fix is valid
    if (_gps.location.isValid() && (millis() - _lastNvsSaveMs > GnssFilterConfig::NVS_SNAPSHOT_INTERVAL_MS)) {
        saveNvsSnapshot();
        _lastNvsSaveMs = millis();
    }

    // Periodic diagnostics log every 2 seconds
    uint32_t now = millis();
    if (now - _lastDiagMs >= 2000) {
        _lastDiagMs = now;
        logDiagnostics();
    }
}

void GnssDriver_LC76G::parseNmeaStream() {
    while (Serial1.available()) {
        char c = (char)Serial1.read();
        _totalBytes++;
        if (c == 0x00 || (uint8_t)c == 0xFF) continue;

        _gps.encode(c);

        if (c == '\n' || c == '\r') {
            if (_nmeaIndex > 0) {
                _nmeaBuffer[_nmeaIndex] = '\0';
                parseNmeaSentence(_nmeaBuffer);
                _nmeaIndex = 0;
            }
        } else if (_nmeaIndex < sizeof(_nmeaBuffer) - 1) {
            _nmeaBuffer[_nmeaIndex++] = c;
        }
    }
}

void GnssDriver_LC76G::parseNmeaSentence(const char* sentence) {
    if (!sentence || *sentence != '$') return;

    // 1. Parse GSV (Satellites in view & SNR / C/N0)
    if (strstr(sentence, "GSV") != nullptr) {
        parseGsvSentence(sentence);
    }
    // 2. Parse GSA (VDOP & Active Fix Type)
    else if (strstr(sentence, "GSA") != nullptr) {
        char copy[160];
        strncpy(copy, sentence, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';

        char* token = strtok(copy, ",");
        int fieldIdx = 0;
        while (token != nullptr) {
            fieldIdx++;
            if (fieldIdx == 17) { // 17th token is VDOP
                float parsedVdop = atof(token);
                if (parsedVdop > 0.1f && parsedVdop < 50.0f) {
                    _vdop = parsedVdop;
                }
            }
            token = strtok(nullptr, ",");
        }
    }
    // 3. Parse Quectel Antenna Status ($PQTMANT)
    else if (strstr(sentence, "PQTMANT") != nullptr) {
        const char* p = strchr(sentence, ',');
        if (p) {
            int code = atoi(p + 1);
            if (code == 0) strncpy(_antennaStatus, "NORMAL (OK)", sizeof(_antennaStatus));
            else if (code == 1) strncpy(_antennaStatus, "OPEN (DISCONNECTED)", sizeof(_antennaStatus));
            else if (code == 2) strncpy(_antennaStatus, "SHORTED", sizeof(_antennaStatus));
        }
    }
}

void GnssDriver_LC76G::parseGsvSentence(const char* sentence) {
    char copy[160];
    strncpy(copy, sentence, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char* token = strtok(copy, ",");
    int field = 0;
    int totalSats = 0;

    char* fields[24];
    while (token && field < 24) {
        fields[field++] = token;
        token = strtok(nullptr, ",");
    }

    if (field >= 4) {
        totalSats = atoi(fields[3]);
        if (totalSats > 0) {
            _satsInView = totalSats;
        }

        // Each satellite block has: PRN, Elevation, Azimuth, SNR
        int satBlock = 4;
        while (satBlock + 3 < field) {
            uint8_t prn = atoi(fields[satBlock]);
            uint8_t elev = atoi(fields[satBlock + 1]);
            uint16_t azim = atoi(fields[satBlock + 2]);
            uint8_t snr = atoi(fields[satBlock + 3]);

            if (snr > _maxSnr) {
                _maxSnr = snr;
            }

            if (prn > 0 && _topSatCount < 12) {
                bool found = false;
                for (uint8_t s = 0; s < _topSatCount; ++s) {
                    if (_topSats[s].prn == prn) {
                        _topSats[s].snr = snr;
                        _topSats[s].elevation = elev;
                        _topSats[s].azimuth = azim;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    _topSats[_topSatCount++] = {prn, elev, azim, snr};
                }
            }

            satBlock += 4;
        }
    }

    // Determine antenna RF condition from SNR
    if (_satsInView == 0) {
        strncpy(_antennaStatus, "SEARCHING (0 SATS)", sizeof(_antennaStatus));
    } else if (_maxSnr == 0) {
        strncpy(_antennaStatus, "NO RF (CHECK ANTENNA!)", sizeof(_antennaStatus));
    } else if (_maxSnr < 25) {
        strncpy(_antennaStatus, "WEAK (INDOOR)", sizeof(_antennaStatus));
    } else if (_maxSnr < 35) {
        strncpy(_antennaStatus, "MODERATE (ACQUIRING)", sizeof(_antennaStatus));
    } else {
        strncpy(_antennaStatus, "EXCELLENT RF (>35dB)", sizeof(_antennaStatus));
    }
}

void GnssDriver_LC76G::logDiagnostics() {
    Serial.println("\n----------------- [GPS & ANTENNA DIAGNOSTICS] -----------------");
    Serial.printf(" Interface      : %s | Total Stream Bytes: %u\n",
                  _usingI2c ? "I2C (0x50/0x54 Handshake)" : "UART1 (RX:GPIO18, TX:GPIO17)", _totalBytes);
    Serial.printf(" Antenna Status : %s\n", _antennaStatus);
    Serial.printf(" Satellites     : In View: %u | In Fix: %u\n", _satsInView, _activeSats);
    Serial.printf(" Peak RF SNR    : %u dB-Hz (Fix Threshold: >=28 dB-Hz)\n", _maxSnr);
    Serial.printf(" Precision      : HDOP: %.1f | VDOP: %.1f\n", _hdop, _vdop);

    if (_gps.location.isValid()) {
        Serial.printf(" Fix Status     : %s\n", _isStable ? "3D FIX [STABLE CONVERGED]" : "3D FIX [CONVERGING]");
        Serial.printf(" Coordinates    : %.6f %c, %.6f %c | Altitude: %.1f m\n",
                      fabs(_gps.location.lat()), _gps.location.lat() >= 0 ? 'N' : 'S',
                      fabs(_gps.location.lng()), _gps.location.lng() >= 0 ? 'E' : 'W',
                      _filteredAltitude);
        Serial.printf(" Speed / Heading: %.1f km/h | Course: %.1f deg\n",
                      _gps.speed.kmph(), _gps.course.deg());
    } else {
        Serial.println(" Fix Status     : NO FIX (Acquiring almanac & ephemeris...)");
    }

    if (_topSatCount > 0) {
        Serial.print(" Satellites Tracked: ");
        for (uint8_t i = 0; i < _topSatCount && i < 6; ++i) {
            Serial.printf("[#%02u: %udB] ", _topSats[i].prn, _topSats[i].snr);
        }
        Serial.println();
    }
    Serial.println("----------------------------------------------------------------");
}

void GnssDriver_LC76G::updateStabilityFilter() {
    uint32_t now = millis();

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

    float rawSpeedKmh = _gps.speed.isValid() ? _gps.speed.kmph() : 0.0f;
    if (rawSpeedKmh < GnssFilterConfig::STATIC_SPEED_THRESHOLD_KMH) {
        _isStaticLocked = true;
    } else {
        _isStaticLocked = false;
    }

    if (_gps.altitude.isValid()) {
        updateAltitudeFilter((float)_gps.altitude.meters());
    }

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

    float diff = rawMslAlt - _filteredAltitude;
    float maxDelta = GnssFilterConfig::MAX_CLIMB_RATE_MPS * dt + 1.0f;
    if (fabs(diff) > maxDelta && !_isStaticLocked) {
        rawMslAlt = _filteredAltitude + (diff > 0 ? maxDelta : -maxDelta);
    }

    if (_isStaticLocked) {
        return;
    }

    float R = (_vdop > 0.5f) ? (_vdop * _vdop * 0.8f) : 2.0f;
    float Q = 0.04f * dt;

    float pPredicted = _altVarianceEstimate + Q;
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

    char injectCmd[80];
    snprintf(injectCmd, sizeof(injectCmd),
             "PAIR006,%04d,%02d,%02d,%02d,%02d,%02d,%.6f,%.6f,%.1f",
             yr, mo, da, hr, mi, se, lat, lon, alt);

    vTaskDelay(pdMS_TO_TICKS(150));
    sendPairCommand(injectCmd);
    Serial.println("[LC76G] Injected Hot-Start Ephemeris snapshot via PAIR006");
    return true;
}
