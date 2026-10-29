#include "service/WeatherService.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

void WeatherService::init() {
    _lastFetchTime = 0;
    _lastLat = 0.0;
    _lastLon = 0.0;
    _isFetching = false;
}

void WeatherService::fetchWeather(double lat, double lon) {
    if (WiFi.status() != WL_CONNECTED) return;
    _isFetching = true;

    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m"
             "&hourly=temperature_2m,precipitation_probability,weather_code&forecast_hours=24&timezone=auto",
             lat, lon);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(5000);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
            SystemState& state = SystemState::instance();
            state.lock();
            SystemContext& ctx = state.getContextUnsafe();

            // Current weather
            ctx.weather.hasData = true;
            ctx.weather.temperatureC = doc["current"]["temperature_2m"] | 25.0f;
            ctx.weather.relativeHumidity = doc["current"]["relative_humidity_2m"] | 50;
            ctx.weather.currentWeatherCode = doc["current"]["weather_code"] | 0;
            ctx.weather.windSpeedKmh = doc["current"]["wind_speed_10m"] | 10.0f;
            ctx.weather.lastUpdateEpoch = millis() / 1000;

            // Hourly 24h
            JsonArray temps = doc["hourly"]["temperature_2m"];
            JsonArray probs = doc["hourly"]["precipitation_probability"];
            JsonArray codes = doc["hourly"]["weather_code"];

            size_t count = min((size_t)24, temps.size());
            for (size_t i = 0; i < count; i++) {
                ctx.weather.hourly[i].hour = (uint8_t)i;
                ctx.weather.hourly[i].tempC = (int8_t)round(temps[i].as<float>());
                ctx.weather.hourly[i].rainProbPercent = probs[i] | 0;
                ctx.weather.hourly[i].weatherCode = codes[i] | 0;
            }

            state.unlock();
            Serial.printf("[WEATHER] Updated: %.1f C, Humidity: %d%%\n", 
                          ctx.weather.temperatureC, ctx.weather.relativeHumidity);
        }
    }
    http.end();
    _isFetching = false;
}

void WeatherService::update(bool wifiConnected, bool gnssValid, double lat, double lon) {
    if (!wifiConnected || !gnssValid || _isFetching) return;

    unsigned long now = millis();
    bool needRefresh = false;

    if (_lastFetchTime == 0 || (now - _lastFetchTime > 1800000)) { // 30 minutes
        needRefresh = true;
    } else {
        // Distance check: > 20km
        double dLat = (lat - _lastLat) * 111.0;
        double dLon = (lon - _lastLon) * 111.0 * cos(lat * 0.01745329);
        double distKm = sqrt(dLat * dLat + dLon * dLon);
        if (distKm > 20.0) needRefresh = true;
    }

    if (needRefresh) {
        _lastFetchTime = now;
        _lastLat = lat;
        _lastLon = lon;
        fetchWeather(lat, lon);
    }
}
