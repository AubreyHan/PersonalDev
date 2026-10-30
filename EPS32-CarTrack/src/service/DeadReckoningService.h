#pragma once
#include <Arduino.h>
#include "driver/ImuDriver.h"
#include "service/TunnelService.h"
#include "model/SystemState.h"

class DeadReckoningService {
public:
    void init(ImuDriver* imu, TunnelService* tunnel);
    void update();

private:
    ImuDriver*     _imu          = nullptr;
    TunnelService* _tunnel       = nullptr;
    uint32_t       _lastUpdateMs = 0;
    uint32_t       _drStartMs    = 0;
    double         _lastTripLat  = 0.0;
    double         _lastTripLon  = 0.0;
    uint32_t       _lastStatSec  = 0;
};
