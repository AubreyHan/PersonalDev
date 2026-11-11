#include "WeatherService.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace {
constexpr const char* WEATHER_CACHE_REL_PATH = "/weather_cache.bin";
constexpr uint32_t    WEATHER_CACHE_MAGIC    = 0x4D355758UL; // 'M5WX'
constexpr uint16_t    WEATHER_CACHE_VERSION  = 1U;

struct WeatherCacheDiskRecord {
    uint32_t    magic;
    uint16_t    version;
    uint16_t    reserved;
    WeatherData payload;
};

int parseTwoDigits(const char* p) {
    if (p == nullptr || p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') {
        return -1;
    }
    return (p[0] - '0') * 10 + (p[1] - '0');
}
} // namespace

WeatherService& WeatherService::instance() {
    static WeatherService s_instance;
    return s_instance;
}

WeatherService::WeatherService()
    : _initialized(false),
      _fetchedWithLiveGnss(false),
      _lastFetchMillis(0),
      _lastAttemptMillis(0) {}

bool WeatherService::begin() {
    if (_initialized) {
        return true;
    }
    loadOfflineCache();
    _initialized = true;
    return true;
}

void WeatherService::update() {
    if (!_initialized) {
        begin();
    }

    bool   manualRefresh = false;
    bool   hasValidData  = false;
    bool   fromCache     = false;
    double prevLat       = 0.0;
    double prevLon       = 0.0;

    {
        StateGuard lock(SystemState::instance());
        auto& st = SystemState::instance().data();
        if (st.weather.cmdRefreshWeather) {
            manualRefresh = true;
            st.weather.cmdRefreshWeather = false;
        }
        hasValidData = st.weather.hasValidData;
        fromCache    = st.weather.isFromOfflineCache;
        prevLat      = st.weather.fetchedLatitude;
        prevLon      = st.weather.fetchedLongitude;
    }

    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    double queryLat = 0.0;
    double queryLon = 0.0;
    bool   isLive   = false;
    if (!resolveQueryCoordinates(queryLat, queryLon, isLive)) {
        return;
    }

    const uint32_t nowMs = millis();
    if (!manualRefresh && _lastAttemptMillis != 0 && (nowMs - _lastAttemptMillis) < RETRY_BACKOFF_MS) {
        return;
    }

    bool shouldFetch = manualRefresh || !hasValidData || fromCache || (_lastFetchMillis == 0);

    // Periodic 15-minute refresh
    if (!shouldFetch && (nowMs - _lastFetchMillis) >= CloudConfig::WEATHER_UPDATE_MS) {
        shouldFetch = true;
    }

    // Upgrade from NVS coarse coords once real 3D GNSS fix arrives (>= 3 km delta)
    if (!shouldFetch && isLive && !_fetchedWithLiveGnss) {
        const float distKm = haversineDistanceKm(prevLat, prevLon, queryLat, queryLon);
        if (distKm >= 3.0f || (nowMs - _lastFetchMillis) >= 60000UL) {
            shouldFetch = true;
        }
    }

    // Displacement threshold auto-refresh (>= 15 km moved with 3 min minimum spacing)
    if (!shouldFetch && isLive && (nowMs - _lastFetchMillis) >= DISPLACEMENT_MIN_COOLDOWN_MS) {
        const float movedKm = haversineDistanceKm(prevLat, prevLon, queryLat, queryLon);
        if (movedKm >= DISPLACEMENT_REFRESH_KM) {
            shouldFetch = true;
        }
    }

    if (shouldFetch) {
        if (fetchNow(queryLat, queryLon)) {
            _fetchedWithLiveGnss = isLive;
        }
    }
}

String WeatherService::buildOpenMeteoUrl(double latitude, double longitude) {
    char coordBuf[64] = {};
    snprintf(coordBuf, sizeof(coordBuf), "latitude=%.4f&longitude=%.4f", latitude, longitude);

    String url;
    url.reserve(320);
    url += CloudConfig::OPEN_METEO_ENDPOINT;
    url += "?";
    url += coordBuf;
    url += "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
           "is_day,weather_code,wind_speed_10m,wind_direction_10m"
           "&hourly=temperature_2m,precipitation_probability,weather_code"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
           "&timezone=auto&forecast_days=2";
    return url;
}

