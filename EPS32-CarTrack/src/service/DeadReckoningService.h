#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "../config.h"

// =============================================================================
// DeadReckoningService - MagSafe-Immune Heading Fusion & 3D Blind-Zone DR Engine
//
// Features:
// 1. 100% MagSafe-Immune 50Hz Heading Fusion:
//    - Integrates gravity-projected Dual BMI270 yaw rate (0x68 + 0x69) at 50Hz
//    - Complementary drift correction from GNSS true ground course when speed > 3 km/h
//    - Strict physical heading lock when speed <= 1.8 km/h (zero standstill drift)
// 2. 3D Blind-Zone Dead Reckoning (Overpasses, Urban Canyons & Extra-Long Tunnels):
//    - Seamless transition when GNSS drops out of valid 3D Fix
//    - 6-axis IMU + ZUPT velocity estimation + BMP280 barometric elevation
//    - Automatic 1D polyline axis locking via TunnelService inside highway tunnels
// 3. 2-Second Cosine-Eased Exit Recovery:
//    - Blends DR coordinates smoothly onto restored 3D GNSS Fix over 2000ms
//    - Strictly prevents sudden polyline teleportation or distance spikes
// =============================================================================
class DeadReckoningService {
public:
    static DeadReckoningService& instance();

    void begin();

    // 50Hz (20ms) heading integration & blind-zone DR propagation tick.
    // Call after ImuDriver::update() and AltitudeFusion::update().
    void update(uint32_t nowMs = millis());

    // Called by GnssDriver whenever a valid 3D NAV-PVT fix arrives:
    // updates portal approach, triggers/applies 2-second smooth exit recovery,
    // and returns the blended (lat, lon, altM, fusedHeadingDeg) to publish.
    void processValid3dFix(
        double   trueLat,
        double   trueLon,
        float    trueAltM,
        float    speedKmh,
        float    gpsCourseDeg,
        uint32_t nowMs,
        double&  outLat,
        double&  outLon,
        float&   outAltM,
        float&   outHeadingDeg
    );

    bool     isDeadReckoning() const { return _inDrMode; }
    bool     isRecovering() const { return _recovering; }
    float    fusedHeadingDeg() const { return _fusedHeadingDeg; }
    uint32_t drDurationSec() const { return _drDurationSec; }

private:
    DeadReckoningService() = default;
    ~DeadReckoningService() = default;
    DeadReckoningService(const DeadReckoningService&) = delete;
    DeadReckoningService& operator=(const DeadReckoningService&) = delete;

    static float wrapDeg360(float deg);
    static float wrapDeg180(float deg);

    static constexpr uint32_t MAX_DR_DURATION_SEC = 1200UL; // Up to 20 min for 22km tunnels

    bool     _hasAnchorPos      = false;
    bool     _headingAnchored   = false;
    bool     _inDrMode          = false;
    bool     _recovering        = false;

    uint32_t _lastTickMs        = 0;
    uint32_t _lastValid3dMs     = 0;
    uint32_t _drStartMs         = 0;
    uint32_t _drDurationSec     = 0;
    uint32_t _stationarySinceMs = 0;

    // DR State Vector
    double   _drLat             = 0.0;
    double   _drLon             = 0.0;
    float    _drAltM            = 0.0f;
    float    _drSpeedMps        = 0.0f;
    float    _fusedHeadingDeg   = 0.0f;

    // 2-Second Smooth Recovery Offset State
    uint32_t _recoveryStartMs   = 0;
    double   _recovOffsetLat    = 0.0;
    double   _recovOffsetLon    = 0.0;
    float    _recovOffsetAltM   = 0.0f;
    float    _recovOffsetHdgDeg = 0.0f;
};
