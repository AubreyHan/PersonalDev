#include "DeadReckoningService.h"
#include "TunnelService.h"

#include <math.h>
#include <string.h>
#include "../model/SystemState.h"

namespace {
    constexpr double EARTH_RADIUS_M = 6371000.0;
    constexpr double DEG_TO_RAD_D   = 0.017453292519943295;
    constexpr double RAD_TO_DEG_D   = 57.29577951308232;
    constexpr float  RAD_TO_DEG_F   = 57.2957795f;
    constexpr float  PI_F           = 3.14159265f;
    constexpr float  GRAVITY_MPS2   = 9.80665f;

    float clampf(float v, float lo, float hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }
}

DeadReckoningService& DeadReckoningService::instance() {
    static DeadReckoningService s_instance;
    return s_instance;
}

float DeadReckoningService::wrapDeg360(float deg) {
    deg = fmodf(deg, 360.0f);
    if (deg < 0.0f) {
        deg += 360.0f;
    }
    return (deg >= 360.0f) ? 0.0f : deg;
}

float DeadReckoningService::wrapDeg180(float deg) {
    deg = fmodf(deg, 360.0f);
    if (deg > 180.0f)  deg -= 360.0f;
    if (deg < -180.0f) deg += 360.0f;
    return deg;
}

void DeadReckoningService::begin() {
    _hasAnchorPos      = false;
    _headingAnchored   = false;
    _inDrMode          = false;
    _recovering        = false;
    _lastTickMs        = 0;
    _lastValid3dMs     = 0;
    _drStartMs         = 0;
    _drDurationSec     = 0;
    _stationarySinceMs = 0;
}

void DeadReckoningService::processValid3dFix(
    double   trueLat,
    double   trueLon,
    float    trueAltM,
    float    speedKmh,
    float    gpsCourseDeg,
    uint32_t nowMs,
    double&  outLat,
    double&  outLon,
    float&   outAltM,
    float&   outHeadingDeg) {

    _lastValid3dMs = nowMs;

    // Heading anchor / complementary pull when moving above 3.0 km/h
    if (speedKmh > 3.0f && isfinite(gpsCourseDeg)) {
        const float cleanCourse = wrapDeg360(gpsCourseDeg);
        if (!_headingAnchored) {
            _fusedHeadingDeg = cleanCourse;
            _headingAnchored = true;
        } else {
            const float gain = clampf((speedKmh - 3.0f) * 0.008f, 0.04f, 0.25f);
            const float err  = wrapDeg180(cleanCourse - _fusedHeadingDeg);
            _fusedHeadingDeg = wrapDeg360(_fusedHeadingDeg + gain * err);
        }
    }

    // Check if we are exiting blind-zone DR back into valid 3D GNSS Fix
    if (_inDrMode && _hasAnchorPos) {
        _inDrMode         = false;
        _drDurationSec    = 0;
        _recovering       = true;
        _recoveryStartMs  = nowMs;
        _recovOffsetLat   = _drLat - trueLat;
        _recovOffsetLon   = _drLon - trueLon;
        _recovOffsetAltM  = _drAltM - trueAltM;
        _recovOffsetHdgDeg = (speedKmh > 3.0f)
                                 ? wrapDeg180(_fusedHeadingDeg - wrapDeg360(gpsCourseDeg))
                                 : 0.0f;

        // Release active tunnel axis constraint now that real 3D sky lock is confirmed
        TunnelService::instance().releaseTunnelLock();
    }

    // Check if approaching a highway tunnel portal while 3D Fix is still valid
    (void)TunnelService::instance().checkPortalApproach(
        trueLat, trueLon, _fusedHeadingDeg, speedKmh, nowMs
    );

    // Apply 2-second cosine-eased trajectory blend if recovering from DR / tunnel exit
    if (_recovering) {
        const uint32_t elapsedMs = nowMs - _recoveryStartMs;
        if (elapsedMs >= TunnelConfig::EXIT_SMOOTH_RECOVERY_MS) {
            _recovering        = false;
            _recovOffsetLat    = 0.0;
            _recovOffsetLon    = 0.0;
            _recovOffsetAltM   = 0.0f;
            _recovOffsetHdgDeg = 0.0f;
            outLat             = trueLat;
            outLon             = trueLon;
            outAltM            = trueAltM;
            outHeadingDeg      = _fusedHeadingDeg;
        } else {
            const float progress = static_cast<float>(elapsedMs) /
                                   static_cast<float>(TunnelConfig::EXIT_SMOOTH_RECOVERY_MS);
            const float weight   = 0.5f * (1.0f + cosf(PI_F * progress));
            outLat               = trueLat + static_cast<double>(weight) * _recovOffsetLat;
            outLon               = trueLon + static_cast<double>(weight) * _recovOffsetLon;
            outAltM              = trueAltM + weight * _recovOffsetAltM;
            if (speedKmh > 3.0f) {
                outHeadingDeg = wrapDeg360(wrapDeg360(gpsCourseDeg) + weight * _recovOffsetHdgDeg);
                _fusedHeadingDeg = outHeadingDeg;
            } else {
                outHeadingDeg = _fusedHeadingDeg;
            }
        }
    } else {
        outLat        = trueLat;
        outLon        = trueLon;
        outAltM       = trueAltM;
        outHeadingDeg = _fusedHeadingDeg;
    }

    // Keep DR anchor continuously synchronized to blended vehicle state
    _drLat        = outLat;
    _drLon        = outLon;
    _drAltM       = outAltM;
    _drSpeedMps   = (speedKmh > GnssConfig::STATIC_DEADBAND_KMH) ? (speedKmh / 3.6f) : 0.0f;
    _hasAnchorPos = true;
}

