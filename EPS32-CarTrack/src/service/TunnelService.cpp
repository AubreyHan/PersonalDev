#include "service/TunnelService.h"
#include <math.h>

void TunnelService::init() {
    _activeTunnel = nullptr;
    _progressKm = 0.0f;
    _lastUpdateMs = millis();
}

float TunnelService::calculateDistanceMeters(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2) {
    double dLat = (lat2 - lat1) * 0.0000001 * 111139.0;
    double dLon = (lon2 - lon1) * 0.0000001 * 111139.0 * cos((lat1 * 0.0000001) * 0.01745329);
    return (float)sqrt(dLat * dLat + dLon * dLon);
}

void TunnelService::update(int32_t latE7, int32_t lonE7, float headingDeg, float speedKmh, bool gnssValid) {
    unsigned long now = millis();
    float dt = (now - _lastUpdateMs) / 1000.0f;
    _lastUpdateMs = now;

    SystemState& state = SystemState::instance();

    // 1. Check for tunnel entry if not currently locked
    if (!_activeTunnel) {
        for (size_t i = 0; i < BUILTIN_TUNNEL_COUNT; i++) {
            const TunnelInfo& t = BUILTIN_TUNNELS[i];
            float distToEntry = calculateDistanceMeters(latE7, lonE7, t.entryLatE7, t.entryLonE7);
            
            // Check within 200m and heading aligns within 25 degrees
            if (distToEntry < 200.0f) {
                float dHeading = fabs(headingDeg - t.entryBearingDeg);
                if (dHeading > 180.0f) dHeading = 360.0f - dHeading;
                if (dHeading < 25.0f) {
                    // Lock into tunnel
                    _activeTunnel = &t;
                    _progressKm = 0.0f;
                    break;
                }
            }
        }
    } else {
        // 2. Currently inside tunnel
        if (speedKmh > 0.0f && dt > 0.0f) {
            float moveDistKm = (speedKmh / 3.6f) * dt / 1000.0f;
            _progressKm += moveDistKm;
        }

        // Check if passed exit or full length
        float totalLenKm = _activeTunnel->lengthMeters / 1000.0f;
        if (_progressKm >= totalLenKm || (gnssValid && _progressKm > 0.5f)) {
            // Exit tunnel
            _activeTunnel = nullptr;
            _progressKm = 0.0f;
        }
    }

    state.lock();
    SystemContext& ctx = state.getContextUnsafe();
    if (_activeTunnel) {
        ctx.gnss.isInTunnel = true;
        strncpy(ctx.gnss.tunnelName, _activeTunnel->name, sizeof(ctx.gnss.tunnelName) - 1);
        ctx.gnss.tunnelProgressKm = _progressKm;
        ctx.gnss.tunnelLengthKm = _activeTunnel->lengthMeters / 1000.0f;

        // Perform 1D axis constraint interpolation along waypoints
        if (_activeTunnel->waypointCount >= 2) {
            float ratio = _progressKm / ctx.gnss.tunnelLengthKm;
            if (ratio > 1.0f) ratio = 1.0f;
            int32_t startLat = _activeTunnel->entryLatE7;
            int32_t startLon = _activeTunnel->entryLonE7;
            int32_t endLat = _activeTunnel->exitLatE7;
            int32_t endLon = _activeTunnel->exitLonE7;

            ctx.gnss.latE7 = startLat + (int32_t)((endLat - startLat) * ratio);
            ctx.gnss.lonE7 = startLon + (int32_t)((endLon - startLon) * ratio);
            ctx.gnss.latitude = ctx.gnss.latE7 / 1e7;
            ctx.gnss.longitude = ctx.gnss.lonE7 / 1e7;
            ctx.gnss.headingDeg = _activeTunnel->entryBearingDeg;
        }
    } else {
        ctx.gnss.isInTunnel = false;
        ctx.gnss.tunnelName[0] = '\0';
        ctx.gnss.tunnelProgressKm = 0.0f;
        ctx.gnss.tunnelLengthKm = 0.0f;
    }
    state.unlock();
}
