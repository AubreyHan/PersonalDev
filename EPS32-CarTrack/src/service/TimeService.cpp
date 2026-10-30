#include "service/TimeService.h"
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>

void TimeService::init(RtcDriver* rtc, GnssDriver* gnss) {
    _rtc  = rtc;
    _gnss = gnss;

    // 1. Read hardware RX8130CE RTC at boot and set ESP32 system clock
    if (_rtc && _rtc->isOnline()) {
        RtcDateTime dt;
        if (_rtc->readTime(dt) && dt.year >= 2024) {
            struct tm t = {};
            t.tm_year = dt.year - 1900;
            t.tm_mon  = dt.month - 1;
            t.tm_mday = dt.day;
            t.tm_hour = dt.hour;
            t.tm_min  = dt.minute;
            t.tm_sec  = dt.second;
            time_t epoch = mktime(&t);
            struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
            settimeofday(&tv, nullptr);
            Serial.printf("[TIME] Boot clock loaded from RX8130CE RTC: %04u-%02u-%02u %02u:%02u:%02u UTC\n",
                          dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
        }
    }

    // 2. Inject Warm-Start Time + Last NVS Position into NEO-M9N immediately
    injectWarmStartAssistance();
}

void TimeService::injectWarmStartAssistance() {
    if (!_gnss || !_gnss->isOnline()) return;

    // Inject UTC time from RTC or system clock
    time_t nowSec = time(nullptr);
    struct tm utcTm;
    gmtime_r(&nowSec, &utcTm);
    if (utcTm.tm_year + 1900 >= 2024) {
        _gnss->injectUtcTime(utcTm.tm_year + 1900, utcTm.tm_mon + 1, utcTm.tm_mday,
                             utcTm.tm_hour, utcTm.tm_min, utcTm.tm_sec, 50000000);
    } else if (_rtc && _rtc->isOnline()) {
        RtcDateTime dt;
        if (_rtc->readTime(dt) && dt.year >= 2024) {
            _gnss->injectUtcTime(dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second, 50000000);
        }
    }

    // Inject Last Known 3D Position from NVS (Fast garage/indoor exit warm-start)
    Preferences prefs;
    if (prefs.begin("m5track", true)) {
        int32_t lastLatE7 = prefs.getInt("last_lat_e7", 0);
        int32_t lastLonE7 = prefs.getInt("last_lon_e7", 0);
        int32_t lastAltCm = prefs.getInt("last_alt_cm", 0);
        prefs.end();

        if (lastLatE7 != 0 && lastLonE7 != 0) {
            _gnss->injectPosition(lastLatE7, lastLonE7, lastAltCm, 50000);
            Serial.printf("[TIME] Injected NVS Warm-Start Position: LatE7=%d, LonE7=%d, AltCm=%d\n",
                          lastLatE7, lastLonE7, lastAltCm);
        }
    }
}

void TimeService::saveLastPositionToNvs(int32_t latE7, int32_t lonE7, int32_t altCm) {
    if (latE7 == 0 && lonE7 == 0) return;
    Preferences prefs;
    if (prefs.begin("m5track", false)) {
        prefs.putInt("last_lat_e7", latE7);
        prefs.putInt("last_lon_e7", lonE7);
        prefs.putInt("last_alt_cm", altCm);
        prefs.end();
    }
}

bool TimeService::syncFromNtpWhenNoGps() {
    // Requirement 1: When there is no GPS signal, but Wi-Fi is connected, sync time via NTP
    configTime(0, 0, "ntp.aliyun.com", "cn.pool.ntp.org", "pool.ntp.org");
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 2500) && (timeinfo.tm_year + 1900 >= 2024)) {
        RtcDateTime dt;
        dt.year   = timeinfo.tm_year + 1900;
        dt.month  = timeinfo.tm_mon + 1;
        dt.day    = timeinfo.tm_mday;
        dt.hour   = timeinfo.tm_hour;
        dt.minute = timeinfo.tm_min;
        dt.second = timeinfo.tm_sec;

        if (_rtc) {
            _rtc->writeTime(dt);
        }
        if (_gnss && _gnss->isOnline()) {
            _gnss->injectUtcTime(dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second, 30000000);
        }
        _ntpSynced = true;
        Serial.printf("[TIME] Wi-Fi NTP Synced (No GPS) -> %04u-%02u-%02u %02u:%02u:%02u UTC (RTC & M9N Updated)\n",
                      dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
        return true;
    }
    return false;
}

