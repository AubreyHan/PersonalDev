#pragma once

#include <Arduino.h>
#include "../config.h"
#include "../model/SystemState.h"

// =============================================================================
// WeatherService - Open-Meteo v1 Automotive Weather & Hazard Warning Engine
// Strictly compliant with Open-Meteo v1 Forecast REST Specification (#3 §3.3):
//   - Aligns hourly[24] to current local whole hour (Front 12h [0..11] / Back 12h [12..23])
//   - Populates daily[3] multi-day summary
//   - Detects severe driving hazards (hail, freezing rain, heavy rain/snow, fog, thunderstorm)
//   - Supports periodic 15-min refresh, >=15km displacement auto-refresh & LittleFS offline cache
// =============================================================================
class WeatherService {
public:
    static constexpr float    DISPLACEMENT_REFRESH_KM     = 15.0f;      // Auto-refresh after driving >= 15 km
    static constexpr uint32_t DISPLACEMENT_MIN_COOLDOWN_MS = 180000UL;  // Min 3 min between displacement fetches
    static constexpr uint32_t RETRY_BACKOFF_MS            = 45000UL;    // 45s retry backoff on HTTP failure
    static constexpr uint16_t HTTP_TIMEOUT_MS             = 10000U;     // 10s HTTPS timeout

    static WeatherService& instance();

    WeatherService();
    ~WeatherService() = default;

    WeatherService(const WeatherService&)            = delete;
    WeatherService& operator=(const WeatherService&) = delete;

    // Initialize service and load offline cached weather from LittleFS if available
    bool begin();

    // Non-blocking periodic tick (call from netTask on Core 0)
    void update();

    // Force immediate Open-Meteo fetch for given WGS-84 coordinates
    bool fetchNow(double latitude, double longitude);

    // Build exact Open-Meteo v1 forecast URL per #3 §3.3
    static String buildOpenMeteoUrl(double latitude, double longitude);

    // Helper: WMO weather code (0..99) to concise Chinese label & hazard classification
    static const char* describeWmoCodeZh(uint8_t wmoCode);
    static bool        isSevereDrivingHazardWmo(uint8_t wmoCode);

    // Great-circle Haversine distance in kilometers
    static float haversineDistanceKm(double lat1, double lon1, double lat2, double lon2);

private:
    bool resolveQueryCoordinates(double& outLat, double& outLon, bool& outIsLiveGnss) const;
    bool parseForecastJson(const String& jsonPayload, double lat, double lon, WeatherData& outWeather) const;
    void evaluateDrivingHazard(WeatherData& wx) const;
    bool loadOfflineCache();
    void saveOfflineCache(const WeatherData& wx) const;

    bool     _initialized;
    bool     _fetchedWithLiveGnss;
    uint32_t _lastFetchMillis;
    uint32_t _lastAttemptMillis;
};
