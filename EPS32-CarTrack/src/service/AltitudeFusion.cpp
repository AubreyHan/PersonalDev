#include "service/AltitudeFusion.h"
#include <Preferences.h>
#include <math.h>

void AltitudeFusion::init(BaroDriver* baro) {
    _baro = baro;
    _seaLevelPressureHpa = 1013.25f;
    _lastVerticalSampleMs = millis();
    _lastHistoryPushMs = millis();
    _initialized = false;
    _gpsCalibrated = false;
    _lastBaroAltitudeM = 0.0f;
    _lastSavedP0 = 0.0f;

    // Load persisted GPS-calibrated sea-level pressure or seed from last known GPS altitude
    Preferences prefs;
    if (prefs.begin("m5track", true)) {
        float savedP0 = prefs.getFloat("sea_p0", 0.0f);
        if (savedP0 >= 950.0f && savedP0 <= 1060.0f) {
            _seaLevelPressureHpa = savedP0;
            _lastSavedP0         = savedP0;
            _gpsCalibrated       = true;
            Serial.printf("[ALTITUDE] Restored GPS-calibrated sea-level pressure from NVS: %.2f hPa\n", _seaLevelPressureHpa);
        } else {
            int32_t lastAltCm = prefs.getInt("last_alt_cm", 0);
            if (lastAltCm > 0 && lastAltCm < 900000 && baro) {
                float hLast = lastAltCm * 0.01f;
                // Read preliminary raw pressure if available
                float p = baro->getPressureHpa();
                if (p > 500.0f && p < 1100.0f) {
                    float ratio = 1.0f - (hLast / 44330.0f);
                    if (ratio > 0.1f) {
                        _seaLevelPressureHpa = p * powf(ratio, -5.25486f);
                        _lastSavedP0         = _seaLevelPressureHpa;
                        _gpsCalibrated       = true;
                        Serial.printf("[ALTITUDE] Seeded sea-level pressure from last GPS fix (%.1f m): %.2f hPa\n",
                                      hLast, _seaLevelPressureHpa);
                    }
                }
            }
        }
        prefs.end();
    }
}

