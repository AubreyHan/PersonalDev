#include "service/DeadReckoningService.h"
#include <math.h>

void DeadReckoningService::init(ImuDriver* imu, TunnelService* tunnel) {
    _imu          = imu;
    _tunnel       = tunnel;
    _lastUpdateMs = millis();
    _lastStatSec  = millis() / 1000;
}

void DeadReckoningService::update() {
    if (_imu) {
        _imu->update();
    }

    uint32_t now = millis();
    float dt = (now - _lastUpdateMs) * 0.001f;
    if (dt < 0.05f) return;
    _lastUpdateMs = now;

    SystemState& state = SystemState::instance();
    state.lock();
    SystemContext& ctx = state.getContextUnsafe();

    bool gnssValid = ctx.gnss.isValid && (ctx.gnss.fixType >= FIX_3D);
    double lat = ctx.gnss.latitude;
    double lon = ctx.gnss.longitude;
    float  spd = ctx.gnss.speedKmh;
    float  hdg = ctx.imu.fusedHeadingDeg;

    // Prevent ghost speed when entering underground garage / losing GPS and parking
    if (!gnssValid) {
        bool imuQuiet = (fabsf(ctx.imu.fusedGyroZ) < 0.025f) && (ctx.imu.totalG < 0.04f);
        if (imuQuiet) {
            if (_noFixImuQuietMs == 0) _noFixImuQuietMs = now;
            if (now - _noFixImuQuietMs >= 3000) {
                spd *= 0.80f;
                if (spd < MotionPowerConfig::STANDSTILL_HEADING_LOCK_KMH) {
                    spd = 0.0f;
                }
                ctx.gnss.speedKmh = spd;
            }
        } else {
            _noFixImuQuietMs = 0;
        }
    } else {
        _noFixImuQuietMs = 0;
    }

    if (!gnssValid && ctx.trip.hasStartAnchor && spd > MotionPowerConfig::STANDSTILL_HEADING_LOCK_KMH) {
        if (_drStartMs == 0) _drStartMs = now;
        uint32_t drSec = (now - _drStartMs) / 1000;
        ctx.gnss.drDurationSec = drSec;

        bool snapped = false;
        if (_tunnel) {
            state.unlock();
            snapped = _tunnel->checkAndSnapToTunnel(lat, lon, hdg, spd, dt, false);
            state.lock();
        }

        if (!snapped) {
            if (drSec <= 180) {
                // 2D Planar Dead Reckoning using Dual-IMU fused yaw heading & speed (max 180s outside mapped tunnels)
                double distM = (spd / 3.6) * dt;
                double rad   = hdg * 0.0174532925;
                lat += (distM * cos(rad)) / 111139.0;
                lon += (distM * sin(rad)) / (111139.0 * cos(lat * 0.0174532925));
                ctx.gnss.isDeadReckoning = true;
            } else {
                // Unassisted DR timeout (e.g., parked underground) -> cease ghost reckoning
                ctx.gnss.isDeadReckoning = false;
                ctx.gnss.speedKmh        = 0.0f;
                spd                      = 0.0f;
            }
        } else {
            ctx.gnss.isDeadReckoning = true;
        }

        ctx.gnss.latitude   = lat;
        ctx.gnss.longitude  = lon;
        ctx.gnss.latE7      = (int32_t)(lat * 1e7);
        ctx.gnss.lonE7      = (int32_t)(lon * 1e7);
        ctx.gnss.headingDeg = hdg;
    } else if (gnssValid) {
        _drStartMs = 0;
        ctx.gnss.isDeadReckoning = false;
        ctx.gnss.drDurationSec   = 0;
        ctx.gnss.isInTunnel      = false;

        if (!ctx.trip.hasStartAnchor && lat != 0.0 && lon != 0.0) {
            ctx.trip.startLatitude  = lat;
            ctx.trip.startLongitude = lon;
            ctx.trip.hasStartAnchor = true;
            _lastTripLat = lat;
            _lastTripLon = lon;
            state.unlock();
            state.pushTrailPoint(lat, lon, spd);
            state.lock();
        }
    } else {
        ctx.gnss.isDeadReckoning = false;
    }

    // Update 1Hz Trip distance, moving/idle timers, and speed structure stats across all 3 trip tiers
    uint32_t curSec = now / 1000;
    if (curSec != _lastStatSec) {
        uint32_t elapsedSec = curSec - _lastStatSec;
        _lastStatSec = curSec;

        double stepM = 0.0;
        if (spd > MotionPowerConfig::STANDSTILL_HEADING_LOCK_KMH) {
            ctx.lastStandstillChangeMs = now;
            if (gnssValid || ctx.gnss.isDeadReckoning) {
                if (_lastTripLat == 0.0 && _lastTripLon == 0.0) {
                    _lastTripLat = lat;
                    _lastTripLon = lon;
                } else {
                    double dLatM = (lat - _lastTripLat) * 111139.0;
                    double dLonM = (lon - _lastTripLon) * 111139.0 * cos(lat * 0.0174532925);
                    stepM = sqrt(dLatM * dLatM + dLonM * dLonM);
                    if (stepM >= 2.0 && stepM < 250.0) {
                        _lastTripLat = lat;
                        _lastTripLon = lon;
                        state.unlock();
                        state.pushTrailPoint(lat, lon, spd);
                        state.lock();
                    } else {
                        stepM = 0.0;
                    }
                }
            }
        }

        state.updateTripsMotion(spd, elapsedSec, stepM, lat, lon, gnssValid || ctx.gnss.isDeadReckoning);
    }

    state.unlock();
}
