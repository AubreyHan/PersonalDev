#pragma once
#include <Arduino.h>
#include "model/SystemState.h"

class WeatherService {
public:
    static WeatherService& instance() {
        static WeatherService inst;
        return inst;
    }

    void init();
    void update(bool wifiConnected, bool gnssValid, double lat, double lon);

private:
    WeatherService() : _lastFetchTime(0), _lastLat(0.0), _lastLon(0.0), _isFetching(false) {}

    unsigned long _lastFetchTime;
    double        _lastLat;
    double        _lastLon;
    bool          _isFetching;

    void fetchWeather(double lat, double lon);
};
