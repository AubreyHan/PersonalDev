#pragma once
#include <Arduino.h>
#include "model/SystemState.h"
#include "driver/GnssDriver_LC76G.h"

class CarTrackService {
public:
    static CarTrackService& instance() {
        static CarTrackService inst;
        return inst;
    }

    void init();
    void update();

    // Reset current trip statistics
    void resetTrip();

    // Statistics getters
    float getCurrentAltitude() const { return _currentAltitude; }
    float getMaxAltitude() const { return _maxAltitude; }
    float getMinAltitude() const { return _minAltitude; }

    float getCurrentSpeed() const { return _currentSpeed; }
    float getMaxSpeed() const { return _maxSpeed; }
    double getTripDistanceKm() const { return _tripDistanceKm; }

    float getFusedHeading() const { return _fusedHeading; }

    // History buffers (120 points each, 30s interval = 1 hour)
    const HistoryBuffer& getAltitudeHistory() const { return _altitudeHistory; }
    const HistoryBuffer& getSpeedHistory() const { return _speedHistory; }

private:
    CarTrackService() = default;

    void updateHistoryBuffers(uint32_t now);
    void updateDistance(float speedKmh, uint32_t now);

    float  _currentAltitude   = 0.0f;
    float  _maxAltitude       = -9999.0f;
    float  _minAltitude       = 9999.0f;

    float  _currentSpeed      = 0.0f;
    float  _maxSpeed          = 0.0f;
    double _tripDistanceKm    = 0.0;

    float  _fusedHeading      = 0.0f;

    HistoryBuffer _altitudeHistory;
    HistoryBuffer _speedHistory;

    uint32_t _lastHistorySampleMs = 0;
    uint32_t _lastDistanceCalcMs  = 0;
    bool     _tripInitialized     = false;
};