bool WeatherService::fetchNow(double latitude, double longitude) {
    _lastAttemptMillis = millis();

    if (WiFi.status() != WL_CONNECTED ||
        !isfinite(latitude) || !isfinite(longitude) ||
        (fabs(latitude) < 1e-5 && fabs(longitude) < 1e-5)) {
        return false;
    }

    {
        StateGuard lock(SystemState::instance());
        SystemState::instance().data().weather.isFetching = true;
    }

    const String url = buildOpenMeteoUrl(latitude, longitude);

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTP_TIMEOUT_MS / 1000U);

    HTTPClient http;
    http.setConnectTimeout(7000);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    // Request plain uncompressed JSON so ArduinoJson can parse directly
    http.setAcceptEncoding("identity");

    bool ok = false;
    String payload;
    if (http.begin(client, url)) {
        const int code = http.GET();
        if (code == HTTP_CODE_OK) {
            payload = http.getString();
            ok = (payload.length() > 32);
        } else {
            Serial.printf("[WEATHER] Open-Meteo HTTP %d\n", code);
        }
        http.end();
    }

    if (!ok) {
        StateGuard lock(SystemState::instance());
        SystemState::instance().data().weather.isFetching = false;
        return false;
    }

    WeatherData parsed = {};
    if (!parseForecastJson(payload, latitude, longitude, parsed)) {
        StateGuard lock(SystemState::instance());
        SystemState::instance().data().weather.isFetching = false;
        return false;
    }

    {
        StateGuard lock(SystemState::instance());
        auto& st = SystemState::instance().data();
        parsed.cmdRefreshWeather = false;
        st.weather               = parsed;
    }

    saveOfflineCache(parsed);
    _lastFetchMillis = millis();
    Serial.printf("[WEATHER] Updated (%.4f, %.4f): %.1fC, WMO=%u, hazard=%d\n",
                  latitude, longitude,
                  static_cast<double>(parsed.currentTempC),
                  static_cast<unsigned>(parsed.currentWmoCode),
                  parsed.hasHazardWarning ? 1 : 0);
    return true;
}

