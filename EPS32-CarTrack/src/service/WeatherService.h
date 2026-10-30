#pragma once
#include <Arduino.h>
#include "model/SystemState.h"

class WeatherService {
public:
    void init();
    void update();
    bool fetchOpenMeteo(double lat, double lon);

private:
    void saveToNvsCache(const WeatherData& w);
    bool loadFromNvsCache(WeatherData& outW);

    uint32_t _lastFetchMs = 0;
    bool     _hasFetched  = false;
};
