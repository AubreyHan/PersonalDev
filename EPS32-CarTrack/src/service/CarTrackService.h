#pragma once
#include <Arduino.h>
#include "model/SystemState.h"
#include "driver/GnssDriver_LC76G.h"

struct BreadcrumbPoint {
    double lat;
    double lon;
};

class CarTrackService {
public:
    static constexpr size_t MAX_TRAIL_POINTS = 360;

    static CarTrackService& instance() {
        static CarTrackService inst;
        return inst;
    }

    void init();
    void update();

    void resetTrip();

    float getCurrentAltitude() const { return _currentAltitude; }
    float getMaxAltitude() const { return _maxAltitude; }
    float getMinAltitude() const { return _minAltitude; }

    float getCurrentSpeed() const { return _currentSpeed; }
    float getMaxSpeed() const { return _maxSpeed; }
    double getTripDistanceKm() const { return _tripDistanceKm; }

    float getFusedHeading() const { return _fusedHeading; }

    const HistoryBuffer& getAltitudeHistory() const { return _altitudeHistory; }
    const HistoryBuffer& getSpeedHistory() const { return _speedHistory; }

    // Breadcrumb Trail API
    size_t getTrailPointCount() const { return _trailCount; }
    const BreadcrumbPoint* getTrailPoints() const { return _trailPoints; }
    void getTrailBounds(double& minLat, double& maxLat, double& minLon, double& maxLon) const {
        minLat = _minLat; maxLat = _maxLat; minLon = _minLon; maxLon = _maxLon;
    }

private:
    CarTrackService() = default;

    void updateHistoryBuffers(uint32_t now);
    void updateDistance(float speedKmh, uint32_t now);
    void recordTrailPoint(double lat, double lon);

    float  _currentAltitude   = 0.0f;
    float  _maxAltitude       = -9999.0f;
    float  _minAltitude       = 9999.0f;

    float  _currentSpeed      = 0.0f;
    float  _maxSpeed          = 0.0f;
    double _tripDistanceKm    = 0.0;

    float  _fusedHeading      = 0.0f;

    HistoryBuffer _altitudeHistory;
    HistoryBuffer _speedHistory;

    // Breadcrumb Trajectory Storage
    BreadcrumbPoint _trailPoints[MAX_TRAIL_POINTS];
    size_t          _trailCount = 0;
    size_t          _trailHead  = 0;
    double          _minLat = 90.0, _maxLat = -90.0;
    double          _minLon = 180.0, _maxLon = -180.0;
    double          _lastRecordedLat = 0.0, _lastRecordedLon = 0.0;

    uint32_t _lastHistorySampleMs = 0;
    uint32_t _lastDistanceCalcMs  = 0;
    bool     _tripInitialized     = false;
};