bool WeatherService::parseForecastJson(const String& jsonPayload,
                                       double lat, double lon,
                                       WeatherData& outWeather) const {
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, jsonPayload);
    if (err) {
        Serial.printf("[WEATHER] JSON parse error: %s\n", err.c_str());
        return false;
    }

    JsonObjectConst current = doc["current"].as<JsonObjectConst>();
    JsonObjectConst hourly  = doc["hourly"].as<JsonObjectConst>();
    if (current.isNull() || hourly.isNull()) {
        return false;
    }

    outWeather = WeatherData{};
    outWeather.hasValidData       = true;
    outWeather.isFromOfflineCache = false;
    outWeather.isFetching         = false;
    outWeather.fetchedLatitude    = lat;
    outWeather.fetchedLongitude   = lon;

    // Timestamp
    uint32_t nowUtc = 0;
    {
        StateGuard lock(SystemState::instance());
        nowUtc = SystemState::instance().data().time.epochUtc;
    }
    if (nowUtc < 1700000000UL) {
        const time_t sysNow = time(nullptr);
        if (sysNow > 1700000000) {
            nowUtc = static_cast<uint32_t>(sysNow);
        }
    }
    outWeather.lastFetchUtc = nowUtc;

    // 1. Current Real-Time Weather
    outWeather.currentTempC   = current["temperature_2m"] | 20.0f;
    outWeather.apparentTempC  = current["apparent_temperature"] | outWeather.currentTempC;
    const int hum             = current["relative_humidity_2m"] | 50;
    outWeather.humidityPct    = static_cast<uint8_t>((hum < 0) ? 0 : ((hum > 100) ? 100 : hum));
    outWeather.isDay          = ((current["is_day"] | 1) != 0);
    outWeather.currentWmoCode = static_cast<uint8_t>(current["weather_code"] | 0);
    outWeather.windSpeedKmh   = current["wind_speed_10m"] | 0.0f;
    const int windDir         = current["wind_direction_10m"] | 0;
    outWeather.windDirectionDeg = static_cast<uint16_t>(((windDir % 360) + 360) % 360);

    // 2. Align Hourly Forecast to Current Local Whole Hour ("YYYY-MM-DDTHH")
    const char* currentIsoTime = current["time"] | "";
    JsonArrayConst hTimes = hourly["time"].as<JsonArrayConst>();
    JsonArrayConst hTemps = hourly["temperature_2m"].as<JsonArrayConst>();
    JsonArrayConst hProbs = hourly["precipitation_probability"].as<JsonArrayConst>();
    JsonArrayConst hCodes = hourly["weather_code"].as<JsonArrayConst>();

    const size_t totalHours = hTimes.size();
    if (totalHours == 0) {
        return false;
    }

    size_t startIdx = 0;
    bool   matched  = false;
    if (strlen(currentIsoTime) >= 13) {
        for (size_t i = 0; i < totalHours; ++i) {
            const char* ht = hTimes[i] | "";
            if (strncmp(ht, currentIsoTime, 13) == 0) {
                startIdx = i;
                matched  = true;
                break;
            }
        }
    }
    if (!matched) {
        uint8_t fallbackHour = 0;
        {
            StateGuard lock(SystemState::instance());
            fallbackHour = SystemState::instance().data().time.localHour;
        }
        if (fallbackHour < totalHours) {
            startIdx = fallbackHour;
        }
    }

    if (totalHours >= WeatherData::HOURLY_SLOTS &&
        (startIdx + WeatherData::HOURLY_SLOTS) > totalHours) {
        startIdx = totalHours - WeatherData::HOURLY_SLOTS;
    }

    for (size_t k = 0; k < WeatherData::HOURLY_SLOTS; ++k) {
        const size_t srcIdx = ((startIdx + k) < totalHours) ? (startIdx + k) : (totalHours - 1);
        HourlyWeatherItem& item = outWeather.hourly[k];

        const char* tStr = hTimes[srcIdx] | "";
        int parsedHr = -1;
        if (strlen(tStr) >= 13) {
            parsedHr = parseTwoDigits(tStr + 11);
        }
        item.hour24 = static_cast<uint8_t>((parsedHr >= 0 && parsedHr < 24) ? parsedHr : (k % 24));
        item.tempC  = (srcIdx < hTemps.size()) ? (hTemps[srcIdx] | outWeather.currentTempC) : outWeather.currentTempC;

        const int prob = (srcIdx < hProbs.size()) ? (hProbs[srcIdx] | 0) : 0;
        item.precipProbPct = static_cast<uint8_t>((prob < 0) ? 0 : ((prob > 100) ? 100 : prob));

        const int code = (srcIdx < hCodes.size()) ? (hCodes[srcIdx] | 0) : 0;
        item.wmoCode = static_cast<uint8_t>((code < 0) ? 0 : ((code > 99) ? 99 : code));
        item.isDay   = (item.hour24 >= 6 && item.hour24 < 18);
    }

    // 3. Parse Daily Summary (daily[0..2])
    JsonObjectConst daily = doc["daily"].as<JsonObjectConst>();
    if (!daily.isNull()) {
        JsonArrayConst dTimes = daily["time"].as<JsonArrayConst>();
        JsonArrayConst dCodes = daily["weather_code"].as<JsonArrayConst>();
        JsonArrayConst dMax   = daily["temperature_2m_max"].as<JsonArrayConst>();
        JsonArrayConst dMin   = daily["temperature_2m_min"].as<JsonArrayConst>();
        JsonArrayConst dProb  = daily["precipitation_probability_max"].as<JsonArrayConst>();

        const size_t dCount = dTimes.size();
        for (size_t i = 0; i < WeatherData::DAILY_SLOTS && dCount > 0; ++i) {
            const size_t src = (i < dCount) ? i : (dCount - 1);
            DailyWeatherItem& dItem = outWeather.daily[i];

            const char* dt = dTimes[src] | "";
            if (strlen(dt) >= 10) {
                const int m = parseTwoDigits(dt + 5);
                const int d = parseTwoDigits(dt + 8);
                dItem.month = static_cast<uint8_t>((m >= 1 && m <= 12) ? m : 1);
                dItem.day   = static_cast<uint8_t>((d >= 1 && d <= 31) ? d : 1);
            }
            dItem.wmoCode  = static_cast<uint8_t>((src < dCodes.size()) ? (dCodes[src] | 0) : 0);
            dItem.tempMaxC = (src < dMax.size()) ? (dMax[src] | outWeather.currentTempC) : outWeather.currentTempC;
            dItem.tempMinC = (src < dMin.size()) ? (dMin[src] | outWeather.currentTempC) : outWeather.currentTempC;
            const int mp   = (src < dProb.size()) ? (dProb[src] | 0) : 0;
            dItem.precipProbMaxPct = static_cast<uint8_t>((mp < 0) ? 0 : ((mp > 100) ? 100 : mp));
        }
    }

    // 4. Evaluate High-Risk Driving Weather Hazards
    evaluateDrivingHazard(outWeather);
    return true;
}

