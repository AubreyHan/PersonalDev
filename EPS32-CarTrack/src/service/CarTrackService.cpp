#include "CarTrackService.h"
#include <cmath>

void CarTrackService::init() {
    _altitudeHistory.reset();
    _speedHistory.reset();
    _lastHistorySampleMs = millis();
    _lastDistanceCalcMs  = millis();
    Serial.println("[CarTrackService] Initialized: 1-hour History Buffers & Mileage Tracker");
}

void CarTrackService::resetTrip() {
    _tripDistanceKm = 0.0;
    _maxSpeed = 0.0f;
    _maxAltitude = -9999.0f;
    _minAltitude = 9999.0f;
    _altitudeHistory.reset();
    _speedHistory.reset();
    _tripInitialized = false;
    Serial.println("[CarTrackService] Trip statistics reset.");
}

void CarTrackService::update() {
    GnssDriver_LC76G& gnssDriver = GnssDriver_LC76G::instance();
    GnssData& gnss = SystemState::instance().gnss;
    uint32_t now = millis();

    // 1. Get filtered altitude from GNSS Driver Kalman Filter
    _currentAltitude = gnssDriver.getFilteredAltitude();

    // 2. Get speed (zero-clamped if < 1.5 km/h)
    _currentSpeed = gnss.speedKmh;

    // 3. Fused Heading (smooth transition)
    if (_currentSpeed > 2.0f && gnss.isValid) {
        float rawHeading = gnss.headingDeg;
        // Smooth heading transition across 0/360 boundary
        float diff = rawHeading - _fusedHeading;
        while (diff < -180.0f) diff += 360.0f;
        while (diff > 180.0f) diff -= 360.0f;
        _fusedHeading += diff * 0.25f; // Low-pass filter
        while (_fusedHeading < 0.0f) _fusedHeading += 360.0f;
        while (_fusedHeading >= 360.0f) _fusedHeading -= 360.0f;
    }

    // 4. Update Trip Metrics ONLY when GNSS is stable (prevents initial drift jump!)
    if (gnssDriver.isStable()) {
        if (!_tripInitialized) {
            _maxAltitude = _currentAltitude;
            _minAltitude = _currentAltitude;
            _tripInitialized = true;
        }

        // Update Max & Min Altitude
        if (_currentAltitude > _maxAltitude) {
            _maxAltitude = _currentAltitude;
        }
        if (_currentAltitude < _minAltitude) {
            _minAltitude = _currentAltitude;
        }

        // Update Max Speed
        if (_currentSpeed > _maxSpeed) {
            _maxSpeed = _currentSpeed;
        }

        // Accumulate Mileage
        updateDistance(_currentSpeed, now);

        // Update 1-Hour History Buffers (every 30 seconds = 120 samples in 60 mins)
        updateHistoryBuffers(now);
    } else {
        _lastDistanceCalcMs = now;
    }
}

void CarTrackService::updateDistance(float speedKmh, uint32_t now) {
    if (_lastDistanceCalcMs == 0) {
        _lastDistanceCalcMs = now;
        return;
    }

    float dtSec = (now - _lastDistanceCalcMs) / 1000.0f;
    _lastDistanceCalcMs = now;

    // Only accumulate if moving faster than static threshold (avoids parking drift)
    if (speedKmh >= GnssFilterConfig::STATIC_SPEED_THRESHOLD_KMH && dtSec > 0.0f && dtSec < 5.0f) {
        double deltaKm = (speedKmh / 3600.0) * dtSec;
        _tripDistanceKm += deltaKm;
    }
}

void CarTrackService::updateHistoryBuffers(uint32_t now) {
    // 30,000 ms = 30 seconds
    if (now - _lastHistorySampleMs >= 30000 || _altitudeHistory.count == 0) {
        _lastHistorySampleMs = now;

        _altitudeHistory.push(_currentAltitude);
        _speedHistory.push(_currentSpeed);

        Serial.printf("[History 1H] Sample #%d - Alt: %.1fm (Min:%.1f, Max:%.1f), Spd: %.1fkm/h (Trip: %.2fkm)\n",
                      _altitudeHistory.count, _currentAltitude, _minAltitude, _maxAltitude,
                      _currentSpeed, _tripDistanceKm);
    }
}
