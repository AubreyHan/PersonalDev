#include "GnssDriver_LC76G.h"
#include <driver/i2c.h>
#include <Preferences.h>
#include <cmath>

#define I2C_PORT           I2C_NUM_0
#define LC76G_ADDR_W       0x50
#define LC76G_ADDR_R       0x54

static uint8_t calcChecksum(const char* sentence) {
    uint8_t checksum = 0;
    if (*sentence == '$') sentence++;
    while (*sentence && *sentence != '*') {
        checksum ^= (uint8_t)*sentence++;
    }
    return checksum;
}

bool GnssDriver_LC76G::init() {
    Serial.println("[LC76G] Initializing Quectel LC76G GNSS over I2C...");

    // Check if I2C bus is ready, probe LC76G
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (LC76G_ADDR_W << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (ret == ESP_OK) {
        _isOnline = true;
        Serial.println("[LC76G] Found LC76G at 0x50/0x54");
    } else {
        Serial.printf("[LC76G] Device probe warning (err=0x%x), proceeding...\n", ret);
        _isOnline = true; // Still allow attempts
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

    i2c_cmd_handle_t h = i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h, (LC76G_ADDR_W << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(h, (const uint8_t*)fullCmd, strlen(fullCmd), true);
    i2c_master_stop(h);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT, h, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(h);

    if (err == ESP_OK) {
        Serial.printf("[LC76G TX] %s", fullCmd);
    } else {
        Serial.printf("[LC76G TX Fail] %s (err=0x%x)\n", cmd, err);
    }
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
    uint8_t buffer[64];
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (LC76G_ADDR_R << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, buffer, sizeof(buffer), I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) return;

    for (size_t i = 0; i < sizeof(buffer); ++i) {
        char c = (char)buffer[i];
        if (c == 0x00 || (uint8_t)c == 0xFF) continue; // Skip idle bytes

        _gps.encode(c);

        // Parse VDOP / GSA sentences manually if available
        if (c == '\n' || c == '\r') {
            if (_nmeaIndex > 0) {
                _nmeaBuffer[_nmeaIndex] = '\0';
                // Check if sentence is GNGSA / GPGSA to extract VDOP
                if (strstr(_nmeaBuffer, "GSA") != nullptr) {
                    // Extract fields: $GNGSA,A,3,sv...,PDOP,HDOP,VDOP*cs
                    char* token = strtok(_nmeaBuffer, ",");
                    int fieldIdx = 0;
                    float parsedVdop = 0.0f;
                    while (token != nullptr) {
                        fieldIdx++;
                        if (fieldIdx == 17) { // 17th field is VDOP before asterisk
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
    GnssData& gnss = SystemState::instance().gnss;

    // 1. Synchronize basic GPS properties
    _activeSats = _gps.satellites.isValid() ? _gps.satellites.value() : 0;
    _hdop = _gps.hdop.isValid() ? _gps.hdop.hdop() : 99.9f;
    if (_vdop > 50.0f || _vdop <= 0.1f) {
        _vdop = _hdop * 1.35f; // Reasonable fallback approximation
    }

    float currentSpeedKmh = _gps.speed.isValid() ? _gps.speed.kmph() : 0.0f;

    // Zero-Velocity Clamping (< 1.5 km/h static detection)
    _isStaticLocked = (currentSpeedKmh < GnssFilterConfig::STATIC_SPEED_THRESHOLD_KMH);
    if (_isStaticLocked) {
        currentSpeedKmh = 0.0f;
    }

    // 2. Convergence Check (Fix quality & DOP & Satellite criteria)
    bool meetsCriteria = (_gps.location.isValid() &&
                          _gps.altitude.isValid() &&
                          _hdop <= GnssFilterConfig::MAX_HDOP_FOR_RECORDING &&
                          _vdop <= GnssFilterConfig::MAX_VDOP_FOR_RECORDING &&
                          _activeSats >= GnssFilterConfig::MIN_SVS_FOR_RECORDING);

    if (meetsCriteria) {
        if (_stableStartTimeMs == 0) {
            _stableStartTimeMs = millis();
        } else if (millis() - _stableStartTimeMs >= GnssFilterConfig::STABILITY_REQUIRED_TIME_MS) {
            _isStable = true;
        }
    } else {
        _stableStartTimeMs = 0;
        // Do not immediately drop stable flag if momentary jitter < 2s
    }

    // 3. Altitude Kalman Filter Update
    if (_gps.altitude.isValid()) {
        updateAltitudeFilter((float)_gps.altitude.meters());
    }

    // 4. Update SystemState Global Model
    gnss.isValid        = _gps.location.isValid();
    gnss.moduleOnline   = _isOnline;
    gnss.fixType        = _isStable ? FIX_3D : (_gps.location.isValid() ? FIX_2D : FIX_NONE);
    gnss.latitude       = _gps.location.lat();
    gnss.longitude      = _gps.location.lng();
    gnss.speedKmh       = currentSpeedKmh;
    gnss.headingDeg     = _gps.course.isValid() ? _gps.course.deg() : gnss.headingDeg;
    gnss.altitudeMeters = _filteredAltitude;
    gnss.hdop           = _hdop;
    gnss.vdop           = _vdop;
    gnss.numSats        = _activeSats;

    if (_gps.time.isValid()) {
        gnss.year   = _gps.date.year();
        gnss.month  = _gps.date.month();
        gnss.day    = _gps.date.day();
        gnss.hour   = _gps.time.hour();
        gnss.minute = _gps.time.minute();
        gnss.second = _gps.time.second();
    }
}

void GnssDriver_LC76G::updateAltitudeFilter(float rawMslAlt) {
    uint32_t now = millis();
    if (!_altFilterInitialized) {
        _filteredAltitude = rawMslAlt;
        _lastRawAltitude = rawMslAlt;
        _altVarianceEstimate = 4.0f;
        _lastAltUpdateMs = now;
        _altFilterInitialized = true;
        return;
    }

    float dt = (now - _lastAltUpdateMs) / 1000.0f;
    if (dt <= 0.05f) return;
    _lastAltUpdateMs = now;

    // Outlier rejection (Hampel / Physical climb rate constraint)
    float deltaAlt = std::fabs(rawMslAlt - _filteredAltitude);
    float maxAllowedDelta = GnssFilterConfig::MAX_CLIMB_RATE_MPS * dt + 1.5f;
    if (deltaAlt > maxAllowedDelta && deltaAlt > 6.0f) {
        // Reject extreme multipath spike
        return;
    }

    // If static clamped, freeze altitude to prevent parking drift
    if (_isStaticLocked) {
        return;
    }

    // 1D Kalman Filter with VDOP Dynamic Measurement Covariance R
    // Process noise variance Q (vehicle dynamics)
    float Q = 0.08f * dt;
    // Measurement noise variance R scaled quadratically with VDOP
    float R = std::pow(_vdop * 0.9f, 2.0f);
    if (R < 1.0f) R = 1.0f;

    // 1. Time Update (Predict)
    float p_pred = _altVarianceEstimate + Q;

    // 2. Measurement Update (Correct)
    float K = p_pred / (p_pred + R); // Kalman gain
    _filteredAltitude = _filteredAltitude + K * (rawMslAlt - _filteredAltitude);
    _altVarianceEstimate = (1.0f - K) * p_pred;

    _lastRawAltitude = rawMslAlt;
}

void GnssDriver_LC76G::saveNvsSnapshot() {
    Preferences prefs;
    if (prefs.begin("cartrack_gnss", false)) {
        prefs.putDouble("last_lat", _gps.location.lat());
        prefs.putDouble("last_lon", _gps.location.lng());
        prefs.putFloat("last_alt", _filteredAltitude);
        prefs.putUInt("last_epoch", _gps.date.year() > 2020 ? 1 : 0);
        prefs.end();
        Serial.printf("[LC76G NVS] Snapshot saved: Lat=%.6f Lon=%.6f Alt=%.1fm\n",
                      _gps.location.lat(), _gps.location.lng(), _filteredAltitude);
    }
}

bool GnssDriver_LC76G::loadAndInjectHotStart() {
    Preferences prefs;
    if (!prefs.begin("cartrack_gnss", true)) return false;

    double lat = prefs.getDouble("last_lat", 0.0);
    double lon = prefs.getDouble("last_lon", 0.0);
    float  alt = prefs.getFloat("last_alt", 0.0f);
    prefs.end();

    if (std::fabs(lat) > 0.001 && std::fabs(lon) > 0.001) {
        Serial.printf("[LC76G HotStart] Injecting saved reference: Lat=%.5f Lon=%.5f Alt=%.1f\n", lat, lon, alt);
        char injectCmd[80];
        snprintf(injectCmd, sizeof(injectCmd), "PAIR006,%.5f,%.5f,%.1f", lat, lon, alt);
        sendPairCommand(injectCmd);
        return true;
    }
    return false;
}
