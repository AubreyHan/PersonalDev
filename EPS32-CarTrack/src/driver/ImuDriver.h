#pragma once
#include <Arduino.h>
#include "model/SystemState.h"

class ImuDriver {
public:
    bool init();
    void update();
    const DualImuStatus& getStatus() const { return _status; }

private:
    bool readBmi270Data(uint8_t addr, float& ax, float& ay, float& az, float& gx, float& gy, float& gz);

    DualImuStatus _status           = {};
    uint32_t      _lastSampleMs     = 0;
    uint32_t      _masterFailStartMs= 0;
};
