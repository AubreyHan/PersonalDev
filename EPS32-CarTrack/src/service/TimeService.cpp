#include "service/TimeService.h"
#include <WiFi.h>
#include <sntp.h>
#include <math.h>

void TimeService::init() {
    RtcDriver::instance().init();
}

void TimeService::syncWithNtp() {
    configTime(0, 0, "ntp.aliyun.com", "pool.ntp.org", "time.apple.com");
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 3000)) {
        uint16_t year = timeinfo.tm_year + 1900;
        uint8_t month = timeinfo.tm_mon + 1;
        uint8_t day = timeinfo.tm_mday;
        uint8_t hour = timeinfo.tm_hour;
        uint8_t minute = timeinfo.tm_min;
        uint8_t second = timeinfo.tm_sec;

        RtcDriver::instance().setTime(year, month, day, hour, minute, second);

        // Feedback inject UTC time to NEO-M9N
        GnssDriver::instance().injectUtcTime(year, month, day, hour, minute, second, 50000000);
        Serial.printf("[TIME] NTP synchronized: %04d-%02d-%02d %02d:%02d:%02d UTC\n", 
                      year, month, day, hour, minute, second);
    }
}

void TimeService::update(bool wifiConnected, bool gnssValid) {
    unsigned long now = millis();

    SystemState& state = SystemState::instance();
    state.lock();
    SystemContext& ctx = state.getContextUnsafe();

    if (gnssValid && ctx.gnss.year >= 2024) {
        // GPS atomic time takes highest priority
        if (now - _lastGpsSync > 60000 || _lastGpsSync == 0) {
            _lastGpsSync = now;
            RtcDriver::instance().setTime(ctx.gnss.year, ctx.gnss.month, ctx.gnss.day,
                                          ctx.gnss.hour, ctx.gnss.minute, ctx.gnss.second);

            // Auto calculate local timezone offset: 15° longitude per hour
            int8_t autoTz = (int8_t)round(ctx.gnss.longitude / 15.0);
            if (ctx.worldClock.slots[0].mode == CLOCK_MODE_GPS_AUTO) {
                ctx.worldClock.slots[0].utcOffsetHours = autoTz;
            }
        }
    } else if (wifiConnected && !gnssValid) {
        // NTP synchronization fallback
        if (now - _lastNtpSync > 3600000 || _lastNtpSync == 0) {
            _lastNtpSync = now;
            state.unlock();
            syncWithNtp();
            return;
        }
    }
    state.unlock();
}

void TimeService::getFormattedLocalTime(char* buffer, size_t maxLen, int8_t utcOffsetHours) {
    time_t now = time(nullptr);
    now += utcOffsetHours * 3600;
    struct tm* tm_info = gmtime(&now);
    if (tm_info) {
        snprintf(buffer, maxLen, "%02d:%02d:%02d", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    } else {
        snprintf(buffer, maxLen, "--:--:--");
    }
}

void TimeService::getSlotTime(uint8_t slotIdx, char* timeBuf, size_t timeLen, char* dateBuf, size_t dateLen, bool& isDaytime) {
    SystemState& state = SystemState::instance();
    state.lock();
    WorldClockSlotConfig slot = state.getContextUnsafe().worldClock.slots[slotIdx];
    state.unlock();

    time_t now = time(nullptr);
    now += slot.utcOffsetHours * 3600;
    struct tm* tm_info = gmtime(&now);
    if (tm_info) {
        snprintf(timeBuf, timeLen, "%02d:%02d:%02d", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
        static const char* const WEEKS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
        snprintf(dateBuf, dateLen, "%d月%d日 %s", tm_info->tm_mon + 1, tm_info->tm_mday, WEEKS[tm_info->tm_wday]);
        isDaytime = (tm_info->tm_hour >= 6 && tm_info->tm_hour < 18);
    } else {
        snprintf(timeBuf, timeLen, "--:--:--");
        snprintf(dateBuf, dateLen, "--/--");
        isDaytime = true;
    }
}