void DeadReckoningService::update(uint32_t nowMs) {
    if (_lastTickMs != 0 && (nowMs - _lastTickMs) < DualImuConfig::SAMPLE_INTERVAL_MS) {
        return;
    }
    const float dtSec = (_lastTickMs == 0)
                            ? (static_cast<float>(DualImuConfig::SAMPLE_INTERVAL_MS) * 0.001f)
                            : clampf(static_cast<float>(nowMs - _lastTickMs) * 0.001f, 0.005f, 0.25f);
    _lastTickMs = nowMs;

    // Read IMU + GNSS + Baro snapshot fields
    float yawRateRadS    = 0.0f;
    float lonG           = 0.0f;
    bool  imuStationary  = true;
    bool  gnss3dOk       = false;
    float gnssSpeedKmh   = 0.0f;
    float baroFusedAltM  = _drAltM;
    bool  baroOnline     = false;

    {
        StateGuard guard(SystemState::instance());
        if (guard) {
            const auto& st = SystemState::instance().data();
            yawRateRadS    = st.imu.gyroZ;
            lonG           = st.imu.longitudinalG;
            imuStationary  = st.imu.isStationary;
            gnss3dOk       = (st.gnss.fixType >= FIX_3D) && st.gnss.gnssFixOk;
            gnssSpeedKmh   = st.gnss.speedKmh;
            baroOnline     = st.baro.isOnline;
            if (baroOnline || st.baro.isP0Calibrated) {
                baroFusedAltM = st.baro.fusedAltitudeM;
            }
        }
    }

    if (imuStationary) {
        if (_stationarySinceMs == 0) {
            _stationarySinceMs = nowMs;
        }
    } else {
        _stationarySinceMs = 0;
    }

    // 1. 50Hz MagSafe-Immune Heading Integration with 1.8 km/h Physical Standstill Lock
    const float activeSpeedKmh = gnss3dOk ? gnssSpeedKmh : (_drSpeedMps * 3.6f);
    const bool  zuptLocked     = (activeSpeedKmh <= GnssConfig::STATIC_DEADBAND_KMH) &&
                                 (imuStationary || gnss3dOk);

    if (!zuptLocked) {
        // Deadzone tiny residual gyro noise (< 0.35 deg/s = 0.006 rad/s) on straight highways
        const float effYawRadS = (fabsf(yawRateRadS) > 0.006f) ? yawRateRadS : 0.0f;
        _fusedHeadingDeg = wrapDeg360(_fusedHeadingDeg + effYawRadS * RAD_TO_DEG_F * dtSec);
    }

    // 2. Check if GNSS dropped out of 3D Fix -> Seamless Blind-Zone 3D Dead Reckoning
    const bool gnssTimedOut = (_lastValid3dMs != 0) && ((nowMs - _lastValid3dMs) > 650UL);
    const bool shouldRunDr  = _hasAnchorPos && (!gnss3dOk || gnssTimedOut);

    if (!shouldRunDr) {
        StateGuard guard(SystemState::instance());
        if (guard) {
            auto& st               = SystemState::instance().data();
            st.imu.zuptLocked      = zuptLocked;
            st.imu.fusedHeadingDeg = _fusedHeadingDeg;
            if (st.gnss.isValid && !st.gnss.isDeadReckoning) {
                st.gnss.headingDeg = _fusedHeadingDeg;
            }
        }
        return;
    }

    if (!_inDrMode) {
        _inDrMode   = true;
        _recovering = false;
        _drStartMs  = nowMs;
    }
    _drDurationSec = (nowMs - _drStartMs) / 1000UL;

    // Safety cap: freeze velocity after MAX_DR_DURATION_SEC (20 min) without sky fix
    if (_drDurationSec > MAX_DR_DURATION_SEC) {
        _drSpeedMps = 0.0f;
    } else if (_stationarySinceMs != 0 && (nowMs - _stationarySinceMs) >= 1200UL) {
        // Vehicle stopped inside tunnel / underpass -> ZUPT locks speed to 0
        _drSpeedMps = 0.0f;
    } else {
        // Integrate ZUPT-gated longitudinal acceleration with gentle aerodynamic/rolling damping
        const bool inKnownTunnel = TunnelService::instance().isInTunnel();
        const float accelMps2    = (fabsf(lonG) > 0.035f) ? (-lonG * GRAVITY_MPS2 * 0.35f) : 0.0f;
        const float damping      = inKnownTunnel ? 0.9997f : 0.9975f;
        _drSpeedMps = (_drSpeedMps + accelMps2 * dtSec) * damping;
        _drSpeedMps = clampf(_drSpeedMps, 0.0f, 41.67f); // Cap at 150 km/h in blind-zone DR
        if ((_drSpeedMps * 3.6f) <= GnssConfig::STATIC_DEADBAND_KMH && imuStationary) {
            _drSpeedMps = 0.0f;
        }
    }

    const float stepDistM = _drSpeedMps * dtSec;
    if (stepDistM > 0.0f) {
        const double hdgRad = static_cast<double>(_fusedHeadingDeg) * DEG_TO_RAD_D;
        const double dNorth = static_cast<double>(stepDistM) * cos(hdgRad);
        const double dEast  = static_cast<double>(stepDistM) * sin(hdgRad);
        const double cosLat = cos(_drLat * DEG_TO_RAD_D);
        _drLat += (dNorth / EARTH_RADIUS_M) * RAD_TO_DEG_D;
        if (fabs(cosLat) > 1e-4) {
            _drLon += (dEast / (EARTH_RADIUS_M * cosLat)) * RAD_TO_DEG_D;
        }
    }

    // Apply 1D Highway Tunnel Polyline Axis Constraint if near/inside a tunnel
    const bool constrainedByTunnel = TunnelService::instance().updateTunnelConstraint(
        _drLat, _drLon, _fusedHeadingDeg, stepDistM, false, nowMs
    );

    if (baroOnline) {
        _drAltM = baroFusedAltM;
    }

    const float drSpeedKmh = (_drSpeedMps * 3.6f <= GnssConfig::STATIC_DEADBAND_KMH)
                                 ? 0.0f
                                 : (_drSpeedMps * 3.6f);

    // Publish 3D Dead-Reckoning state atomically to SystemState
    {
        StateGuard guard(SystemState::instance());
        if (guard) {
            auto& st               = SystemState::instance().data();
            st.imu.zuptLocked      = (_drSpeedMps <= 0.01f);
            st.imu.fusedHeadingDeg = _fusedHeadingDeg;

            auto& gnss             = st.gnss;
            gnss.isValid           = true;
            gnss.isDeadReckoning   = true;
            gnss.drDurationSec     = _drDurationSec;
            gnss.latitude          = _drLat;
            gnss.longitude         = _drLon;
            gnss.latE7             = static_cast<int32_t>(llround(_drLat * 1e7));
            gnss.lonE7             = static_cast<int32_t>(llround(_drLon * 1e7));
            gnss.altitudeMeters    = _drAltM;
            gnss.rawSpeedKmh       = _drSpeedMps * 3.6f;
            gnss.speedKmh          = drSpeedKmh;
            gnss.staticHoldLocked  = (drSpeedKmh <= 0.0f);
            gnss.headingDeg        = _fusedHeadingDeg;

            gnss.isInTunnel        = constrainedByTunnel;
            if (constrainedByTunnel) {
                memset(gnss.tunnelName, 0, sizeof(gnss.tunnelName));
                strncpy(gnss.tunnelName, TunnelService::instance().activeTunnelName(),
                        sizeof(gnss.tunnelName) - 1);
                gnss.tunnelProgressKm = TunnelService::instance().progressKm();
                gnss.tunnelLengthKm   = TunnelService::instance().lengthKm();
            } else {
                gnss.tunnelProgressKm = 0.0f;
                gnss.tunnelLengthKm   = 0.0f;
                memset(gnss.tunnelName, 0, sizeof(gnss.tunnelName));
            }
        }
    }
}