void AltitudeFusion::update() {
    if (_baro) {
        _baro->update();
    }

    SystemState& state = SystemState::instance();
    state.lock();
    SystemContext& ctx = state.getContextUnsafe();

    bool baroOk = (_baro && _baro->isAvailable());
    ctx.baro.isAvailable = baroOk;

    // Check if GPS 3D fix altitude is valid and reliable
    float dop = (ctx.gnss.pdop > 0.0f) ? ctx.gnss.pdop : ctx.gnss.hdop;
    bool dopValid = (dop <= 0.0f || dop < 8.0f); // Allow reasonable DOP or unpopulated DOP
    bool gpsAltValid = (ctx.gnss.isValid && 
                        ctx.gnss.fixType >= FIX_3D && 
                        ctx.gnss.numSats >= 4 &&
                        ctx.gnss.altitudeMeters > -400.0f && 
                        ctx.gnss.altitudeMeters < 9000.0f &&
                        dopValid);

    if (baroOk) {
        float P = _baro->getPressureHpa();
        ctx.baro.pressureHpa  = P;
        ctx.baro.temperatureC = _baro->getTemperatureC();

        // 1. GPS Calibration of Sea-Level Pressure
        if (gpsAltValid && P > 300.0f && P < 1100.0f) {
            float hGnss = ctx.gnss.altitudeMeters;
            float ratio = 1.0f - (hGnss / 44330.0f);
            if (ratio > 0.1f) {
                // p0 = P / (1 - h/44330)^5.25486
                float p0Target = P * powf(ratio, -5.25486f);

                if (!_gpsCalibrated) {
                    // First calibration: immediately snap P0 to target and re-anchor fused altitude
                    _seaLevelPressureHpa = p0Target;
                    _gpsCalibrated = true;
                    _fusedAltitudeM = hGnss;
                    _lastAltitudeM = hGnss;
                    _lastBaroAltitudeM = hGnss;
                    Serial.printf("[ALTITUDE] GPS 3D Calibrated Sea-Level Pressure: %.2f hPa (GPS Alt=%.1f m, Baro P=%.2f hPa)\n",
                                  _seaLevelPressureHpa, hGnss, P);
                } else {
                    // Continuous slow calibration: gentle complementary drift correction
                    constexpr float ALPHA = 0.015f;
                    _seaLevelPressureHpa += ALPHA * (p0Target - _seaLevelPressureHpa);
                }
            }
        }

        ctx.baro.seaLevelPressureHpa = _seaLevelPressureHpa;
        // Barometric altitude computed with GPS-calibrated sea-level pressure
        float hBaro = 44330.0f * (1.0f - powf(P / _seaLevelPressureHpa, 0.1903f));
        ctx.baro.rawBaroAltitudeM = hBaro;

        if (!_initialized) {
            _fusedAltitudeM    = gpsAltValid ? ctx.gnss.altitudeMeters : hBaro;
            _lastAltitudeM     = _fusedAltitudeM;
            _lastBaroAltitudeM = hBaro;
            _initialized       = true;
        } else {
            float dBaro = hBaro - _lastBaroAltitudeM;
            _lastBaroAltitudeM = hBaro;

            if (gpsAltValid) {
                // Complementary Fusion:
                // Responsive high-frequency barometric delta (75%) + Absolute zero-drift GPS altitude anchor (25%)
                float predictedAlt = _fusedAltitudeM + dBaro;
                _fusedAltitudeM = predictedAlt * 0.75f + ctx.gnss.altitudeMeters * 0.25f;
            } else {
                // GPS unavailable or tunnel: integrate barometric delta from last calibrated state
                _fusedAltitudeM += dBaro;
            }
        }
    } else if (gpsAltValid) {
        // No barometer available: pure GPS altitude
        if (!_initialized) {
            _fusedAltitudeM = ctx.gnss.altitudeMeters;
            _lastAltitudeM  = _fusedAltitudeM;
            _initialized    = true;
        } else {
            _fusedAltitudeM = _fusedAltitudeM * 0.85f + ctx.gnss.altitudeMeters * 0.15f;
        }
    }

    ctx.baro.fusedAltitudeM = _fusedAltitudeM;

    float dClimb = 0.0f;
    float dtCalc = 0.0f;
    float slopePctCalc = ctx.trip.roadSlopePercent;

    uint32_t now = millis();
    if (now - _lastVerticalSampleMs >= 1000) {
        dtCalc = (now - _lastVerticalSampleMs) * 0.001f;
        _lastVerticalSampleMs = now;

        float dAlt = _fusedAltitudeM - _lastAltitudeM;
        _lastAltitudeM = _fusedAltitudeM;

        if (dAlt > 0.35f && ctx.gnss.speedKmh > 2.0f) {
            dClimb = dAlt;
        }

        // Road slope % calculation when vehicle moves >= 15m
        if (ctx.gnss.isValid && ctx.gnss.speedKmh > 3.0f) {
            if (_lastSlopeLat == 0.0 && _lastSlopeLon == 0.0) {
                _lastSlopeLat  = ctx.gnss.latitude;
                _lastSlopeLon  = ctx.gnss.longitude;
                _lastSlopeAltM = _fusedAltitudeM;
            } else {
                double dLatM = (ctx.gnss.latitude - _lastSlopeLat) * 111139.0;
                double dLonM = (ctx.gnss.longitude - _lastSlopeLon) * 111139.0 * cos(ctx.gnss.latitude * 0.0174532925);
                double horizDist = sqrt(dLatM * dLatM + dLonM * dLonM);
                if (horizDist >= 15.0) {
                    float dH = _fusedAltitudeM - _lastSlopeAltM;
                    float rawSlope = (float)(dH / horizDist) * 100.0f;
                    if (rawSlope > 35.0f)  rawSlope = 35.0f;
                    if (rawSlope < -35.0f) rawSlope = -35.0f;
                    slopePctCalc = rawSlope;

                    _lastSlopeLat  = ctx.gnss.latitude;
                    _lastSlopeLon  = ctx.gnss.longitude;
                    _lastSlopeAltM = _fusedAltitudeM;
                }
            }
        }
    }

    state.updateTripsAltitude(_fusedAltitudeM, dClimb, dtCalc, slopePctCalc);

    // Push 1-hour history sample every 30 seconds (120 points = 60 minutes)
    if (now - _lastHistoryPushMs >= 30000 || ctx.trip.altitudeHistory.count == 0) {
        _lastHistoryPushMs = now;
        for (int i = 0; i < TRIP_CATEGORY_COUNT; i++) {
            ctx.trips[i].altitudeHistory.push(_fusedAltitudeM);
            ctx.trips[i].speedHistory.push(ctx.gnss.speedKmh);
        }
        ctx.trip = ctx.trips[ctx.activeTripCategory];
    }

    state.unlock();
}
