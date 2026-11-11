#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include "TunnelDb.h"

// =============================================================================
// TunnelService - LittleFS -> PSRAM Tunnel Database & 1D Polyline Axis Constraint
//
// Features:
// - Mounts LittleFS using mandatory 4-arg call:
//   LittleFS.begin(true, LittleFsConfig::BASE_PATH, 10, LittleFsConfig::PARTITION_LABEL)
// - Preloads /littlefs/tunnels.bin into PSRAM in < 2ms (generates /tunnels.bin
//   from TunnelDb::BUILTIN_TUNNELS if absent, with zero-IO built-in fallback)
// - Bidirectional portal capture within 200m Haversine radius + <=35 deg heading
// - 1D polyline arc-length projection inside extra-long tunnels: eliminates lateral
//   gyro drift, updates tunnelProgressKm / tunnelLengthKm, and releases on exit
// =============================================================================
class TunnelService {
public:
    static TunnelService& instance();

    // Mount LittleFS, seed /tunnels.bin if missing, and load into PSRAM.
    bool begin();

    // Check cmdReloadFromFlash and reload /littlefs/tunnels.bin into PSRAM if requested.
    void pollReloadCommand();
    bool reloadFromFlash();

    // Called while valid 3D GNSS Fix is active to detect approaching tunnel portals.
    // Returns true if vehicle is currently captured near a portal or inside a tunnel.
    bool checkPortalApproach(double lat, double lon, float headingDeg, float speedKmh, uint32_t nowMs);

    // Called during Dead Reckoning (or portal transition) to advance & project
    // vehicle position and heading onto the locked tunnel's 1D polyline axis.
    bool updateTunnelConstraint(
        double&  inOutLat,
        double&  inOutLon,
        float&   inOutHeadingDeg,
        float    stepDistMeters,
        bool     hasValid3DFix,
        uint32_t nowMs
    );

    void releaseTunnelLock();

    bool        isInTunnel() const { return _inTunnel; }
    const char* activeTunnelName() const { return _activeName; }
    float       progressKm() const { return _progressMeters * 0.001f; }
    float       lengthKm() const { return _totalLengthMeters * 0.001f; }
    uint16_t    tunnelCount() const { return _tunnelCount; }

    static double haversineMeters(double lat1, double lon1, double lat2, double lon2);
    static float  forwardBearingDeg(double lat1, double lon1, double lat2, double lon2);
    static float  headingAbsDiffDeg(float aDeg, float bDeg);

private:
    TunnelService() = default;
    ~TunnelService() = default;
    TunnelService(const TunnelService&) = delete;
    TunnelService& operator=(const TunnelService&) = delete;

    static constexpr size_t MAX_AXIS_NODES = TunnelInfo::MAX_WAYPOINTS + 2;

    bool writeBuiltinToLittleFs();
    bool loadBuiltinIntoPsram();
    bool allocAndCopyPsram(const TunnelInfo* src, uint16_t count, const char* verTag);
    void buildOrderedPolyline(const TunnelInfo& info, bool reverseDir, double entryLat, double entryLon);
    double projectPointToPolylineMeters(double lat, double lon) const;
    void samplePolylineAtMeters(double sMeters, double& outLat, double& outLon, float& outBearingDeg) const;

    bool        _littlefsMounted   = false;
    TunnelInfo* _psramTunnels      = nullptr;
    uint16_t    _tunnelCount       = 0;
    char        _dbVersion[24]     = "builtin";

    // Active Tunnel Lock State
    bool        _portalPrimed      = false; // Within 200m of portal while 3D Fix still present
    bool        _inTunnel          = false; // Actively constraining trajectory inside tunnel
    uint16_t    _activeIdx         = 0;
    bool        _activeReverse     = false;
    char        _activeName[32]    = "";
    uint32_t    _primedAtMs        = 0;

    uint8_t     _nodeCount         = 0;
    double      _nodeLat[MAX_AXIS_NODES]    = {};
    double      _nodeLon[MAX_AXIS_NODES]    = {};
    float       _segCumM[MAX_AXIS_NODES]    = {};
    float       _segBearDeg[MAX_AXIS_NODES] = {};
    float       _totalLengthMeters = 0.0f;
    float       _progressMeters    = 0.0f;
};
