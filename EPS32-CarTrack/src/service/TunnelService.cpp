#include "service/TunnelService.h"
#include "service/TunnelDb.h"
#include <LittleFS.h>
#include <math.h>

static double distanceMeters(double lat1, double lon1, double lat2, double lon2) {
    double dLat = (lat2 - lat1) * 111139.0;
    double dLon = (lon2 - lon1) * 111139.0 * cos(lat1 * 0.0174532925);
    return sqrt(dLat * dLat + dLon * dLon);
}

bool TunnelService::init() {
    _tunnelCount = TunnelDatabase::BUILTIN_TUNNEL_COUNT;
    reloadFromLittleFS();
    return true;
}

bool TunnelService::reloadFromLittleFS() {
    if (LittleFS.exists(StorageConfig::TUNNEL_LITTLEFS_PATH)) {
        File f = LittleFS.open(StorageConfig::TUNNEL_LITTLEFS_PATH, "r");
        if (f && f.size() >= 4) {
            uint16_t count = 0;
            f.read((uint8_t*)&count, 2);
            if (count > 0 && count < 1000) {
                _tunnelCount = count;
            }
            f.close();
            return true;
        }
    }
    return false;
}

bool TunnelService::checkAndSnapToTunnel(double& ioLat, double& ioLon, float& ioHeadingDeg, float speedKmh, float dtSec, bool gnssValid) {
    SystemState& state = SystemState::instance();

    if (gnssValid) {
        _activeTunnel   = nullptr;
        _progressMeters = 0.0f;
        state.lock();
        state.getContextUnsafe().gnss.isInTunnel = false;
        state.unlock();
        return false;
    }

    // If GNSS lost, check if within 200m of any tunnel entry
    if (!_activeTunnel) {
        for (size_t i = 0; i < TunnelDatabase::BUILTIN_TUNNEL_COUNT; i++) {
            const auto& t = TunnelDatabase::BUILTIN_TUNNELS[i];
            double entryLat = t.entryLatE7 / 1e7;
            double entryLon = t.entryLonE7 / 1e7;
            double dist = distanceMeters(ioLat, ioLon, entryLat, entryLon);
            float hdgDiff = fabsf(ioHeadingDeg - t.entryBearingDeg);
            if (hdgDiff > 180.0f) hdgDiff = 360.0f - hdgDiff;

            if (dist <= 200.0 && hdgDiff <= 35.0f) {
                _activeTunnel   = &t;
                _progressMeters = (float)dist;
                break;
            }
        }
    }

    if (_activeTunnel) {
        _progressMeters += (speedKmh / 3.6f) * dtSec;
        float ratio = _progressMeters / _activeTunnel->lengthMeters;
        if (ratio >= 1.0f) {
            _activeTunnel = nullptr;
            return false;
        }

        double sLat = _activeTunnel->entryLatE7 / 1e7;
        double sLon = _activeTunnel->entryLonE7 / 1e7;
        double eLat = _activeTunnel->exitLatE7  / 1e7;
        double eLon = _activeTunnel->exitLonE7  / 1e7;

        ioLat = sLat + (eLat - sLat) * ratio;
        ioLon = sLon + (eLon - sLon) * ratio;
        ioHeadingDeg = _activeTunnel->entryBearingDeg;

        state.lock();
        auto& gnss = state.getContextUnsafe().gnss;
        gnss.isInTunnel = true;
        strncpy(gnss.tunnelName, _activeTunnel->name, sizeof(gnss.tunnelName) - 1);
        gnss.tunnelProgressKm = _progressMeters * 0.001f;
        gnss.tunnelLengthKm   = _activeTunnel->lengthMeters * 0.001f;
        state.unlock();
        return true;
    }

    return false;
}
