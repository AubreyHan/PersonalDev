#include "CarTrackService.h"
#include <cmath>

void CarTrackService::init() {
    _altitudeHistory.reset();
    _speedHistory.reset();
    _trailCount = 0;
    _trailHead = 0;
    _minLat = 90.0; _maxLat = -90.0;
    _minLon = 180.0; _maxLon = -180.0;
    _lastHistorySampleMs = millis();
    _lastDistanceCalcMs  = millis();
    Serial.println("[CarTrackService] Initialized: 1-hour History Buffers & Breadcrumb Trail Engine");
}

void CarTrackService::resetTrip() {
    _tripDistanceKm = 0.0;
    _maxSpeed = 0.0f;
    _maxAltitude = -9999.0f;
    _minAltitude = 9999.0f;
    _altitudeHistory.reset();
    _speedHistory.reset();
    _trailCount = 0;
    _trailHead = 0;
    _minLat = 90.0; _maxLat = -90.0;
    _minLon = 180.0; _maxLon = -180.0;
    _lastRecordedLat = 0.0; _lastRecordedLon = 0.0;
    _tripInitialized = false;
    Serial.println("[CarTrackService] Trip & Trail statistics reset.");
}

void CarTrackService::update() {
    GnssDriver_LC76G& gnssDriver = GnssDriver_LC76G::instance();
    GnssData& gnss = SystemState::instance().getContextUnsafe().gnss;
    uint32_t now = millis();

    _currentAltitude = gnssDriver.getFilteredAltitude();
    _currentSpeed = gnss.speedKmh;

    // Fused Heading (smooth transition)
    if (_currentSpeed > 2.0f && gnss.isValid) {
        float rawHeading = gnss.headingDeg;
        float diff = rawHeading - _fusedHeading;
        while (diff < -180.0f) diff += 360.0f;
        while (diff > 180.0f) diff -= 360.0f;
        _fusedHeading += diff * 0.25f;
        while (_fusedHeading < 0.0f) _fusedHeading += 360.0f;
        while (_fusedHeading >= 360.0f) _fusedHeading -= 360.0f;
    }

    // Only record when GNSS is stable
    if (gnssDriver.isStable()) {
        if (!_tripInitialized) {
            _maxAltitude = _currentAltitude;
            _minAltitude = _currentAltitude;
            _tripInitialized = true;
        }

        if (_currentAltitude > _maxAltitude) _maxAltitude = _currentAltitude;
        if (_currentAltitude < _minAltitude) _minAltitude = _currentAltitude;
        if (_currentSpeed > _maxSpeed) _maxSpeed = _currentSpeed;

        // Mileage calculation
        updateDistance(_currentSpeed, now);

        // 1-Hour History Buffers (every 30s)
        updateHistoryBuffers(now);

        // Breadcrumb Trail Recording (sample every ~15m movement or 10 seconds)
        if (gnss.isValid && std::fabs(gnss.latitude) > 0.001) {
            recordTrailPoint(gnss.latitude, gnss.longitude);
        }
    } else {
        _lastDistanceCalcMs = now;
    }
}

void CarTrackService::recordTrailPoint(double lat, double lon) {
    // Check distance threshold from last recorded point (~10 meters)
    if (_trailCount > 0) {
        double dLat = (lat - _lastRecordedLat) * 111319.5;
        double dLon = (lon - _lastRecordedLon) * 111319.5 * std::cos(lat * 0.0174532925);
        double distMeters = std::sqrt(dLat * dLat + dLon * dLon);
        if (distMeters < 8.0) {
            return; // Too close, skip
        }
    }

    _lastRecordedLat = lat;
    _lastRecordedLon = lon;

    // Push into ring buffer
    _trailPoints[_trailHead] = {lat, lon};
    _trailHead = (_trailHead + 1) % MAX_TRAIL_POINTS;
    if (_trailCount < MAX_TRAIL_POINTS) {
        _trailCount++;
    }

    // Update Bounding Box
    if (lat < _minLat) _minLat = lat;
    if (lat > _maxLat) _maxLat = lat;
    if (lon < _minLon) _minLon = lon;
    if (lon > _maxLon) _maxLon = lon;
}

void CarTrackService::updateDistance(float speedKmh, uint32_t now) {
    if (_lastDistanceCalcMs == 0) {
        _lastDistanceCalcMs = now;
        return;
    }

    float dtSec = (now - _lastDistanceCalcMs) / 1000.0f;
    _lastDistanceCalcMs = now;

    if (speedKmh >= GnssFilterConfig::STATIC_SPEED_THRESHOLD_KMH && dtSec > 0.0f && dtSec < 5.0f) {
        double deltaKm = (speedKmh / 3600.0) * dtSec;
        _tripDistanceKm += deltaKm;
    }
}

void CarTrackService::updateHistoryBuffers(uint32_t now) {
    if (now - _lastHistorySampleMs >= 30000 || _altitudeHistory.count == 0) {
        _lastHistorySampleMs = now;
        _altitudeHistory.push(_currentAltitude);
        _speedHistory.push(_currentSpeed);
    }
}
