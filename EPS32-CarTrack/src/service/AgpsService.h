#pragma once
#include <Arduino.h>
#include "driver/GnssDriver.h"
#include "model/SystemState.h"

class AgpsService {
public:
    void init(GnssDriver* gnss);
    void update();
    bool downloadAndInjectAssistNow(const char* token);

private:
    GnssDriver* _gnss             = nullptr;
    bool        _injectedThisBoot = false;
    uint32_t    _lastTryMs        = 0;
};