const char* WeatherService::describeWmoCodeZh(uint8_t wmoCode) {
    switch (wmoCode) {
        case 0:  return "晴朗";
        case 1:  return "大部晴朗";
        case 2:  return "多云";
        case 3:  return "阴天";
        case 45: return "大雾";
        case 48: return "雾凇";
        case 51: return "小毛毛雨";
        case 53: return "毛毛雨";
        case 55: return "密毛毛雨";
        case 56:
        case 57: return "冻毛毛雨";
        case 61: return "小雨";
        case 63: return "中雨";
        case 65: return "暴雨";
        case 66:
        case 67: return "冻雨";
        case 71: return "小雪";
        case 73: return "中雪";
        case 75: return "暴雪";
        case 77: return "雪粒";
        case 80: return "小阵雨";
        case 81: return "强阵雨";
        case 82: return "特强阵雨";
        case 85: return "阵雪";
        case 86: return "强阵雪";
        case 95: return "雷阵雨";
        case 96:
        case 99: return "雷暴伴冰雹";
        default: return "多云";
    }
}

bool WeatherService::isSevereDrivingHazardWmo(uint8_t wmoCode) {
    switch (wmoCode) {
        case 45: // Fog
        case 48: // Depositing rime fog
        case 56: // Freezing drizzle
        case 57: // Dense freezing drizzle
        case 65: // Heavy rain
        case 66: // Light freezing rain
        case 67: // Heavy freezing rain
        case 75: // Heavy snow fall
        case 82: // Violent rain showers
        case 86: // Heavy snow showers
        case 95: // Thunderstorm
        case 96: // Thunderstorm with slight hail
        case 99: // Thunderstorm with heavy hail
            return true;
        default:
            return false;
    }
}

void WeatherService::evaluateDrivingHazard(WeatherData& wx) const {
    wx.hasHazardWarning = false;

    const uint8_t cur = wx.currentWmoCode;
    if (cur == 96 || cur == 99) {
        wx.hasHazardWarning = true;
        snprintf(wx.warningMessage, sizeof(wx.warningMessage),
                 "高危预警: 当前遭遇雷暴伴冰雹，请立即减速寻找安全区避险");
        return;
    }
    if (cur == 66 || cur == 67 || cur == 56 || cur == 57) {
        wx.hasHazardWarning = true;
        snprintf(wx.warningMessage, sizeof(wx.warningMessage),
                 "高危预警: 当前冻雨路面极易结暗冰，严禁急刹与急打方向");
        return;
    }
    if (cur == 65 || cur == 82) {
        wx.hasHazardWarning = true;
        snprintf(wx.warningMessage, sizeof(wx.warningMessage),
                 "高危预警: 当前暴雨倾盆，请开启雾灯并加倍保持安全车距");
        return;
    }
    if (cur == 75 || cur == 86) {
        wx.hasHazardWarning = true;
        snprintf(wx.warningMessage, sizeof(wx.warningMessage),
                 "高危预警: 当前暴雪积雪湿滑，请控速慢行谨防侧滑");
        return;
    }
    if (cur == 95) {
        wx.hasHazardWarning = true;
        snprintf(wx.warningMessage, sizeof(wx.warningMessage),
                 "行车预警: 当前雷阵雨强对流天气，注意横风与低能见度");
        return;
    }
    if (cur == 45 || cur == 48) {
        wx.hasHazardWarning = true;
        snprintf(wx.warningMessage, sizeof(wx.warningMessage),
                 "行车预警: 当前大雾低能见度，请开启前后雾灯减速慢行");
        return;
    }

    // Scan upcoming 12 hours for severe driving hazards first
    for (size_t h = 1; h < 12 && h < WeatherData::HOURLY_SLOTS; ++h) {
        const uint8_t hc = wx.hourly[h].wmoCode;
        if (isSevereDrivingHazardWmo(hc)) {
            wx.hasHazardWarning = true;
            snprintf(wx.warningMessage, sizeof(wx.warningMessage),
                     "高危预警: 预计%u小时后(%02u:00)遭遇%s，请提前规划行车",
                     static_cast<unsigned>(h),
                     static_cast<unsigned>(wx.hourly[h].hour24),
                     describeWmoCodeZh(hc));
            return;
        }
    }

    // Scan upcoming 12 hours for moderate/high-probability rain or snow
    for (size_t h = 1; h < 12 && h < WeatherData::HOURLY_SLOTS; ++h) {
        const uint8_t hc   = wx.hourly[h].wmoCode;
        const uint8_t prob = wx.hourly[h].precipProbPct;
        if (hc >= 61 || prob >= 70) {
            if (prob >= 75 || hc >= 63) {
                wx.hasHazardWarning = true;
            }
            snprintf(wx.warningMessage, sizeof(wx.warningMessage),
                     "预计%u小时后(%02u:00)迎面%s(概率%u%%)，请注意路面湿滑",
                     static_cast<unsigned>(h),
                     static_cast<unsigned>(wx.hourly[h].hour24),
                     describeWmoCodeZh(hc >= 51 ? hc : 61),
                     static_cast<unsigned>(prob));
            return;
        }
    }

    if (wx.windSpeedKmh >= 50.0f) {
        wx.hasHazardWarning = true;
        snprintf(wx.warningMessage, sizeof(wx.warningMessage),
                 "行车预警: 当前阵风达%.0fkm/h，桥梁与隧道口谨防横风偏移",
                 static_cast<double>(wx.windSpeedKmh));
        return;
    }

    snprintf(wx.warningMessage, sizeof(wx.warningMessage),
             "未来12小时沿途气象平稳，祝您行车安全");
}

