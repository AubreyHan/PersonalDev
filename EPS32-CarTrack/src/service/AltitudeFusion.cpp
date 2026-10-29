#include "service/AltitudeFusion.h"
#include <math.h>

void AltitudeFusion::init() {
    _p0Hpa = 1013.25f;
    _fusedAltM = 0.0f;
    _roadSlopePercent = 0.0f;
    _lastUpdateMs = millis();
}

void AltitudeFusion::update(float gnssAltM, bool gnssValid, float hdop, float speedKmh) {
    BaroDriver& baro = BaroDriver::instance();
    if (!baro.isAvailable()) {
        if (gnssValid) _fusedAltM = gnssAltM;
        return;
    }

    float p = baro.getPressureHpa();
    if (p <= 300.0f || p >= 1100.0f) return;

    // 1. Dynamic P0 sea level pressure calibration when GNSS is strong (SPEC 2.5)
    if (gnssValid && hdop < 1.5f && gnssAltM > -100.0f && gnssAltM < 9000.0f) {
        float term = 1.0f - (gnssAltM / 44330.0f);
        if (term > 0.1f) {
            float p0Estimated = p * powf(term, -1.0f / 0.1903f);
            constexpr float alpha = 0.005f; // Ultra-slow low-pass adaptation
            _p0Hpa += alpha * (p0Estimated - _p0Hpa);
        }
    }

    // 2. Barometric altitude calculation with calibrated P0
    float hBaro = 44330.0f * (1.0f - powf(p / _p0Hpa, 0.1903f));

    // 3. Complementary fusion
    if (gnssValid && hdop < 2.0f) {
        // High frequency baro dynamics + low frequency GNSS absolute level
        _fusedAltM = 0.92f * (_fusedAltM + (hBaro - _prevAltM)) + 0.08f * gnssAltM;
    } else {
        _fusedAltM = hBaro;
    }
    _prevAltM = hBaro;

    // 4. Road slope percentage calculation
    unsigned long now = millis();
    float dt = (now - _lastUpdateMs) / 1000.0f;
    if (dt >= 1.0f) {
        _lastUpdateMs = now;
        float speedMps = speedKmh / 3.6f;
        float distDeltaM = speedMps * dt;
        if (distDeltaM >= 3.0f) {
            float altDeltaM = _fusedAltM - _prevDistanceM;
            float slope = (altDeltaM / distDeltaM) * 100.0f;
            if (slope > -30.0f && slope < 30.0f) {
                _roadSlopePercent = 0.7f * _roadSlopePercent + 0.3f * slope;
            }
        } else {
            // Speed too low to determine slope reliably
            _roadSlopePercent = 0.0f;
        }
        _prevDistanceM = _fusedAltM;
    }
}
