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

    if (!gnssValid && ctx.trip.hasStartAnchor && spd > MotionPowerConfig::STANDSTILL_HEADING_LOCK_KMH) {
        if (_drStartMs == 0) _drStartMs = now;
        ctx.gnss.isDeadReckoning = true;
        ctx.gnss.drDurationSec   = (now - _drStartMs) / 1000;

        bool snapped = false;
        if (_tunnel) {
            state.unlock();
            snapped = _tunnel->checkAndSnapToTunnel(lat, lon, hdg, spd, dt, false);
            state.lock();
        }

        if (!snapped) {
            // 2D Planar Dead Reckoning using Dual-IMU fused yaw heading & speed
            double distM = (spd / 3.6) * dt;
            double rad   = hdg * 0.0174532925;
            lat += (distM * cos(rad)) / 111139.0;
            lon += (distM * sin(rad)) / (111139.0 * cos(lat * 0.0174532925));
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
    }

    // Update 1Hz Trip distance, moving/idle timers, and speed structure stats
    uint32_t curSec = now / 1000;
    if (curSec != _lastStatSec) {
        uint32_t elapsedSec = curSec - _lastStatSec;
        _lastStatSec = curSec;

        if (spd > MotionPowerConfig::STANDSTILL_HEADING_LOCK_KMH) {
            ctx.trip.movingDurationSec += elapsedSec;
            ctx.isVehicleMoving        = true;
            ctx.lastStandstillChangeMs = now;

            if (spd > 80.0f)      ctx.trip.highwaySec += elapsedSec;
            else if (spd >= 40.0f) ctx.trip.urbanSec  += elapsedSec;
            else                   ctx.trip.slowSec   += elapsedSec;

            if (spd > ctx.trip.maxSpeedKmh) {
                ctx.trip.maxSpeedKmh = spd;
            }

            if (ctx.trip.hasStartAnchor && (gnssValid || ctx.gnss.isDeadReckoning)) {
                double dLatM = (lat - _lastTripLat) * 111139.0;
                double dLonM = (lon - _lastTripLon) * 111139.0 * cos(lat * 0.0174532925);
                double stepM = sqrt(dLatM * dLatM + dLonM * dLonM);
                if (stepM >= 2.0 && stepM < 250.0) {
                    ctx.trip.tripDistanceKm += (float)(stepM * 0.001);
                    _lastTripLat = lat;
                    _lastTripLon = lon;
                    state.unlock();
                    state.pushTrailPoint(lat, lon, spd);
                    state.lock();
                }
            }

            if (ctx.trip.movingDurationSec > 0) {
                ctx.trip.avgSpeedKmh = (ctx.trip.tripDistanceKm * 3600.0f) / (float)ctx.trip.movingDurationSec;
            }
        } else {
            ctx.trip.idleDurationSec += elapsedSec;
            ctx.isVehicleMoving = false;
        }
    }

    state.unlock();
}
