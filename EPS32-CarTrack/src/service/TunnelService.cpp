#include "TunnelService.h"

#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#include "../model/SystemState.h"

namespace {
    constexpr double EARTH_RADIUS_M = 6371000.0;
    constexpr double DEG_TO_RAD_D   = 0.017453292519943295;
    constexpr double RAD_TO_DEG_D   = 57.29577951308232;
    constexpr uint16_t MAX_TUNNELS_CAP = 2048U;
}

TunnelService& TunnelService::instance() {
    static TunnelService s_instance;
    return s_instance;
}

double TunnelService::haversineMeters(double lat1, double lon1, double lat2, double lon2) {
    const double dLat = (lat2 - lat1) * DEG_TO_RAD_D;
    const double dLon = (lon2 - lon1) * DEG_TO_RAD_D;
    const double rLat1 = lat1 * DEG_TO_RAD_D;
    const double rLat2 = lat2 * DEG_TO_RAD_D;
    const double sinLat = sin(dLat * 0.5);
    const double sinLon = sin(dLon * 0.5);
    double a = sinLat * sinLat + cos(rLat1) * cos(rLat2) * sinLon * sinLon;
    if (a < 0.0) a = 0.0;
    if (a > 1.0) a = 1.0;
    return 2.0 * EARTH_RADIUS_M * atan2(sqrt(a), sqrt(1.0 - a));
}

float TunnelService::forwardBearingDeg(double lat1, double lon1, double lat2, double lon2) {
    const double rLat1 = lat1 * DEG_TO_RAD_D;
    const double rLat2 = lat2 * DEG_TO_RAD_D;
    const double dLon  = (lon2 - lon1) * DEG_TO_RAD_D;
    const double y     = sin(dLon) * cos(rLat2);
    const double x     = cos(rLat1) * sin(rLat2) - sin(rLat1) * cos(rLat2) * cos(dLon);
    double brng        = atan2(y, x) * RAD_TO_DEG_D;
    brng               = fmod(brng + 360.0, 360.0);
    if (brng < 0.0) brng += 360.0;
    return static_cast<float>(brng);
}

float TunnelService::headingAbsDiffDeg(float aDeg, float bDeg) {
    float diff = fmodf(fabsf(aDeg - bDeg), 360.0f);
    if (diff > 180.0f) {
        diff = 360.0f - diff;
    }
    return diff;
}

bool TunnelService::allocAndCopyPsram(const TunnelInfo* src, uint16_t count, const char* verTag) {
    if (src == nullptr || count == 0 || count > MAX_TUNNELS_CAP) {
        return false;
    }

    const size_t bytes = static_cast<size_t>(count) * sizeof(TunnelInfo);
    void* mem = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mem == nullptr) {
        mem = malloc(bytes);
    }
    if (mem == nullptr) {
        return false;
    }

    memcpy(mem, src, bytes);
    if (_psramTunnels != nullptr) {
        heap_caps_free(_psramTunnels);
    }
    _psramTunnels = static_cast<TunnelInfo*>(mem);
    _tunnelCount  = count;

    memset(_dbVersion, 0, sizeof(_dbVersion));
    if (verTag != nullptr && verTag[0] != '\0') {
        strncpy(_dbVersion, verTag, sizeof(_dbVersion) - 1);
    } else {
        strncpy(_dbVersion, "flash-bin", sizeof(_dbVersion) - 1);
    }

    {
        StateGuard guard(SystemState::instance());
        if (guard) {
            auto& db          = SystemState::instance().data().tunnelDb;
            db.loadedInPsram  = true;
            db.tunnelCount    = _tunnelCount;
            memset(db.version, 0, sizeof(db.version));
            strncpy(db.version, _dbVersion, sizeof(db.version) - 1);
        }
    }
    return true;
}

bool TunnelService::loadBuiltinIntoPsram() {
    return allocAndCopyPsram(
        TunnelDb::BUILTIN_TUNNELS,
        static_cast<uint16_t>(TunnelDb::BUILTIN_TUNNEL_COUNT),
        TunnelDb::BUILTIN_VER
    );
}

