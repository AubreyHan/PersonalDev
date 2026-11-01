#include "TimeService_Dual.h"
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include "model/SystemState.h"

void TimeService_Dual::init() {
    Serial.println("[TimeService] Initializing Multi-Source Time Service...");

    // 1. First read from hardware PCF85063 RTC
    RtcDriver_PCF85063& rtc = RtcDriver_PCF85063::instance();
    if (rtc.init()) {
        RtcTimeInfo dt;
        if (rtc.readTime(dt)) {
            updateSystemTime(dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second, TIME_SOURCE_RTC);
            Serial.printf("[TimeService] Restored from RTC: %04d-%02d-%02d %02d:%02d:%02d\n",
                          dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
        }
    }
}

void TimeService_Dual::update() {
    GnssData& gnss = SystemState::instance().getContextUnsafe().gnss;

    // 1. Priority 1: GPS Time (if available and valid)
    if (gnss.isValid && gnss.year >= 2024 && (millis() - _lastGpsSyncMs > 5000)) {
        // Convert GPS UTC to China Standard Time (UTC+8)
        struct tm tmGps;
        memset(&tmGps, 0, sizeof(tmGps));
        tmGps.tm_year = gnss.year - 1900;
        tmGps.tm_mon  = gnss.month - 1;
        tmGps.tm_mday = gnss.day;
        tmGps.tm_hour = gnss.hour;
        tmGps.tm_min  = gnss.minute;
        tmGps.tm_sec  = gnss.second;

        time_t utcEpoch = mktime(&tmGps); // mktime treats as local, so adjust
        time_t cstEpoch = utcEpoch + NetworkConfig::TIMEZONE_OFFSET_SEC;
        struct tm* cst = gmtime(&cstEpoch);

        updateSystemTime(cst->tm_year + 1900, cst->tm_mon + 1, cst->tm_mday,
                         cst->tm_hour, cst->tm_min, cst->tm_sec,
                         TIME_SOURCE_GPS);
        _lastGpsSyncMs = millis();
        return;
    }

    // 2. Priority 2: Fallback WiFi NTP Time when GPS has no fix
    if (!gnss.isValid && WiFi.status() == WL_CONNECTED) {
        if (!_ntpConfigured) {
            configTime(NetworkConfig::TIMEZONE_OFFSET_SEC, 0, NetworkConfig::NTP_SERVER_1, NetworkConfig::NTP_SERVER_2);
            _ntpConfigured = true;
            _lastNtpSyncMs = millis();
            Serial.println("[TimeService] Configured SNTP with aliyun/pool.ntp.org");
        } else if (millis() - _lastNtpSyncMs > 60000 || _currentSource <= TIME_SOURCE_RTC) {
            syncNtpAsync();
            _lastNtpSyncMs = millis();
        }
    }
}

void TimeService_Dual::syncNtpAsync() {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year > (2024 - 1900)) {
        updateSystemTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                         timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                         TIME_SOURCE_NTP);
        Serial.printf("[TimeService] Synchronized from NTP: %02d:%02d:%02d\n",
                      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
}

void TimeService_Dual::updateSystemTime(uint16_t year, uint8_t month, uint8_t day,
                                        uint8_t hour, uint8_t minute, uint8_t second,
                                        TimeSourceType source) {
    _currentSource = source;

    // 1. Set ESP32 System Time
    struct tm t;
    t.tm_year = year - 1900;
    t.tm_mon  = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = minute;
    t.tm_sec  = second;
    t.tm_isdst = -1;

    time_t epoch = mktime(&t);
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, nullptr);

    // 2. Always synchronize back to PCF85063 Hardware RTC if source is GPS or NTP
    if (source == TIME_SOURCE_GPS || source == TIME_SOURCE_NTP) {
        RtcTimeInfo rtcTime;
        rtcTime.year    = year;
        rtcTime.month   = month;
        rtcTime.day     = day;
        rtcTime.hour    = hour;
        rtcTime.minute  = minute;
        rtcTime.second  = second;
        rtcTime.weekday = t.tm_wday;
        RtcDriver_PCF85063::instance().writeTime(rtcTime);
    }
}

void TimeService_Dual::getTimeString(char* outStr, size_t maxLen) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    snprintf(outStr, maxLen, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
}

void TimeService_Dual::getDateTimeString(char* outStr, size_t maxLen) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    snprintf(outStr, maxLen, "%04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}
