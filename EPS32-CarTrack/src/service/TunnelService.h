#pragma once
#include <Arduino.h>
#include "model/SystemState.h"
#include "service/TunnelDb.h"

class TunnelService {
public:
    static TunnelService& instance() {
        static TunnelService inst;
        return inst;
    }

    void init();
    void update(int32_t latE7, int32_t lonE7, float headingDeg, float speedKmh, bool gnssValid);

    bool isLocked() const { return _activeTunnel != nullptr; }
    const TunnelInfo* getActiveTunnel() const { return _activeTunnel; }
    float getProgressKm() const { return _progressKm; }

private:
    TunnelService() : _activeTunnel(nullptr), _progressKm(0.0f), _lastUpdateMs(0) {}

    const TunnelInfo* _activeTunnel;
    float             _progressKm;
    unsigned long     _lastUpdateMs;

    float calculateDistanceMeters(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2);
};
