#include "service/WeatherService.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

void WeatherService::init() {
    WeatherData cached;
    if (loadFromNvsCache(cached)) {
        SystemState& state = SystemState::instance();
        state.lock();
        cached.isOfflineCache = true;
        state.getContextUnsafe().weather = cached;
        state.unlock();
    }
}

void WeatherService::saveToNvsCache(const WeatherData& w) {
    Preferences prefs;
    if (prefs.begin("m5track", false)) {
        prefs.putBytes("weather_cache", &w, sizeof(WeatherData));
        prefs.end();
    }
}

bool WeatherService::loadFromNvsCache(WeatherData& outW) {
    Preferences prefs;
    bool ok = false;
    if (prefs.begin("m5track", true)) {
        if (prefs.getBytesLength("weather_cache") == sizeof(WeatherData)) {
            prefs.getBytes("weather_cache", &outW, sizeof(WeatherData));
            ok = outW.isValid;
        }
        prefs.end();
    }
    return ok;
}

bool WeatherService::fetchOpenMeteo(double lat, double lon) {
    char url[256];
    snprintf(url, sizeof(url),
             "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,wind_direction_10m"
             "&hourly=temperature_2m,precipitation_probability,weather_code&forecast_days=2",
             lat, lon);

    HTTPClient http;
    http.setTimeout(5000);
    if (!http.begin(url)) return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) return false;

    WeatherData w = {};
    w.isValid          = true;
    w.isOfflineCache   = false;
    w.currentTempC     = doc["current"]["temperature_2m"] | 24.5f;
    w.apparentTempC    = doc["current"]["apparent_temperature"] | 23.0f;
    w.humidityPercent  = doc["current"]["relative_humidity_2m"] | 52;
    w.windSpeedKmh     = doc["current"]["wind_speed_10m"] | 14.0f;
    w.windDirDeg       = doc["current"]["wind_direction_10m"] | 45;
    w.weatherCode      = doc["current"]["weather_code"] | 1;
    w.lastUpdateEpoch  = millis() / 1000;

    JsonArray temps = doc["hourly"]["temperature_2m"].as<JsonArray>();
    JsonArray probs = doc["hourly"]["precipitation_probability"].as<JsonArray>();
    JsonArray codes = doc["hourly"]["weather_code"].as<JsonArray>();

    int rainHourOffset = -1;
    uint8_t maxRainProb = 0;

    for (int i = 0; i < 24; i++) {
        w.hourly[i].hour        = i % 24;
        w.hourly[i].tempC       = (i < (int)temps.size()) ? (float)temps[i] : w.currentTempC;
        w.hourly[i].precipProb  = (i < (int)probs.size()) ? (uint8_t)((int)probs[i]) : 0;
        w.hourly[i].weatherCode = (i < (int)codes.size()) ? (uint8_t)((int)codes[i]) : 1;

        if (w.hourly[i].precipProb >= 60 && rainHourOffset < 0) {
            rainHourOffset = i;
            maxRainProb = w.hourly[i].precipProb;
        }
    }

    if (rainHourOffset >= 0) {
        snprintf(w.warningText, sizeof(w.warningText),
                 "Rain expected in %dh (Prob %u%%) - Wet road ahead, reduce speed",
                 rainHourOffset, maxRainProb);
    } else {
        strncpy(w.warningText, "Next 24h Clear/Stable - Optimal driving visibility", sizeof(w.warningText) - 1);
    }

    saveToNvsCache(w);

    SystemState& state = SystemState::instance();
    state.lock();
    state.getContextUnsafe().weather = w;
    state.unlock();
    return true;
}

void WeatherService::update() {
    uint32_t now = millis();
    uint32_t interval = _hasFetched ? 900000 : 20000; // 15 min after first fetch
    if (now - _lastFetchMs < interval && _lastFetchMs != 0) return;
    _lastFetchMs = now;

    SystemContext snap = SystemState::instance().getSnapshot();
    if (!snap.wifi.isConnected) return;

    double lat = snap.gnss.isValid ? snap.gnss.latitude  : 31.2304;
    double lon = snap.gnss.isValid ? snap.gnss.longitude : 121.4737;

    if (fetchOpenMeteo(lat, lon)) {
        _hasFetched = true;
    }
}