bool TunnelService::writeBuiltinToLittleFs() {
    if (!_littlefsMounted) {
        return false;
    }

    File f = LittleFS.open(LittleFsConfig::TUNNELS_BIN_REL, "w");
    if (!f) {
        return false;
    }

    TunnelDb::TunnelBinHeader hdr = {};
    hdr.magic         = TunnelDb::MAGIC_TUNL;
    hdr.formatVersion = TunnelDb::FORMAT_VERSION;
    hdr.recordCount   = static_cast<uint16_t>(TunnelDb::BUILTIN_TUNNEL_COUNT);
    hdr.recordStride  = static_cast<uint16_t>(sizeof(TunnelInfo));
    hdr.reserved      = 0;
    strncpy(hdr.version, TunnelDb::BUILTIN_VER, sizeof(hdr.version) - 1);

    const size_t w1 = f.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));
    const size_t expectedDataBytes = sizeof(TunnelDb::BUILTIN_TUNNELS);
    const size_t w2 = f.write(reinterpret_cast<const uint8_t*>(TunnelDb::BUILTIN_TUNNELS), expectedDataBytes);
    f.close();

    return (w1 == sizeof(hdr) && w2 == expectedDataBytes);
}

bool TunnelService::reloadFromFlash() {
    if (!_littlefsMounted) {
        _littlefsMounted = LittleFS.begin(
            true,
            LittleFsConfig::BASE_PATH,
            10,
            LittleFsConfig::PARTITION_LABEL
        );
    }

    if (!_littlefsMounted) {
        return loadBuiltinIntoPsram();
    }

    if (!LittleFS.exists(LittleFsConfig::TUNNELS_BIN_REL)) {
        (void)writeBuiltinToLittleFs();
    }

    File f = LittleFS.open(LittleFsConfig::TUNNELS_BIN_REL, "r");
    if (!f) {
        return loadBuiltinIntoPsram();
    }

    const size_t fileSize = f.size();
    if (fileSize < sizeof(TunnelInfo)) {
        f.close();
        (void)writeBuiltinToLittleFs();
        return loadBuiltinIntoPsram();
    }

    // Check whether file starts with TunnelBinHeader ('TUNL') or is raw TunnelInfo[]
    TunnelDb::TunnelBinHeader hdr = {};
    if (fileSize >= sizeof(hdr) + sizeof(TunnelInfo)) {
        if (f.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) == sizeof(hdr) &&
            hdr.magic == TunnelDb::MAGIC_TUNL &&
            hdr.recordStride == sizeof(TunnelInfo) &&
            hdr.recordCount > 0 &&
            hdr.recordCount <= MAX_TUNNELS_CAP &&
            (sizeof(hdr) + static_cast<size_t>(hdr.recordCount) * sizeof(TunnelInfo)) <= fileSize) {

            const size_t payloadBytes = static_cast<size_t>(hdr.recordCount) * sizeof(TunnelInfo);
            void* mem = heap_caps_malloc(payloadBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (mem == nullptr) {
                mem = malloc(payloadBytes);
            }
            if (mem != nullptr) {
                const size_t got = f.read(static_cast<uint8_t*>(mem), payloadBytes);
                f.close();
                if (got == payloadBytes) {
                    if (_psramTunnels != nullptr) {
                        heap_caps_free(_psramTunnels);
                    }
                    _psramTunnels = static_cast<TunnelInfo*>(mem);
                    _tunnelCount  = hdr.recordCount;

                    char safeVer[24] = {};
                    memcpy(safeVer, hdr.version, sizeof(safeVer) - 1);
                    if (safeVer[0] == '\0') {
                        strncpy(safeVer, "flash-bin", sizeof(safeVer) - 1);
                    }
                    memset(_dbVersion, 0, sizeof(_dbVersion));
                    strncpy(_dbVersion, safeVer, sizeof(_dbVersion) - 1);

                    StateGuard guard(SystemState::instance());
                    if (guard) {
                        auto& db         = SystemState::instance().data().tunnelDb;
                        db.loadedInPsram = true;
                        db.tunnelCount   = _tunnelCount;
                        memset(db.version, 0, sizeof(db.version));
                        strncpy(db.version, _dbVersion, sizeof(db.version) - 1);
                    }
                    releaseTunnelLock();
                    return true;
                }
                heap_caps_free(mem);
                return loadBuiltinIntoPsram();
            }
        }
    }

    // Fallback: raw headerless TunnelInfo[] array uploaded via Web portal
    if ((fileSize % sizeof(TunnelInfo)) == 0) {
        const size_t rawCount = fileSize / sizeof(TunnelInfo);
        if (rawCount > 0 && rawCount <= MAX_TUNNELS_CAP) {
            f.seek(0);
            void* mem = heap_caps_malloc(fileSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (mem == nullptr) {
                mem = malloc(fileSize);
            }
            if (mem != nullptr) {
                const size_t got = f.read(static_cast<uint8_t*>(mem), fileSize);
                f.close();
                if (got == fileSize) {
                    if (_psramTunnels != nullptr) {
                        heap_caps_free(_psramTunnels);
                    }
                    _psramTunnels = static_cast<TunnelInfo*>(mem);
                    _tunnelCount  = static_cast<uint16_t>(rawCount);
                    strncpy(_dbVersion, "custom-bin", sizeof(_dbVersion) - 1);

                    StateGuard guard(SystemState::instance());
                    if (guard) {
                        auto& db         = SystemState::instance().data().tunnelDb;
                        db.loadedInPsram = true;
                        db.tunnelCount   = _tunnelCount;
                        memset(db.version, 0, sizeof(db.version));
                        strncpy(db.version, _dbVersion, sizeof(db.version) - 1);
                    }
                    releaseTunnelLock();
                    return true;
                }
                heap_caps_free(mem);
            }
        }
    }

    f.close();
    return loadBuiltinIntoPsram();
}

bool TunnelService::begin() {
    // MANDATORY 4-argument LittleFS mount per config.h & #3 correction #9
    _littlefsMounted = LittleFS.begin(
        true,
        LittleFsConfig::BASE_PATH,
        10,
        LittleFsConfig::PARTITION_LABEL
    );

    {
        StateGuard guard(SystemState::instance());
        if (guard) {
            SystemState::instance().data().boot.littlefsOk = _littlefsMounted;
        }
    }

    return reloadFromFlash();
}

void TunnelService::pollReloadCommand() {
    bool shouldReload = false;
    {
        StateGuard guard(SystemState::instance());
        if (guard && SystemState::instance().data().tunnelDb.cmdReloadFromFlash) {
            SystemState::instance().data().tunnelDb.cmdReloadFromFlash = false;
            shouldReload = true;
        }
    }
    if (shouldReload) {
        (void)reloadFromFlash();
    }
}

void TunnelService::buildOrderedPolyline(const TunnelInfo& info, bool reverseDir,
                                         double curLat, double curLon) {
    _nodeCount = 0;
    const uint8_t wpCount = (info.waypointCount <= TunnelInfo::MAX_WAYPOINTS)
                                ? info.waypointCount
                                : static_cast<uint8_t>(TunnelInfo::MAX_WAYPOINTS);

    if (!reverseDir) {
        _nodeLat[_nodeCount] = static_cast<double>(info.entryLatE7) * 1e-7;
        _nodeLon[_nodeCount] = static_cast<double>(info.entryLonE7) * 1e-7;
        _nodeCount++;
        for (uint8_t i = 0; i < wpCount; ++i) {
            _nodeLat[_nodeCount] = static_cast<double>(info.waypoints[i].latE7) * 1e-7;
            _nodeLon[_nodeCount] = static_cast<double>(info.waypoints[i].lonE7) * 1e-7;
            _nodeCount++;
        }
        _nodeLat[_nodeCount] = static_cast<double>(info.exitLatE7) * 1e-7;
        _nodeLon[_nodeCount] = static_cast<double>(info.exitLonE7) * 1e-7;
        _nodeCount++;
    } else {
        _nodeLat[_nodeCount] = static_cast<double>(info.exitLatE7) * 1e-7;
        _nodeLon[_nodeCount] = static_cast<double>(info.exitLonE7) * 1e-7;
        _nodeCount++;
        for (int8_t i = static_cast<int8_t>(wpCount) - 1; i >= 0; --i) {
            _nodeLat[_nodeCount] = static_cast<double>(info.waypoints[i].latE7) * 1e-7;
            _nodeLon[_nodeCount] = static_cast<double>(info.waypoints[i].lonE7) * 1e-7;
            _nodeCount++;
        }
        _nodeLat[_nodeCount] = static_cast<double>(info.entryLatE7) * 1e-7;
        _nodeLon[_nodeCount] = static_cast<double>(info.entryLonE7) * 1e-7;
        _nodeCount++;
    }

    _segCumM[0] = 0.0f;
    for (uint8_t i = 0; i + 1U < _nodeCount; ++i) {
        const float segLen = static_cast<float>(
            haversineMeters(_nodeLat[i], _nodeLon[i], _nodeLat[i + 1], _nodeLon[i + 1])
        );
        _segCumM[i + 1] = _segCumM[i] + segLen;
        _segBearDeg[i]  = forwardBearingDeg(_nodeLat[i], _nodeLon[i], _nodeLat[i + 1], _nodeLon[i + 1]);
    }
    if (_nodeCount >= 2U) {
        _segBearDeg[_nodeCount - 1] = _segBearDeg[_nodeCount - 2];
    }

    const float geomLen = _segCumM[_nodeCount - 1];
    _totalLengthMeters  = (info.lengthMeters > geomLen) ? info.lengthMeters : geomLen;
    _progressMeters     = static_cast<float>(projectPointToPolylineMeters(curLat, curLon));
}

double TunnelService::projectPointToPolylineMeters(double lat, double lon) const {
    if (_nodeCount < 2U) {
        return 0.0;
    }

    const double cosLat = cos(lat * DEG_TO_RAD_D);
    double bestDistSq   = 1e18;
    double bestArcM     = 0.0;

    for (uint8_t i = 0; i + 1U < _nodeCount; ++i) {
        const double ax = 0.0;
        const double ay = 0.0;
        const double bx = (_nodeLon[i + 1] - _nodeLon[i]) * DEG_TO_RAD_D * EARTH_RADIUS_M * cosLat;
        const double by = (_nodeLat[i + 1] - _nodeLat[i]) * DEG_TO_RAD_D * EARTH_RADIUS_M;
        const double px = (lon - _nodeLon[i]) * DEG_TO_RAD_D * EARTH_RADIUS_M * cosLat;
        const double py = (lat - _nodeLat[i]) * DEG_TO_RAD_D * EARTH_RADIUS_M;

        const double abx   = bx - ax;
        const double aby   = by - ay;
        const double abLenSq = abx * abx + aby * aby;

        double t = 0.0;
        if (abLenSq > 1e-3) {
            t = ((px - ax) * abx + (py - ay) * aby) / abLenSq;
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;
        }

        const double qx = ax + t * abx;
        const double qy = ay + t * aby;
        const double dx = px - qx;
        const double dy = py - qy;
        const double dSq = dx * dx + dy * dy;

        if (dSq < bestDistSq) {
            bestDistSq = dSq;
            const double segLen = static_cast<double>(_segCumM[i + 1] - _segCumM[i]);
            bestArcM            = static_cast<double>(_segCumM[i]) + t * segLen;
        }
    }

    const double geomLen = static_cast<double>(_segCumM[_nodeCount - 1]);
    if (geomLen > 1.0 && _totalLengthMeters > geomLen) {
        bestArcM *= (static_cast<double>(_totalLengthMeters) / geomLen);
    }
    return bestArcM;
}

void TunnelService::samplePolylineAtMeters(double sMeters, double& outLat,
                                           double& outLon, float& outBearingDeg) const {
    if (_nodeCount < 2U) {
        return;
    }

    const double geomLen = static_cast<double>(_segCumM[_nodeCount - 1]);
    double sGeom         = sMeters;
    if (_totalLengthMeters > 1.0f && geomLen > 1.0) {
        sGeom = sMeters * (geomLen / static_cast<double>(_totalLengthMeters));
    }

    if (sGeom <= 0.0) {
        outLat        = _nodeLat[0];
        outLon        = _nodeLon[0];
        outBearingDeg = _segBearDeg[0];
        return;
    }
    if (sGeom >= geomLen) {
        outLat        = _nodeLat[_nodeCount - 1];
        outLon        = _nodeLon[_nodeCount - 1];
        outBearingDeg = _segBearDeg[_nodeCount - 2];
        return;
    }

    for (uint8_t i = 0; i + 1U < _nodeCount; ++i) {
        const double s0 = static_cast<double>(_segCumM[i]);
        const double s1 = static_cast<double>(_segCumM[i + 1]);
        if (sGeom <= s1 || (i + 2U == _nodeCount)) {
            const double span = (s1 - s0 > 1e-3) ? (s1 - s0) : 1.0;
            double t          = (sGeom - s0) / span;
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;
            outLat        = _nodeLat[i] + t * (_nodeLat[i + 1] - _nodeLat[i]);
            outLon        = _nodeLon[i] + t * (_nodeLon[i + 1] - _nodeLon[i]);
            outBearingDeg = _segBearDeg[i];
            return;
        }
    }
}

bool TunnelService::checkPortalApproach(double lat, double lon, float headingDeg,
                                        float speedKmh, uint32_t nowMs) {
    pollReloadCommand();

    if (_psramTunnels == nullptr || _tunnelCount == 0) {
        return false;
    }

    // If vehicle has valid 3D GNSS Fix and was previously in tunnel, check if past exit or clear sky
    if (_inTunnel) {
        const double exitDistM = haversineMeters(
            lat, lon, _nodeLat[_nodeCount - 1], _nodeLon[_nodeCount - 1]
        );
        const double projS = projectPointToPolylineMeters(lat, lon);
        if (projS >= (_totalLengthMeters - 120.0f) || exitDistM <= 250.0) {
            releaseTunnelLock();
            return false;
        }
    }

    if (speedKmh <= GnssConfig::STATIC_DEADBAND_KMH) {
        return _portalPrimed;
    }

    double bestDistM  = static_cast<double>(TunnelConfig::ENTRY_MATCH_RADIUS_M) + 1.0;
    int32_t bestIdx   = -1;
    bool bestReverse  = false;

    for (uint16_t i = 0; i < _tunnelCount; ++i) {
        const TunnelInfo& t = _psramTunnels[i];
        const double eLat   = static_cast<double>(t.entryLatE7) * 1e-7;
        const double eLon   = static_cast<double>(t.entryLonE7) * 1e-7;
        const double xLat   = static_cast<double>(t.exitLatE7) * 1e-7;
        const double xLon   = static_cast<double>(t.exitLonE7) * 1e-7;

        // Forward approach (entry -> exit)
        const double dEntry = haversineMeters(lat, lon, eLat, eLon);
        if (dEntry <= TunnelConfig::ENTRY_MATCH_RADIUS_M && dEntry < bestDistM) {
            const float fwdBear = (t.entryBearingDeg > 0.01f || t.entryBearingDeg < -0.01f)
                                      ? t.entryBearingDeg
                                      : forwardBearingDeg(eLat, eLon, xLat, xLon);
            if (headingAbsDiffDeg(headingDeg, fwdBear) <= TunnelConfig::ENTRY_MAX_HEADING_DIFF_DEG) {
                bestDistM   = dEntry;
                bestIdx     = static_cast<int32_t>(i);
                bestReverse = false;
            }
        }

        // Reverse approach (exit -> entry) for opposite highway carriageway
        const double dExit = haversineMeters(lat, lon, xLat, xLon);
        if (dExit <= TunnelConfig::ENTRY_MATCH_RADIUS_M && dExit < bestDistM) {
            const double prevLat = (t.waypointCount > 0)
                                       ? (static_cast<double>(t.waypoints[t.waypointCount - 1].latE7) * 1e-7)
                                       : eLat;
            const double prevLon = (t.waypointCount > 0)
                                       ? (static_cast<double>(t.waypoints[t.waypointCount - 1].lonE7) * 1e-7)
                                       : eLon;
            const float revBear = forwardBearingDeg(xLat, xLon, prevLat, prevLon);
            if (headingAbsDiffDeg(headingDeg, revBear) <= TunnelConfig::ENTRY_MAX_HEADING_DIFF_DEG) {
                bestDistM   = dExit;
                bestIdx     = static_cast<int32_t>(i);
                bestReverse = true;
            }
        }
    }

    if (bestIdx >= 0) {
        _activeIdx     = static_cast<uint16_t>(bestIdx);
        _activeReverse = bestReverse;
        _portalPrimed  = true;
        _primedAtMs    = nowMs;
        memset(_activeName, 0, sizeof(_activeName));
        strncpy(_activeName, _psramTunnels[_activeIdx].name, sizeof(_activeName) - 1);
        buildOrderedPolyline(_psramTunnels[_activeIdx], _activeReverse, lat, lon);
        return true;
    }

    // Expire primed portal state if 3D Fix remained clear for > 18s without entering blind zone
    if (_portalPrimed && !_inTunnel && (nowMs - _primedAtMs) > 18000UL) {
        _portalPrimed = false;
    }
    return _portalPrimed;
}

bool TunnelService::updateTunnelConstraint(
    double&  inOutLat,
    double&  inOutLon,
    float&   inOutHeadingDeg,
    float    stepDistMeters,
    bool     hasValid3DFix,
    uint32_t nowMs) {

    pollReloadCommand();

    if (hasValid3DFix) {
        if (_inTunnel) {
            releaseTunnelLock();
        }
        return false;
    }

    // If GNSS just lost 3D Fix without being pre-primed, do one final portal snap check
    if (!_inTunnel && !_portalPrimed && _psramTunnels != nullptr && _tunnelCount > 0) {
        (void)checkPortalApproach(inOutLat, inOutLon, inOutHeadingDeg, 36.0f, nowMs);
    }

    if (!_inTunnel && _portalPrimed) {
        _inTunnel     = true;
        _portalPrimed = false;
        _progressMeters = static_cast<float>(projectPointToPolylineMeters(inOutLat, inOutLon));
    }

    if (!_inTunnel || _nodeCount < 2U) {
        return false;
    }

    if (stepDistMeters > 0.0f) {
        _progressMeters += stepDistMeters;
    }

    // Release tunnel constraint once vehicle clears the exit portal (+50m margin)
    if (_progressMeters >= (_totalLengthMeters + 50.0f)) {
        releaseTunnelLock();
        return false;
    }

    const double clampedS = (_progressMeters < 0.0f)
                                ? 0.0
                                : ((_progressMeters > _totalLengthMeters)
                                       ? static_cast<double>(_totalLengthMeters)
                                       : static_cast<double>(_progressMeters));

    samplePolylineAtMeters(clampedS, inOutLat, inOutLon, inOutHeadingDeg);
    return true;
}

void TunnelService::releaseTunnelLock() {
    _portalPrimed      = false;
    _inTunnel          = false;
    _progressMeters    = 0.0f;
    _totalLengthMeters = 0.0f;
    memset(_activeName, 0, sizeof(_activeName));

    StateGuard guard(SystemState::instance());
    if (guard) {
        auto& gnss            = SystemState::instance().data().gnss;
        gnss.isInTunnel       = false;
        gnss.tunnelProgressKm = 0.0f;
        gnss.tunnelLengthKm   = 0.0f;
        memset(gnss.tunnelName, 0, sizeof(gnss.tunnelName));
    }
}
