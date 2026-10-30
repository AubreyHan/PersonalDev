#include "service/AltitudeFusion.h"
#include <math.h>

void AltitudeFusion::init(BaroDriver* baro) {
    _baro = baro;
    _seaLevelPressureHpa = 1013.25f;
    _lastVerticalSampleMs = millis();
    _lastHistoryPushMs = millis();
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

    if (baroOk) {
        float P = _baro->getPressureHpa();
        ctx.baro.pressureHpa  = P;
        ctx.baro.temperatureC = _baro->getTemperatureC();

        // SPEC 2.5 Complementary filter: calibrate P0 when 3D Fix and HDOP < 1.2
        if (ctx.gnss.isValid && ctx.gnss.fixType >= FIX_3D && ctx.gnss.hdop > 0.0f && ctx.gnss.hdop < 1.5f) {
            float hGnss = ctx.gnss.altitudeMeters;
            float ratio = 1.0f - (hGnss / 44330.0f);
            if (ratio > 0.1f) {
                float p0Target = P * powf(ratio, -5.25486f); // -1 / 0.1903
                constexpr float ALPHA = 0.005f;
                _seaLevelPressureHpa = _seaLevelPressureHpa + ALPHA * (p0Target - _seaLevelPressureHpa);
            }
        }

        ctx.baro.seaLevelPressureHpa = _seaLevelPressureHpa;
        float hBaro = 44330.0f * (1.0f - powf(P / _seaLevelPressureHpa, 0.1903f));
        ctx.baro.rawBaroAltitudeM = hBaro;

        if (!_initialized) {
            _fusedAltitudeM = hBaro;
            _lastAltitudeM  = hBaro;
            _initialized    = true;
        } else {
            _fusedAltitudeM = _fusedAltitudeM * 0.85f + hBaro * 0.15f;
        }
    } else if (ctx.gnss.isValid) {
        _fusedAltitudeM = ctx.gnss.altitudeMeters;
        if (!_initialized) {
            _lastAltitudeM = _fusedAltitudeM;
            _initialized   = true;
        }
    }

    ctx.baro.fusedAltitudeM   = _fusedAltitudeM;
    ctx.trip.currentAltitudeM = _fusedAltitudeM;

    if (_initialized) {
        if (ctx.trip.maxAltitudeM == 0.0f && ctx.trip.minAltitudeM == 0.0f) {
            ctx.trip.maxAltitudeM = _fusedAltitudeM;
            ctx.trip.minAltitudeM = _fusedAltitudeM;
        } else {
            if (_fusedAltitudeM > ctx.trip.maxAltitudeM) ctx.trip.maxAltitudeM = _fusedAltitudeM;
            if (_fusedAltitudeM < ctx.trip.minAltitudeM) ctx.trip.minAltitudeM = _fusedAltitudeM;
        }
    }

    uint32_t now = millis();
    if (now - _lastVerticalSampleMs >= 1000) {
        float dt = (now - _lastVerticalSampleMs) * 0.001f;
        _lastVerticalSampleMs = now;

        float dAlt = _fusedAltitudeM - _lastAltitudeM;
        _lastAltitudeM = _fusedAltitudeM;
        ctx.trip.verticalSpeedMps = dAlt / dt;

        if (dAlt > 0.35f && ctx.gnss.speedKmh > 2.0f) {
            ctx.trip.cumulativeClimbM += dAlt;
        }

        // Road slope % calculation when vehicle moves >= 10m
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
                    float slopePct = (float)(dH / horizDist) * 100.0f;
                    if (slopePct > 35.0f)  slopePct = 35.0f;
                    if (slopePct < -35.0f) slopePct = -35.0f;
                    ctx.trip.roadSlopePercent = ctx.trip.roadSlopePercent * 0.6f + slopePct * 0.4f;

                    _lastSlopeLat  = ctx.gnss.latitude;
                    _lastSlopeLon  = ctx.gnss.longitude;
                    _lastSlopeAltM = _fusedAltitudeM;
                }
            }
        } else if (ctx.gnss.speedKmh <= MotionPowerConfig::STANDSTILL_HEADING_LOCK_KMH) {
            // ZUPT standstill slope lock
        }
    }

    // Push 1-hour history sample every 30 seconds (120 points = 60 minutes)
    if (now - _lastHistoryPushMs >= 30000 || ctx.trip.altitudeHistory.count == 0) {
        _lastHistoryPushMs = now;
        ctx.trip.altitudeHistory.push(_fusedAltitudeM);
        ctx.trip.speedHistory.push(ctx.gnss.speedKmh);
    }

    state.unlock();
}
