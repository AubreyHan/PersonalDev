#include "service/TimeService.h"
#include "service/TrackLogger.h"
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>

static time_t getBuildEpoch() {
    // Parse compile-time timestamp, e.g. "Nov  3 2026", "19:45:00"
    const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char monthStr[4] = {0};
    int day = 1, year = 2026, hour = 0, min = 0, sec = 0;
    sscanf(__DATE__, "%3s %d %d", monthStr, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &hour, &min, &sec);
    int month = 0;
    for (int i = 0; i < 12; i++) {
        if (strncmp(monthStr, months[i], 3) == 0) {
            month = i;
            break;
        }
    }
    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon  = month;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = min;
    t.tm_sec  = sec;
    t.tm_isdst = -1;
    time_t ep = mktime(&t);
    return (ep > 1700000000) ? ep : 1793342128;
}

void TimeService::init(RtcDriver* rtc, GnssDriver* gnss) {
    _rtc  = rtc;
    _gnss = gnss;

    // 1. Read hardware RX8130CE RTC at boot and set ESP32 system clock
    bool timeLoaded = false;
    if (_rtc) {
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
            timeLoaded = true;
            Serial.printf("[TIME] Boot clock loaded from RX8130CE RTC: %04u-%02u-%02u %02u:%02u:%02u UTC\n",
                          dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
        }
    }

    // 2. Fallback check if system clock already has valid time
    time_t curSec = time(nullptr);
    struct tm curTm;
    gmtime_r(&curSec, &curTm);
    if (!timeLoaded && curTm.tm_year + 1900 >= 2024) {
        timeLoaded = true;
        Serial.printf("[TIME] System clock active: %04u-%02u-%02u %02u:%02u:%02u UTC\n",
                      curTm.tm_year + 1900, curTm.tm_mon + 1, curTm.tm_mday,
                      curTm.tm_hour, curTm.tm_min, curTm.tm_sec);
    }

    // 3. If RTC and system clock were uninitialized (< 2024), seed from NVS or Build Time!
    if (!timeLoaded) {
        Preferences prefs;
        time_t nvsEpoch = 0;
        if (prefs.begin("m5track", true)) {
            nvsEpoch = (time_t)prefs.getULong64("last_epoch", 0);
            prefs.end();
        }
        time_t buildEpoch = getBuildEpoch();
        time_t seedEpoch  = (nvsEpoch > buildEpoch) ? nvsEpoch : buildEpoch;
        if (seedEpoch < 1700000000) {
            seedEpoch = 1793342128;
        }

        struct timeval tv = { .tv_sec = seedEpoch, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
        timeLoaded = true;

        gmtime_r(&seedEpoch, &curTm);
        RtcDateTime initDt = {
            (uint16_t)(curTm.tm_year + 1900),
            (uint8_t)(curTm.tm_mon + 1),
            (uint8_t)curTm.tm_mday,
            (uint8_t)curTm.tm_hour,
            (uint8_t)curTm.tm_min,
            (uint8_t)curTm.tm_sec
        };
        if (_rtc) {
            _rtc->writeTime(initDt);
        }
        Serial.printf("[TIME] Boot clock initialized from %s: %04u-%02u-%02u %02u:%02u:%02u UTC\n",
                      (nvsEpoch > buildEpoch) ? "NVS" : "Build Time",
                      initDt.year, initDt.month, initDt.day, initDt.hour, initDt.minute, initDt.second);
    }

    // 4. Immediately seed ctx.gnss with device's current UTC time!
    curSec = time(nullptr);
    gmtime_r(&curSec, &curTm);
    {
        SystemState& state = SystemState::instance();
        state.lock();
        auto& gnssData = state.getContextUnsafe().gnss;
        gnssData.year   = curTm.tm_year + 1900;
        gnssData.month  = curTm.tm_mon + 1;
        gnssData.day    = curTm.tm_mday;
        gnssData.hour   = curTm.tm_hour;
        gnssData.minute = curTm.tm_min;
        gnssData.second = curTm.tm_sec;
        state.unlock();
    }

    // 5. Inject Warm-Start Time + Last NVS Position into NEO-M9N immediately
    injectWarmStartAssistance();

    // 3. Initialize GPS Position-Derived Timezone from NVS
    Preferences prefs;
    if (prefs.begin("m5track", true)) {
        int16_t savedTz   = prefs.getShort("tz_offset_min", 480);
        int32_t lastLonE7 = prefs.getInt("last_lon_e7", 0);
        int32_t lastLatE7 = prefs.getInt("last_lat_e7", 0);
        prefs.end();

        if (lastLonE7 != 0) {
            double lastLon = lastLonE7 / 1e7;
            double lastLat = lastLatE7 / 1e7;
            int tzHours = (int)round(lastLon / 15.0);
            savedTz = tzHours * 60;
            // China national standard time (UTC+8) within China mainland borders
            if (lastLon >= 73.0 && lastLon <= 135.0 && lastLat >= 18.0 && lastLat <= 54.0) {
                savedTz = 480;
            }
        }
        SystemState& state = SystemState::instance();
        state.lock();
        state.getContextUnsafe().clocks[0].tzOffsetMinutes = savedTz;
        state.unlock();
        Serial.printf("[TIME] Boot clock timezone seeded from GPS NVS: %+03d:%02d\n",
                      savedTz / 60, abs(savedTz % 60));
    }
}

void TimeService::injectWarmStartAssistance() {
    if (!_gnss || !_gnss->isOnline()) return;

    // Inject UTC time from RTC or system clock (1s accuracy = 1,000,000,000 ns)
    time_t nowSec = time(nullptr);
    struct tm utcTm;
    gmtime_r(&nowSec, &utcTm);
    if (utcTm.tm_year + 1900 >= 2024) {
        _gnss->injectUtcTime(utcTm.tm_year + 1900, utcTm.tm_mon + 1, utcTm.tm_mday,
                             utcTm.tm_hour, utcTm.tm_min, utcTm.tm_sec, 1000000000);
        Serial.printf("[TIME] Injected System UTC Time to NEO-M9N: %04u-%02u-%02u %02u:%02u:%02u UTC\n",
                      utcTm.tm_year + 1900, utcTm.tm_mon + 1, utcTm.tm_mday,
                      utcTm.tm_hour, utcTm.tm_min, utcTm.tm_sec);
    } else if (_rtc) {
        RtcDateTime dt;
        if (_rtc->readTime(dt) && dt.year >= 2024) {
            _gnss->injectUtcTime(dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second, 1000000000);
            Serial.printf("[TIME] Injected RTC Time to NEO-M9N: %04u-%02u-%02u %02u:%02u:%02u UTC\n",
                          dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
        }
    }

    // Only inject position from NVS if recorded within the last 2 hours to avoid poisoning Doppler search
    Preferences prefs;
    if (prefs.begin("m5track", true)) {
        int32_t lastLatE7 = prefs.getInt("last_lat_e7", 0);
        int32_t lastLonE7 = prefs.getInt("last_lon_e7", 0);
        int32_t lastAltCm = prefs.getInt("last_alt_cm", 0);
        uint32_t lastEpoch = prefs.getUInt("last_fix_epoch", 0);
        prefs.end();

        if (lastLatE7 != 0 && lastLonE7 != 0 && lastEpoch > 0) {
            uint32_t curEpoch = (uint32_t)nowSec;
            if (curEpoch >= lastEpoch && (curEpoch - lastEpoch) < 7200) { // within 2 hours
                _gnss->injectPosition(lastLatE7, lastLonE7, lastAltCm, 100000); // 1km tolerance
                Serial.printf("[TIME] Injected Recent (<2h) Position: LatE7=%d, LonE7=%d, AltCm=%d\n",
                              lastLatE7, lastLonE7, lastAltCm);
            } else {
                Serial.println("[TIME] Stored NVS position is stale (>2h); skipping position injection for full autonomous lock.");
            }
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
        prefs.putUInt("last_fix_epoch", (uint32_t)time(nullptr));
        prefs.end();
    }
}

bool TimeService::syncFromNtpWhenNoGps() {
    // Requirement 1: When there is no GPS signal, but Wi-Fi is connected, sync time via NTP
    configTime(0, 0, "ntp.aliyun.com", "cn.pool.ntp.org", "pool.ntp.org");
    struct tm timeinfo;
    // Fast 80ms check: SNTP runs asynchronously in LwIP background daemon
    if (getLocalTime(&timeinfo, 80) && (timeinfo.tm_year + 1900 >= 2024)) {
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
        char ntpSyncMsg[80];
        snprintf(ntpSyncMsg, sizeof(ntpSyncMsg), "NTP calibrated -> %04u-%02u-%02u %02u:%02u:%02u UTC",
                 dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
        TrackLogger::instance().logPowerEvent("TIME_SYNC_NTP", ntpSyncMsg);
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

    // Drive ctx.gnss UTC clock from device system/RTC time whenever GPS is searching or year < 2024
    time_t curSec = time(nullptr);
    struct tm curTm;
    gmtime_r(&curSec, &curTm);
    if (!gnssValid || gYear < 2024) {
        if (curTm.tm_year + 1900 >= 2024) {
            state.lock();
            auto& g = state.getContextUnsafe().gnss;
            if (!g.isValid || g.year < 2024) {
                g.year   = curTm.tm_year + 1900;
                g.month  = curTm.tm_mon + 1;
                g.day    = curTm.tm_mday;
                g.hour   = curTm.tm_hour;
                g.minute = curTm.tm_min;
                g.second = curTm.tm_sec;
                struct timeval tv;
                gettimeofday(&tv, nullptr);
                g.millisecond = (tv.tv_usec / 1000) % 1000;
            }
            state.unlock();
        }
    }

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

            char gnssSyncMsg[80];
            snprintf(gnssSyncMsg, sizeof(gnssSyncMsg), "GNSS 3D Fix calibrated -> %04u-%02u-%02u %02u:%02u:%02u UTC",
                     dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
            TrackLogger::instance().logPowerEvent("TIME_SYNC_GNSS", gnssSyncMsg);
        }

        // Dynamically update GPS position-derived timezone for slot 0 whenever longitude changes
        state.lock();
        auto& ctx = state.getContextUnsafe();
        if (ctx.clocks[0].useGpsLongitude && fabsf((float)gLon) > 1.0f) {
            int tzHours = (int)round(gLon / 15.0);
            int16_t targetTzMin = tzHours * 60;
            if (gLon >= 73.0 && gLon <= 135.0 && (gLatE7 / 1e7) >= 18.0 && (gLatE7 / 1e7) <= 54.0) {
                targetTzMin = 480; // China Standard Time (UTC+8)
            }
            if (ctx.clocks[0].tzOffsetMinutes != targetTzMin) {
                ctx.clocks[0].tzOffsetMinutes = targetTzMin;
                Preferences prefs;
                if (prefs.begin("m5track", false)) {
                    prefs.putShort("tz_offset_min", targetTzMin);
                    prefs.end();
                }
                Serial.printf("[TIME] Updated GPS local timezone: %+03d:%02d (Lon: %.4f)\n",
                              targetTzMin / 60, abs(targetTzMin % 60), gLon);
            }
        }
        state.unlock();

        // Save valid 3D coordinates to NVS only when vehicle moves >= 200m or every 5 min
        if (now - _lastNvsPosSaveMs >= 300000 ||
            (_lastNvsPosSaveMs == 0) ||
            (abs(gLatE7 - _lastSavedLatE7) > 1800 || abs(gLonE7 - _lastSavedLonE7) > 1800)) {
            _lastNvsPosSaveMs = now;
            _lastSavedLatE7   = gLatE7;
            _lastSavedLonE7   = gLonE7;
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

    // 3. Check 00:00 midnight daily rollover in local GPS timezone
    uint16_t lYr; uint8_t lMo, lDy, lH, lM, lS, lWd;
    state.lock();
    int16_t tzMin = state.getContextUnsafe().clocks[0].tzOffsetMinutes;
    bool autoDst  = state.getContextUnsafe().clocks[0].autoDst;
    state.unlock();
    getLocalTimeParts(tzMin, autoDst, lYr, lMo, lDy, lH, lM, lS, lWd);
    uint32_t currentLocalDateInt = (uint32_t)lYr * 10000 + (uint32_t)lMo * 100 + (uint32_t)lDy;
    state.lock();
    uint32_t lastDay = state.getContextUnsafe().lastLocalDateInt;
    state.unlock();
    if (lastDay > 0 && currentLocalDateInt != lastDay) {
        TrackLogger::instance().recordTripSummary(TRIP_DAILY);
    }
    state.checkDailyMidnightRollover(currentLocalDateInt);

    // 4. Save persistent Trip Stats (Daily & Manual) every 5 minutes
    if (now - _lastTripSaveMs >= 300000 || _lastTripSaveMs == 0) {
        _lastTripSaveMs = now;
        state.saveTripStatsToNvs();
    }

    // 5. Save persistent last valid epoch every 30 seconds to survive power cuts
    if (curSec >= 1700000000 && (now - _lastEpochSaveMs >= 30000 || _lastEpochSaveMs == 0)) {
        _lastEpochSaveMs = now;
        Preferences prefs;
        if (prefs.begin("m5track", false)) {
            prefs.putULong64("last_epoch", (uint64_t)curSec);
            prefs.end();
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
        utcSec = getBuildEpoch() + (millis() / 1000);
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