void TimeService::update() {
    SystemState& state = SystemState::instance();
    state.lock();
    bool     gnssValid = state.getContextUnsafe().gnss.isValid;
    uint16_t gYear     = state.getContextUnsafe().gnss.year;
    uint8_t  gMonth    = state.getContextUnsafe().gnss.month;
    uint8_t  gDay      = state.getContextUnsafe().gnss.day;
    uint8_t  gHour     = state.getContextUnsafe().gnss.hour;
    uint8_t  gMin      = state.getContextUnsafe().gnss.minute;
    uint8_t  gSec      = state.getContextUnsafe().gnss.second;
    int32_t  gLatE7    = state.getContextUnsafe().gnss.latE7;
    int32_t  gLonE7    = state.getContextUnsafe().gnss.lonE7;
    double   gLon      = state.getContextUnsafe().gnss.longitude;
    float    gAltM     = state.getContextUnsafe().gnss.altitudeMeters;
    bool     wifiConn  = state.getContextUnsafe().wifi.isConnected;
    state.unlock();

    uint32_t now = millis();

    // Check if GnssDriver requested re-injecting time & position after 45s hot-start reset
    if (_gnss && _gnss->consumeReinjectRequest()) {
        injectWarmStartAssistance();
    }

    // 1. If GPS has valid 3D Fix and valid year, calibrate hardware RTC & system clock
    if (gnssValid && gYear >= 2024) {
        if (!_gpsCalibratedRtc) {
            _gpsCalibratedRtc = true;
            RtcDateTime dt = { gYear, gMonth, gDay, gHour, gMin, gSec };
            if (_rtc) {
                _rtc->writeTime(dt);
            }
            struct tm t = {};
            t.tm_year = dt.year - 1900;
            t.tm_mon  = dt.month - 1;
            t.tm_mday = dt.day;
            t.tm_hour = dt.hour;
            t.tm_min  = dt.minute;
            t.tm_sec  = dt.second;
            time_t epoch = mktime(&t);
            struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
            settimeofday(&tv, nullptr);

            // Auto calculate longitude timezone for slot 0 if useGpsLongitude enabled
            state.lock();
            auto& ctx = state.getContextUnsafe();
            if (ctx.clocks[0].useGpsLongitude && fabsf((float)gLon) > 1.0f) {
                int tzHours = (int)round(gLon / 15.0);
                ctx.clocks[0].tzOffsetMinutes = tzHours * 60;
            }
            state.unlock();
        }

        // Periodically save valid 3D coordinates to NVS (every 60s) for garage warm-start
        if (now - _lastNvsPosSaveMs >= 60000) {
            _lastNvsPosSaveMs = now;
            saveLastPositionToNvs(gLatE7, gLonE7, (int32_t)(gAltM * 100.0f));
        }
    } else if (!gnssValid && wifiConn) {
        // 2. No GPS fix, but Wi-Fi is connected -> sync via NTP
        uint32_t retryInterval = _ntpSynced ? 600000 : 15000;
        if (now - _lastNtpAttemptMs >= retryInterval || _lastNtpAttemptMs == 0) {
            _lastNtpAttemptMs = now;
            syncFromNtpWhenNoGps();
        }
    }
}

// Calculate exact US/EU Daylight Saving Time (DST)
// US DST: Second Sunday in March to First Sunday in November at 02:00
static bool isUsDst(int year, int month, int day, int hour) {
    if (month < 3 || month > 11) return false;
    if (month > 3 && month < 11) return true;

    // Helper: day of week (0 = Sunday, 1 = Monday, ...) via Zeller's-like formula
    auto getDOW = [](int y, int m, int d) -> int {
        struct tm t = {};
        t.tm_year = y - 1900;
        t.tm_mon  = m - 1;
        t.tm_mday = d;
        mktime(&t);
        return t.tm_wday;
    };

    if (month == 3) {
        // Second Sunday in March: falls between 8th and 14th
        int secondSun = 8 + (7 - getDOW(year, 3, 8)) % 7;
        if (day > secondSun) return true;
        if (day == secondSun) return (hour >= 2);
        return false;
    }
    if (month == 11) {
        // First Sunday in November: falls between 1st and 7th
        int firstSun = 1 + (7 - getDOW(year, 11, 1)) % 7;
        if (day < firstSun) return true;
        if (day == firstSun) return (hour < 2);
        return false;
    }
    return false;
}

void TimeService::getLocalTimeParts(int16_t tzOffsetMin, bool autoDst, uint16_t& year, uint8_t& month, uint8_t& day, uint8_t& hour, uint8_t& minute, uint8_t& second, uint8_t& weekday) {
    time_t utcSec = time(nullptr);
    if (utcSec < 1700000000) {
        // Fallback default timestamp (2026-10-30 06:35:28 UTC -> 14:35:28 UTC+8)
        utcSec = 1793342128 + (millis() / 1000);
    }

    struct tm utcTm;
    gmtime_r(&utcSec, &utcTm);

    int16_t effectiveOffsetMin = tzOffsetMin;
    if (autoDst) {
        if (isUsDst(utcTm.tm_year + 1900, utcTm.tm_mon + 1, utcTm.tm_mday, utcTm.tm_hour)) {
            effectiveOffsetMin += 60;
        }
    }

    time_t localSec = utcSec + (effectiveOffsetMin * 60);
    struct tm lt;
    gmtime_r(&localSec, &lt);

    year    = lt.tm_year + 1900;
    month   = lt.tm_mon + 1;
    day     = lt.tm_mday;
    hour    = lt.tm_hour;
    minute  = lt.tm_min;
    second  = lt.tm_sec;
    weekday = lt.tm_wday;
}
