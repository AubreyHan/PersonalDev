#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "../config.h"

// =============================================================================
// AltitudeFusion - 10Hz BMP280 Barometer + u-blox GNSS 3D Complementary Filter
//
// Features:
// - Automatic sea-level reference pressure P0 inversion on first 3D Fix (hdop <= 8.0)
//   and slow weather-front tracking (alpha = 0.005) when hdop <= 1.5, persisted in NVS
// - 10Hz complementary altitude fusion: high-frequency baro delta + low-frequency GNSS hMSL
// - Seamless pure barometric relative elevation tracking inside highway tunnels / DR
// - Smoothed vertical climb/descent rate (m/s) & real-time road grade (%) with >=7.0% alert
// - Updates SystemState::baro and 3-tier TripStats altitude / cumulative climb / descent
// =============================================================================
class AltitudeFusion {
public:
    static AltitudeFusion& instance();

    // Load persisted sea-level pressure P0 / last altitude from NVS and seed initial state.
    bool begin();

    // 10Hz (100ms) fusion update; call after BaroDriver::update() & GnssDriver::update().
    void update(uint32_t nowMs = millis());

    float fusedAltitudeM() const { return _fusedAltM; }
    float seaLevelPressureHpa() const { return _seaLevelHpa; }
    float verticalSpeedMps() const { return _verticalSpeedMps; }
    float roadSlopePercent() const { return _roadSlopePct; }

    static float pressureToAltitudeM(float pressureHpa, float seaLevelHpa);
    static float invertSeaLevelPressureHpa(float pressureHpa, float altitudeM);

private:
    AltitudeFusion() = default;
    ~AltitudeFusion() = default;
    AltitudeFusion(const AltitudeFusion&) = delete;
    AltitudeFusion& operator=(const AltitudeFusion&) = delete;

    void saveSeaLevelToNvs(float p0Hpa);

    struct SlopeAnchor {
        float distM = 0.0f;
        float altM  = 0.0f;
    };
    static constexpr size_t SLOPE_RING_SIZE = 16;

    float       _seaLevelHpa       = BaroFusionConfig::DEFAULT_SEA_LEVEL_HPA;
    float       _lastSavedP0Hpa    = BaroFusionConfig::DEFAULT_SEA_LEVEL_HPA;
    uint32_t    _lastP0SaveMs      = 0;
    bool        _p0Calibrated      = false;
    bool        _firstGnssAnchored = false;
    bool        _baroSeeded        = false;

    float       _prevRawBaroAltM   = 0.0f;
    float       _fusedAltM         = 0.0f;
    float       _verticalSpeedMps  = 0.0f;
    float       _roadSlopePct      = 0.0f;
    uint32_t    _lastUpdateMs      = 0;

    // Rolling horizontal distance vs altitude window for road grade (%) calculation
    float       _cumHorizDistM     = 0.0f;
    SlopeAnchor _slopeRing[SLOPE_RING_SIZE] = {};
    uint8_t     _slopeHead         = 0;
    uint8_t     _slopeCount        = 0;

    // Per-tier hysteresis anchors (1.5m threshold) for cumulative climb/descent
    float       _climbAnchorM[3]   = { 0.0f, 0.0f, 0.0f };
    bool        _climbAnchorInit[3]= { false, false, false };
    uint32_t    _seenResetVersion  = 0;
    uint32_t    _seenLocalDayYmd   = 0;
    uint16_t    _seenManualSubIdx  = 0;
};
