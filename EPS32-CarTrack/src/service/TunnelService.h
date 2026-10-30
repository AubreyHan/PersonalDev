#pragma once
#include <Arduino.h>
#include "model/SystemState.h"

class TunnelService {
public:
    bool init();
    bool reloadFromLittleFS();
    bool checkAndSnapToTunnel(double& ioLat, double& ioLon, float& ioHeadingDeg, float speedKmh, float dtSec, bool gnssValid);
    uint16_t getTunnelCount() const { return _tunnelCount; }

private:
    const TunnelInfo* _activeTunnel     = nullptr;
    TunnelInfo*       _psramTunnels     = nullptr;
    float             _progressMeters   = 0.0f;
    uint16_t          _tunnelCount      = 0;
};