bool WeatherService::resolveQueryCoordinates(double& outLat, double& outLon, bool& outIsLiveGnss) const {
    {
        StateGuard lock(SystemState::instance());
        const auto& st = SystemState::instance().data();
        if (st.gnss.isValid &&
            (fabs(st.gnss.latitude) > 1e-5 || fabs(st.gnss.longitude) > 1e-5)) {
            outLat        = st.gnss.latitude;
            outLon        = st.gnss.longitude;
            outIsLiveGnss = true;
            return true;
        }
    }

    Preferences prefs;
    if (!prefs.begin(NvsConfig::NAMESPACE, true)) {
        return false;
    }
    const int32_t latE7 = prefs.getInt(NvsConfig::KEY_LAST_LAT_E7, 0);
    const int32_t lonE7 = prefs.getInt(NvsConfig::KEY_LAST_LON_E7, 0);
    prefs.end();

    if (latE7 == 0 && lonE7 == 0) {
        return false;
    }

    const double lat = static_cast<double>(latE7) * 1e-7;
    const double lon = static_cast<double>(lonE7) * 1e-7;
    if (fabs(lat) > 90.0 || fabs(lon) > 180.0) {
        return false;
    }

    outLat        = lat;
    outLon        = lon;
    outIsLiveGnss = false;
    return true;
}

float WeatherService::haversineDistanceKm(double lat1, double lon1, double lat2, double lon2) {
    constexpr double kDegToRad = 0.017453292519943295;
    constexpr double kEarthKm  = 6371.0;

    const double dLat = (lat2 - lat1) * kDegToRad;
    const double dLon = (lon2 - lon1) * kDegToRad;
    const double rLat1 = lat1 * kDegToRad;
    const double rLat2 = lat2 * kDegToRad;

    const double sinLat = sin(dLat * 0.5);
    const double sinLon = sin(dLon * 0.5);
    double a = sinLat * sinLat + cos(rLat1) * cos(rLat2) * sinLon * sinLon;
    if (a < 0.0) a = 0.0;
    if (a > 1.0) a = 1.0;
    const double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return static_cast<float>(kEarthKm * c);
}

bool WeatherService::loadOfflineCache() {
    bool lfsReady = false;
    {
        StateGuard lock(SystemState::instance());
        lfsReady = SystemState::instance().data().boot.littlefsOk;
    }
    if (!lfsReady) {
        return false;
    }

    fs::File f = LittleFS.open(WEATHER_CACHE_REL_PATH, "r");
    if (!f) {
        return false;
    }
    WeatherCacheDiskRecord rec = {};
    const size_t n = f.readBytes(reinterpret_cast<char*>(&rec), sizeof(rec));
    f.close();

    if (n != sizeof(rec) ||
        rec.magic != WEATHER_CACHE_MAGIC ||
        rec.version != WEATHER_CACHE_VERSION ||
        !rec.payload.hasValidData) {
        return false;
    }

    rec.payload.isFromOfflineCache = true;
    rec.payload.isFetching         = false;
    rec.payload.cmdRefreshWeather  = false;
    rec.payload.warningMessage[sizeof(rec.payload.warningMessage) - 1] = '\0';

    {
        StateGuard lock(SystemState::instance());
        SystemState::instance().data().weather = rec.payload;
    }
    return true;
}

void WeatherService::saveOfflineCache(const WeatherData& wx) const {
    bool lfsReady = false;
    {
        StateGuard lock(SystemState::instance());
        lfsReady = SystemState::instance().data().boot.littlefsOk;
    }
    if (!lfsReady) {
        return;
    }

    WeatherCacheDiskRecord rec = {};
    rec.magic   = WEATHER_CACHE_MAGIC;
    rec.version = WEATHER_CACHE_VERSION;
    rec.payload = wx;
    rec.payload.isFromOfflineCache = true;
    rec.payload.isFetching         = false;
    rec.payload.cmdRefreshWeather  = false;

    fs::File f = LittleFS.open(WEATHER_CACHE_REL_PATH, "w");
    if (f) {
        f.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec));
        f.close();
    }
}
