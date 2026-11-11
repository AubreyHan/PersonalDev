#include "TimeService.h"
#include "TrackLogger.h"
#include <Preferences.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {
    constexpr uint32_t MIN_PLAUSIBLE_EPOCH_UTC = 1735689600UL; // 2025-01-01T00:00:00Z
    constexpr uint32_t RTC_RECALIB_INTERVAL_MS = 900000UL;     // Re-sync RTC every 15 min on GNSS lock
    constexpr uint32_t NVS_POS_SAVE_INTERVAL_MS = 600000UL;    // Save warm-start coords every 10 min

    volatile bool g_sntpSyncCompletedFlag = false;

    void onSntpTimeSyncNotification(struct timeval* tv) {
        (void)tv;
        g_sntpSyncCompletedFlag = true;
    }

    // Helper: day-of-month (1..31) of the Nth Sunday (nth=1,2) or Last Sunday (nth=0)
    uint8_t findSundayOfMonth(uint16_t year, uint8_t month, uint8_t nth) {
        static constexpr uint8_t kDaysInMonth[12] = {
            31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
        };
        uint8_t maxDay = kDaysInMonth[(month - 1U) % 12U];
        if (month == 2U && (((year % 4U == 0U) && (year % 100U != 0U)) || (year % 400U == 0U))) {
            maxDay = 29U;
        }
        if (nth >= 1U) {
            const uint8_t firstWday = RtcDriver::computeWeekday(year, month, 1U); // 0=Sun
            const uint8_t firstSun  = static_cast<uint8_t>(1U + ((7U - firstWday) % 7U));
            return static_cast<uint8_t>(firstSun + (nth - 1U) * 7U);
        }
        const uint8_t lastWday = RtcDriver::computeWeekday(year, month, maxDay);
        return static_cast<uint8_t>(maxDay - lastWday);
    }

    uint32_t makeUtcEpoch(uint16_t year, uint8_t month, uint8_t day, uint8_t hour) {
        RtcDateTime dt{ year, month, day, 0U, hour, 0U, 0U };
        return RtcDriver::toUnixEpoch(dt);
    }
} // namespace

TimeService& TimeService::instance() {
    static TimeService s_instance;
    return s_instance;
}

void TimeService::computeUbxChecksum(const uint8_t* body, size_t len, uint8_t& ckA, uint8_t& ckB) {
    ckA = 0U;
    ckB = 0U;
    for (size_t i = 0; i < len; ++i) {
        ckA = static_cast<uint8_t>(ckA + body[i]);
        ckB = static_cast<uint8_t>(ckB + ckA);
    }
}

size_t TimeService::buildMgaIniTimeUtcFrame(const RtcDateTime& utc,
                                            uint32_t nanosecond,
                                            uint16_t tAccSec,
                                            uint32_t tAccNs,
                                            uint8_t outFrame[UBX_MGA_TIME_UTC_FRAME_LEN]) {
    memset(outFrame, 0, UBX_MGA_TIME_UTC_FRAME_LEN);
    outFrame[0] = 0xB5U; // UBX Sync 1
    outFrame[1] = 0x62U; // UBX Sync 2
    outFrame[2] = 0x13U; // Class: UBX-MGA
    outFrame[3] = 0x40U; // ID:    MGA-INI
    outFrame[4] = 24U;   // Payload length LSB (24 bytes)
    outFrame[5] = 0x00U; // Payload length MSB

    uint8_t* p = &outFrame[6];
    p[0]  = 0x10U; // type = MGA-INI-TIME_UTC
    p[1]  = 0x00U; // version = 0x00
    p[2]  = 0x00U; // ref = 0x00 (none / on receipt)
    p[3]  = static_cast<uint8_t>(GnssConfig::MGA_GPS_LEAP_SECONDS); // 18 leap seconds
    p[4]  = static_cast<uint8_t>(utc.year & 0xFFU);
    p[5]  = static_cast<uint8_t>((utc.year >> 8) & 0xFFU);
    p[6]  = utc.month;
    p[7]  = utc.day;
    p[8]  = utc.hour;
    p[9]  = utc.minute;
    p[10] = utc.second;
    p[11] = 0x00U; // reserved1

    p[12] = static_cast<uint8_t>(nanosecond & 0xFFUL);
    p[13] = static_cast<uint8_t>((nanosecond >> 8) & 0xFFUL);
    p[14] = static_cast<uint8_t>((nanosecond >> 16) & 0xFFUL);
    p[15] = static_cast<uint8_t>((nanosecond >> 24) & 0xFFUL);

    // Offset 16..17: tAccS (U2 LE), Offset 18..19: reserved2[2] = 0x0000
    p[16] = static_cast<uint8_t>(tAccSec & 0xFFU);
    p[17] = static_cast<uint8_t>((tAccSec >> 8) & 0xFFU);
    p[18] = 0x00U;
    p[19] = 0x00U;

    // Offset 20..23: tAccNs (U4 LE)
    p[20] = static_cast<uint8_t>(tAccNs & 0xFFUL);
    p[21] = static_cast<uint8_t>((tAccNs >> 8) & 0xFFUL);
    p[22] = static_cast<uint8_t>((tAccNs >> 16) & 0xFFUL);
    p[23] = static_cast<uint8_t>((tAccNs >> 24) & 0xFFUL);

    computeUbxChecksum(&outFrame[2], 4U + 24U, outFrame[30], outFrame[31]);
    return UBX_MGA_TIME_UTC_FRAME_LEN;
}

size_t TimeService::buildMgaIniPosLlhFrame(int32_t latE7,
                                           int32_t lonE7,
                                           int32_t altCm,
                                           uint32_t posAccCm,
                                           uint8_t outFrame[UBX_MGA_POS_LLH_FRAME_LEN]) {
    memset(outFrame, 0, UBX_MGA_POS_LLH_FRAME_LEN);
    outFrame[0] = 0xB5U; // UBX Sync 1
    outFrame[1] = 0x62U; // UBX Sync 2
    outFrame[2] = 0x13U; // Class: UBX-MGA
    outFrame[3] = 0x40U; // ID:    MGA-INI
    outFrame[4] = 20U;   // Payload length LSB (20 bytes)
    outFrame[5] = 0x00U; // Payload length MSB

    uint8_t* p = &outFrame[6];
    p[0] = 0x01U; // type = MGA-INI-POS_LLH
    p[1] = 0x00U; // version = 0x00
    p[2] = 0x00U; // reserved1[0]
    p[3] = 0x00U; // reserved1[1]

    const uint32_t uLat = static_cast<uint32_t>(latE7);
    const uint32_t uLon = static_cast<uint32_t>(lonE7);
    const uint32_t uAlt = static_cast<uint32_t>(altCm);

    p[4]  = static_cast<uint8_t>(uLat & 0xFFUL);
    p[5]  = static_cast<uint8_t>((uLat >> 8) & 0xFFUL);
    p[6]  = static_cast<uint8_t>((uLat >> 16) & 0xFFUL);
    p[7]  = static_cast<uint8_t>((uLat >> 24) & 0xFFUL);

    p[8]  = static_cast<uint8_t>(uLon & 0xFFUL);
    p[9]  = static_cast<uint8_t>((uLon >> 8) & 0xFFUL);
    p[10] = static_cast<uint8_t>((uLon >> 16) & 0xFFUL);
    p[11] = static_cast<uint8_t>((uLon >> 24) & 0xFFUL);

    p[12] = static_cast<uint8_t>(uAlt & 0xFFUL);
    p[13] = static_cast<uint8_t>((uAlt >> 8) & 0xFFUL);
    p[14] = static_cast<uint8_t>((uAlt >> 16) & 0xFFUL);
    p[15] = static_cast<uint8_t>((uAlt >> 24) & 0xFFUL);

    p[16] = static_cast<uint8_t>(posAccCm & 0xFFUL);
    p[17] = static_cast<uint8_t>((posAccCm >> 8) & 0xFFUL);
    p[18] = static_cast<uint8_t>((posAccCm >> 16) & 0xFFUL);
    p[19] = static_cast<uint8_t>((posAccCm >> 24) & 0xFFUL);

    computeUbxChecksum(&outFrame[2], 4U + 20U, outFrame[26], outFrame[27]);
    return UBX_MGA_POS_LLH_FRAME_LEN;
}

int16_t TimeService::computeLongitudeTzOffsetMin(double longitudeDeg, int16_t prevOffsetMin) {
    if (!isfinite(longitudeDeg) || longitudeDeg < -180.0 || longitudeDeg > 180.0) {
        return prevOffsetMin;
    }
    // 0.30 deg deadband hysteresis around 15-degree meridian zone boundaries (7.5 + 15*k)
    const double prevHours = static_cast<double>(prevOffsetMin) / 60.0;
    const double prevCenterLon = prevHours * 15.0;
    if ((prevOffsetMin % 60 == 0) && fabs(longitudeDeg - prevCenterLon) <= 7.80) {
        return prevOffsetMin;
    }
    int16_t zoneHours = static_cast<int16_t>(lround(longitudeDeg / 15.0));
    if (zoneHours < -12) zoneHours = -12;
    if (zoneHours > 14)  zoneHours = 14;
    return static_cast<int16_t>(zoneHours * 60);
}

bool TimeService::isDstActiveForUtc(uint32_t epochUtc, int16_t baseOffsetMin, DstRuleType rule) {
    if (rule == DST_RULE_NONE || epochUtc < MIN_PLAUSIBLE_EPOCH_UTC) {
        return false;
    }
    RtcDateTime utc{};
    RtcDriver::fromUnixEpoch(epochUtc, utc);
    const uint16_t y = utc.year;

    switch (rule) {
        case DST_RULE_US_EASTERN:
        case DST_RULE_US_PACIFIC: {
            // US DST: Starts 2nd Sunday of March @ 02:00 local standard time
            //         Ends   1st Sunday of Nov   @ 02:00 local daylight (= 01:00 standard)
            const uint8_t marSun = findSundayOfMonth(y, 3U, 2U);
            const uint8_t novSun = findSundayOfMonth(y, 11U, 1U);
            const int64_t startUtc = static_cast<int64_t>(makeUtcEpoch(y, 3U, marSun, 2U))
                                   - static_cast<int64_t>(baseOffsetMin) * 60LL;
            const int64_t endUtc   = static_cast<int64_t>(makeUtcEpoch(y, 11U, novSun, 1U))
                                   - static_cast<int64_t>(baseOffsetMin) * 60LL;
            return (static_cast<int64_t>(epochUtc) >= startUtc) &&
                   (static_cast<int64_t>(epochUtc) <  endUtc);
        }
        case DST_RULE_EU_CENTRAL:
        case DST_RULE_EU_LONDON: {
            // EU / UK Summer Time: Last Sunday of March 01:00 UTC -> Last Sunday of October 01:00 UTC
            const uint8_t marLastSun = findSundayOfMonth(y, 3U, 0U);
            const uint8_t octLastSun = findSundayOfMonth(y, 10U, 0U);
            const uint32_t startUtc  = makeUtcEpoch(y, 3U, marLastSun, 1U);
            const uint32_t endUtc    = makeUtcEpoch(y, 10U, octLastSun, 1U);
            return (epochUtc >= startUtc && epochUtc < endUtc);
        }
        case DST_RULE_AU_SYDNEY: {
            // Australia (AEDT): Starts 1st Sunday of Oct @ 02:00 AEST, Ends 1st Sunday of Apr @ 02:00 AEST
            const uint8_t aprSun = findSundayOfMonth(y, 4U, 1U);
            const uint8_t octSun = findSundayOfMonth(y, 10U, 1U);
            const int64_t endAprUtc   = static_cast<int64_t>(makeUtcEpoch(y, 4U, aprSun, 2U))
                                      - static_cast<int64_t>(baseOffsetMin) * 60LL;
            const int64_t startOctUtc = static_cast<int64_t>(makeUtcEpoch(y, 10U, octSun, 2U))
                                      - static_cast<int64_t>(baseOffsetMin) * 60LL;
            return (static_cast<int64_t>(epochUtc) < endAprUtc) ||
                   (static_cast<int64_t>(epochUtc) >= startOctUtc);
        }
        default:
            return false;
    }
}

void TimeService::setSystemClockUtc(uint32_t epochUtc, uint32_t usec) {
    struct timeval tv{};
    tv.tv_sec  = static_cast<time_t>(epochUtc);
    tv.tv_usec = static_cast<suseconds_t>(usec % 1000000UL);
    settimeofday(&tv, nullptr);
}

uint32_t TimeService::readSystemClockUtc(uint32_t* outUsec) {
    struct timeval tv{};
    gettimeofday(&tv, nullptr);
    if (outUsec != nullptr) {
        *outUsec = (tv.tv_usec >= 0) ? static_cast<uint32_t>(tv.tv_usec) : 0UL;
    }
    return (tv.tv_sec > 0) ? static_cast<uint32_t>(tv.tv_sec) : 0UL;
}

void TimeService::loadFromNvs() {
    Preferences prefs;
    if (!prefs.begin(NvsConfig::NAMESPACE, true)) {
        return;
    }

    const int16_t tzOff      = prefs.getShort(NvsConfig::KEY_TZ_OFFSET_MIN, 480);
    const bool    tzAuto     = prefs.getBool(NvsConfig::KEY_TZ_AUTO_GPS, true);
    const uint8_t bright     = prefs.getUChar(NvsConfig::KEY_SCREEN_BRIGHT, 180U);
    const bool    autoBright = prefs.getBool(NvsConfig::KEY_AUTO_BRIGHT, true);
    const bool    beepEn     = prefs.getBool(NvsConfig::KEY_BEEP_ENABLE, true);
    const bool    autoRec    = prefs.getBool(NvsConfig::KEY_AUTO_RECORD, true);

    if (prefs.isKey(NvsConfig::KEY_LAST_LAT_E7) && prefs.isKey(NvsConfig::KEY_LAST_LON_E7)) {
        _nvsLastLatE7  = prefs.getInt(NvsConfig::KEY_LAST_LAT_E7, 0);
        _nvsLastLonE7  = prefs.getInt(NvsConfig::KEY_LAST_LON_E7, 0);
        _nvsLastAltCm  = prefs.getInt(NvsConfig::KEY_LAST_ALT_CM, 0);
        _nvsLastPosUtc = prefs.getUInt(NvsConfig::KEY_LAST_POS_UTC, 0UL);
        _hasNvsLastPos = (_nvsLastLatE7 != 0 || _nvsLastLonE7 != 0);
    }

    WorldClockEntry loadedClocks[4];
    bool hasCustomClocks = false;
    {
        StateGuard guard(SystemState::instance());
        for (size_t i = 0; i < 4; ++i) {
            loadedClocks[i] = SystemState::instance().data().time.worldClocks[i];
        }
    }

    for (size_t i = 0; i < 4; ++i) {
        char keyCity[16], keyReg[16], keyOff[16], keyDst[16], keyGps[16];
        snprintf(keyCity, sizeof(keyCity), "wc%u_city", static_cast<unsigned>(i));
        snprintf(keyReg,  sizeof(keyReg),  "wc%u_reg",  static_cast<unsigned>(i));
        snprintf(keyOff,  sizeof(keyOff),  "wc%u_off",  static_cast<unsigned>(i));
        snprintf(keyDst,  sizeof(keyDst),  "wc%u_dst",  static_cast<unsigned>(i));
        snprintf(keyGps,  sizeof(keyGps),  "wc%u_gps",  static_cast<unsigned>(i));

        if (prefs.isKey(keyCity)) {
            hasCustomClocks = true;
            prefs.getString(keyCity, loadedClocks[i].cityName, sizeof(loadedClocks[i].cityName));
            prefs.getString(keyReg,  loadedClocks[i].regionGroup, sizeof(loadedClocks[i].regionGroup));
            loadedClocks[i].baseUtcOffsetMin = prefs.getShort(keyOff, loadedClocks[i].baseUtcOffsetMin);
            const uint8_t dstRaw = prefs.getUChar(keyDst, static_cast<uint8_t>(loadedClocks[i].dstRule));
            loadedClocks[i].dstRule = (dstRaw <= DST_RULE_AU_SYDNEY)
                ? static_cast<DstRuleType>(dstRaw)
                : DST_RULE_NONE;
            loadedClocks[i].useGpsDynamicTz = prefs.getBool(keyGps, loadedClocks[i].useGpsDynamicTz);
        }
    }
    prefs.end();

    StateGuard guard(SystemState::instance());
    SystemStateSnapshot& st = SystemState::instance().data();
    st.time.localTzOffsetMin        = tzOff;
    st.time.autoLocalTzFromGps      = tzAuto;
    st.settings.screenBrightness    = bright;
    st.settings.autoDayNightBacklight = autoBright;
    st.settings.speakerBeepEnabled  = beepEn;
    st.settings.autoRecordOnMove    = autoRec;
    st.storage.autoRecordEnabled    = autoRec;
    if (hasCustomClocks) {
        for (size_t i = 0; i < 4; ++i) {
            st.time.worldClocks[i] = loadedClocks[i];
        }
    }
}

bool TimeService::saveWorldClocksToNvs() {
    TimeData timeCopy;
    {
        StateGuard guard(SystemState::instance());
        SystemStateSnapshot& st = SystemState::instance().data();
        st.time.cmdSaveClocksNvs = false;
        timeCopy = st.time;
    }

    Preferences prefs;
    if (!prefs.begin(NvsConfig::NAMESPACE, false)) {
        return false;
    }
    prefs.putShort(NvsConfig::KEY_TZ_OFFSET_MIN, timeCopy.localTzOffsetMin);
    prefs.putBool(NvsConfig::KEY_TZ_AUTO_GPS, timeCopy.autoLocalTzFromGps);

    for (size_t i = 0; i < 4; ++i) {
        char keyCity[16], keyReg[16], keyOff[16], keyDst[16], keyGps[16];
        snprintf(keyCity, sizeof(keyCity), "wc%u_city", static_cast<unsigned>(i));
        snprintf(keyReg,  sizeof(keyReg),  "wc%u_reg",  static_cast<unsigned>(i));
        snprintf(keyOff,  sizeof(keyOff),  "wc%u_off",  static_cast<unsigned>(i));
        snprintf(keyDst,  sizeof(keyDst),  "wc%u_dst",  static_cast<unsigned>(i));
        snprintf(keyGps,  sizeof(keyGps),  "wc%u_gps",  static_cast<unsigned>(i));

        prefs.putString(keyCity, timeCopy.worldClocks[i].cityName);
        prefs.putString(keyReg,  timeCopy.worldClocks[i].regionGroup);
        prefs.putShort(keyOff,   timeCopy.worldClocks[i].baseUtcOffsetMin);
        prefs.putUChar(keyDst,   static_cast<uint8_t>(timeCopy.worldClocks[i].dstRule));
        prefs.putBool(keyGps,    timeCopy.worldClocks[i].useGpsDynamicTz);
    }
    prefs.end();
    return true;
}

void TimeService::saveUserSettingsToNvs(const UserSettings& settings) {
    Preferences prefs;
    if (!prefs.begin(NvsConfig::NAMESPACE, false)) {
        return;
    }
    prefs.putUChar(NvsConfig::KEY_SCREEN_BRIGHT, settings.screenBrightness);
    prefs.putBool(NvsConfig::KEY_AUTO_BRIGHT, settings.autoDayNightBacklight);
    prefs.putBool(NvsConfig::KEY_BEEP_ENABLE, settings.speakerBeepEnabled);
    prefs.putBool(NvsConfig::KEY_AUTO_RECORD, settings.autoRecordOnMove);
    prefs.putBool(NvsConfig::KEY_OD_AUTO_SYNC, settings.oneDriveAutoSync);
    if (settings.assistNowToken[0] != '\0') {
        prefs.putString(NvsConfig::KEY_AGPS_TOKEN, settings.assistNowToken);
    }
    if (settings.oneDriveClientId[0] != '\0') {
        prefs.putString(NvsConfig::KEY_OD_CLIENT_ID, settings.oneDriveClientId);
    }
    if (settings.oneDriveTenant[0] != '\0') {
        prefs.putString(NvsConfig::KEY_OD_TENANT, settings.oneDriveTenant);
    }
    prefs.end();
}

bool TimeService::saveWarmStartPositionToNvs() {
    int32_t  latE7 = 0, lonE7 = 0, altCm = 0;
    uint32_t nowUtc = 0;
    bool     valid  = false;
    {
        StateGuard guard(SystemState::instance(), pdMS_TO_TICKS(50));
        if (guard.locked()) {
            const SystemStateSnapshot& st = SystemState::instance().data();
            if (st.gnss.isValid && st.gnss.fixType >= FIX_3D &&
                (st.gnss.latE7 != 0 || st.gnss.lonE7 != 0)) {
                latE7  = st.gnss.latE7;
                lonE7  = st.gnss.lonE7;
                const float altM = st.baro.isOnline ? st.baro.fusedAltitudeM : st.gnss.altitudeMeters;
                altCm  = static_cast<int32_t>(lroundf(altM * 100.0f));
                nowUtc = st.time.epochUtc;
                valid  = true;
            }
        }
    }
    if (!valid) {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(NvsConfig::NAMESPACE, false)) {
        return false;
    }
    prefs.putInt(NvsConfig::KEY_LAST_LAT_E7, latE7);
    prefs.putInt(NvsConfig::KEY_LAST_LON_E7, lonE7);
    prefs.putInt(NvsConfig::KEY_LAST_ALT_CM, altCm);
    prefs.putUInt(NvsConfig::KEY_LAST_POS_UTC, nowUtc);
    prefs.end();

    _hasNvsLastPos = true;
    _nvsLastLatE7  = latE7;
    _nvsLastLonE7  = lonE7;
    _nvsLastAltCm  = altCm;
    _nvsLastPosUtc = nowUtc;
    return true;
}

void TimeService::stageMgaTimeFrame(const RtcDateTime& utc, uint32_t ns, uint16_t tAccS, uint32_t tAccNs) {
    buildMgaIniTimeUtcFrame(utc, ns, tAccS, tAccNs, _pendingMgaTime);
    _hasPendingMgaTime = true;
}

void TimeService::stageMgaPosFrameFromNvs(uint32_t currentEpochUtc) {
    if (!_hasNvsLastPos) {
        return;
    }
    uint32_t posAccCm = GnssConfig::MGA_POS_ACC_STALE_CM; // Default 30km if age unknown/stale
    if (currentEpochUtc >= MIN_PLAUSIBLE_EPOCH_UTC &&
        _nvsLastPosUtc >= MIN_PLAUSIBLE_EPOCH_UTC &&
        currentEpochUtc >= _nvsLastPosUtc) {
        const uint32_t ageSec = currentEpochUtc - _nvsLastPosUtc;
        if (ageSec <= GnssConfig::MGA_POS_FRESH_MAX_AGE_SEC) {
            posAccCm = GnssConfig::MGA_POS_ACC_FRESH_CM; // 500m fresh garage warm-start
        }
    }
    buildMgaIniPosLlhFrame(_nvsLastLatE7, _nvsLastLonE7, _nvsLastAltCm, posAccCm, _pendingMgaPos);
    _hasPendingMgaPos = true;
}

void TimeService::setMgaInjectCallback(MgaInjectCallback cb) {
    _mgaInjectCb = cb;
    flushPendingMgaAssistance();
}

uint8_t TimeService::flushPendingMgaAssistance() {
    if (!_mgaInjectCb) {
        return 0U;
    }
    uint8_t sent = 0U;
    if (_hasPendingMgaTime) {
        if (_mgaInjectCb(_pendingMgaTime, UBX_MGA_TIME_UTC_FRAME_LEN)) {
            _hasPendingMgaTime = false;
            sent++;
        }
    }
    if (_hasPendingMgaPos) {
        if (_mgaInjectCb(_pendingMgaPos, UBX_MGA_POS_LLH_FRAME_LEN)) {
            _hasPendingMgaPos = false;
            _posInjectedOnce  = true;
            sent++;
        }
    }
    return sent;
}

bool TimeService::popPendingMgaTimeFrame(uint8_t outFrame[UBX_MGA_TIME_UTC_FRAME_LEN]) {
    if (!_hasPendingMgaTime || outFrame == nullptr) {
        return false;
    }
    memcpy(outFrame, _pendingMgaTime, UBX_MGA_TIME_UTC_FRAME_LEN);
    _hasPendingMgaTime = false;
    return true;
}

bool TimeService::popPendingMgaPosFrame(uint8_t outFrame[UBX_MGA_POS_LLH_FRAME_LEN]) {
    if (!_hasPendingMgaPos || outFrame == nullptr) {
        return false;
    }
    memcpy(outFrame, _pendingMgaPos, UBX_MGA_POS_LLH_FRAME_LEN);
    _hasPendingMgaPos = false;
    _posInjectedOnce  = true;
    return true;
}

bool TimeService::begin() {
    loadFromNvs();

    RtcDriver& rtc = RtcDriver::instance();
    const bool rtcOnline = rtc.begin();
    bool       vlfFlag   = rtc.lastVlfState();
    uint32_t   bootEpoch = 0UL;

    RtcDateTime rtcUtc{};
    if (rtcOnline && rtc.readUtc(rtcUtc)) {
        bootEpoch = RtcDriver::toUnixEpoch(rtcUtc);
        if (bootEpoch >= MIN_PLAUSIBLE_EPOCH_UTC) {
            setSystemClockUtc(bootEpoch, 0UL);
            _syncSource = CLOCK_SRC_RTC;
            vlfFlag     = false;

            // Stage cold-boot UBX-MGA-INI-TIME_UTC (tAccS = 2s) + UBX-MGA-INI-POS_LLH
            stageMgaTimeFrame(rtcUtc, 0UL, GnssConfig::MGA_RTC_TACC_SEC, GnssConfig::MGA_RTC_TACC_NS);
            stageMgaPosFrameFromNvs(bootEpoch);
        }
    } else if (_hasNvsLastPos) {
        // Even if RTC lost backup (VLF=1), coarse 30km position helps AssistNow / acquisition
        stageMgaPosFrameFromNvs(0UL);
    }

    {
        StateGuard guard(SystemState::instance());
        SystemStateSnapshot& st = SystemState::instance().data();
        st.boot.rtcOnline        = rtcOnline;
        st.time.rtcVoltageLowVlf = vlfFlag;
        st.time.source           = _syncSource;
        st.time.isCalibrated     = (_syncSource != CLOCK_SRC_UNSYNCED);
    }

    const double initLon = _hasNvsLastPos ? (static_cast<double>(_nvsLastLonE7) * 1e-7) : 116.4;
    refreshAllClocksInState(bootEpoch > 0UL ? bootEpoch : readSystemClockUtc(), initLon, _hasNvsLastPos);
    flushPendingMgaAssistance();
    return rtcOnline;
}

void TimeService::refreshAllClocksInState(uint32_t epochUtc, double gnssLon, bool gnssValid) {
    RtcDateTime utcDt{};
    if (epochUtc > 0UL) {
        RtcDriver::fromUnixEpoch(epochUtc, utcDt);
    }

    StateGuard guard(SystemState::instance());
    TimeData& td = SystemState::instance().data().time;

    td.source       = _syncSource;
    td.isCalibrated = (_syncSource != CLOCK_SRC_UNSYNCED) && (epochUtc >= MIN_PLAUSIBLE_EPOCH_UTC);
    td.epochUtc     = epochUtc;
    td.utcYear      = utcDt.year;
    td.utcMonth     = utcDt.month;
    td.utcDay       = utcDt.day;
    td.utcHour      = utcDt.hour;
    td.utcMinute    = utcDt.minute;
    td.utcSecond    = utcDt.second;

    if (td.autoLocalTzFromGps && gnssValid) {
        td.localTzOffsetMin = computeLongitudeTzOffsetMin(gnssLon, td.localTzOffsetMin);
    }

    const int64_t localSigned = static_cast<int64_t>(epochUtc) + static_cast<int64_t>(td.localTzOffsetMin) * 60LL;
    const uint32_t localSec = (localSigned > 0) ? static_cast<uint32_t>(localSigned) : 0UL;
    RtcDateTime localDt{};
    if (localSec > 0UL) {
        RtcDriver::fromUnixEpoch(localSec, localDt);
    }
    td.localYear    = localDt.year;
    td.localMonth   = localDt.month;
    td.localDay     = localDt.day;
    td.localWeekday = localDt.weekDay;
    td.localHour    = localDt.hour;
    td.localMinute  = localDt.minute;
    td.localSecond  = localDt.second;

    // Update all 4 editable WorldClockEntry cards
    for (size_t i = 0; i < 4; ++i) {
        WorldClockEntry& wc = td.worldClocks[i];
        if (wc.useGpsDynamicTz) {
            if (gnssValid) {
                wc.baseUtcOffsetMin = computeLongitudeTzOffsetMin(gnssLon, wc.baseUtcOffsetMin);
            } else {
                wc.baseUtcOffsetMin = td.localTzOffsetMin;
            }
            wc.isDstActive        = false;
            wc.effectiveOffsetMin = wc.baseUtcOffsetMin;
        } else {
            wc.isDstActive        = isDstActiveForUtc(epochUtc, wc.baseUtcOffsetMin, wc.dstRule);
            wc.effectiveOffsetMin = static_cast<int16_t>(wc.baseUtcOffsetMin + (wc.isDstActive ? 60 : 0));
        }

        const int64_t cardSigned = static_cast<int64_t>(epochUtc) + static_cast<int64_t>(wc.effectiveOffsetMin) * 60LL;
        const uint32_t cardSec   = (cardSigned > 0) ? static_cast<uint32_t>(cardSigned) : 0UL;
        RtcDateTime cardDt{};
        if (cardSec > 0UL) {
            RtcDriver::fromUnixEpoch(cardSec, cardDt);
        }
        wc.year      = cardDt.year;
        wc.month     = cardDt.month;
        wc.day       = cardDt.day;
        wc.weekday   = cardDt.weekDay;
        wc.hour      = cardDt.hour;
        wc.minute    = cardDt.minute;
        wc.second    = cardDt.second;
        wc.isDaytime = (cardDt.hour >= 6U && cardDt.hour < 18U);
    }
}

void TimeService::update() {
    GnssData     gnssSnap{};
    bool         staConnected  = false;
    bool         saveClocksReq = false;
    bool         saveSetReq    = false;
    UserSettings settingsSnap{};

    {
        StateGuard guard(SystemState::instance(), pdMS_TO_TICKS(20));
        if (!guard.locked()) {
            return;
        }
        SystemStateSnapshot& st = SystemState::instance().data();
        gnssSnap      = st.gnss;
        staConnected  = st.network.staConnected;
        saveClocksReq = st.time.cmdSaveClocksNvs;
        saveSetReq    = st.settings.cmdSaveSettingsToNvs;
        if (saveSetReq) {
            st.settings.cmdSaveSettingsToNvs = false;
            st.storage.autoRecordEnabled     = st.settings.autoRecordOnMove;
            settingsSnap                     = st.settings;
        }
    }

    if (saveClocksReq) {
        saveWorldClocksToNvs();
    }
    if (saveSetReq) {
        saveUserSettingsToNvs(settingsSnap);
    }

    const uint32_t nowMs = millis();
    uint32_t sysUsec     = 0UL;
    uint32_t activeEpoch = readSystemClockUtc(&sysUsec);

    // -------------------------------------------------------------------------
    // Source 1 (Highest Authority): GNSS 3D Fix Atomic UTC
    // -------------------------------------------------------------------------
    const bool gnssTimeAuth = (gnssSnap.fixType >= FIX_3D) &&
                              gnssSnap.gnssFixOk &&
                              gnssSnap.utcDateValid &&
                              gnssSnap.utcTimeValid &&
                              (gnssSnap.year >= 2025U);

    if (gnssTimeAuth) {
        RtcDateTime gnssDt{
            gnssSnap.year, gnssSnap.month, gnssSnap.day, 0U,
            gnssSnap.hour, gnssSnap.minute, gnssSnap.second
        };
        gnssDt.weekDay = RtcDriver::computeWeekday(gnssDt.year, gnssDt.month, gnssDt.day);
        const uint32_t gnssEpoch = RtcDriver::toUnixEpoch(gnssDt);

        if (gnssEpoch >= MIN_PLAUSIBLE_EPOCH_UTC) {
            const int64_t diffSec = static_cast<int64_t>(gnssEpoch) - static_cast<int64_t>(activeEpoch);
            if (_syncSource != CLOCK_SRC_GNSS || diffSec < -1LL || diffSec > 1LL) {
                setSystemClockUtc(gnssEpoch, static_cast<uint32_t>(gnssSnap.millisecond) * 1000UL);
                activeEpoch = gnssEpoch;
            }
            _syncSource = CLOCK_SRC_GNSS;

            // Reverse-calibrate RX8130CE hardware RTC on first GNSS lock or every 15 min
            if (_lastRtcCalibMs == 0UL ||
                RtcDriver::instance().lastVlfState() ||
                (nowMs - _lastRtcCalibMs >= RTC_RECALIB_INTERVAL_MS)) {
                if (RtcDriver::instance().writeUtc(gnssDt)) {
                    _lastRtcCalibMs = nowMs;
                    StateGuard guard(SystemState::instance());
                    SystemState::instance().data().time.rtcVoltageLowVlf = false;
                }
            }

            // Periodically persist warm-start 3D coordinates to NVS
            if (_lastPosSaveMs == 0UL || (nowMs - _lastPosSaveMs >= NVS_POS_SAVE_INTERVAL_MS)) {
                if (saveWarmStartPositionToNvs()) {
                    _lastPosSaveMs = nowMs;
                }
            }

            if (!_gnssSyncLogged) {
                _gnssSyncLogged = true;
                TrackLogger::instance().appendPowerLog("TIME_SYNC_GNSS", "Atomic UTC locked & RX8130CE calibrated");
            }
        }
    } else if (staConnected) {
        // ---------------------------------------------------------------------
        // Source 2: Wi-Fi SNTP (when no GNSS 3D Fix is active)
        // ---------------------------------------------------------------------
        if (!_ntpConfigured || (nowMs - _lastNtpConfigMs >= 300000UL && _syncSource != CLOCK_SRC_NTP)) {
            esp_sntp_set_time_sync_notification_cb(&onSntpTimeSyncNotification);
            configTime(0L, 0, NetConfig::NTP_SERVER_1, NetConfig::NTP_SERVER_2, NetConfig::NTP_SERVER_3);
            _ntpConfigured   = true;
            _lastNtpConfigMs = nowMs;
        }

        if (g_sntpSyncCompletedFlag || esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            g_sntpSyncCompletedFlag = false;
            uint32_t ntpUsec = 0UL;
            const uint32_t ntpEpoch = readSystemClockUtc(&ntpUsec);
            if (ntpEpoch >= MIN_PLAUSIBLE_EPOCH_UTC) {
                activeEpoch = ntpEpoch;
                if (_syncSource != CLOCK_SRC_GNSS) {
                    _syncSource = CLOCK_SRC_NTP;
                }

                RtcDateTime ntpUtc{};
                RtcDriver::fromUnixEpoch(ntpEpoch, ntpUtc);
                if (RtcDriver::instance().writeUtc(ntpUtc)) {
                    _lastRtcCalibMs = nowMs;
                }

                // Inject precise NTP time (50ms accuracy) + NVS warm-start pos into NEO-M9N
                stageMgaTimeFrame(ntpUtc,
                                  ntpUsec * 1000UL,
                                  GnssConfig::MGA_NTP_TACC_SEC,
                                  GnssConfig::MGA_NTP_TACC_NS);
                if (!_posInjectedOnce && _hasNvsLastPos) {
                    stageMgaPosFrameFromNvs(ntpEpoch);
                }

                {
                    StateGuard guard(SystemState::instance());
                    SystemStateSnapshot& st = SystemState::instance().data();
                    st.network.ntpSynced     = true;
                    st.time.rtcVoltageLowVlf = false;
                }

                if (!_ntpSyncLogged) {
                    _ntpSyncLogged = true;
                    TrackLogger::instance().appendPowerLog("TIME_SYNC_NTP", "NTP synced, RTC written & MGA-INI-TIME_UTC staged");
                }
            }
        }
    } else {
        // Wi-Fi disconnected: allow immediate SNTP re-trigger upon next connection
        _ntpConfigured = false;
    }

    flushPendingMgaAssistance();

    // Refresh SystemState TimeData & 4 WorldClockEntry cards whenever UTC second ticks
    const bool validLon = gnssSnap.isValid && (gnssSnap.fixType >= FIX_2D || gnssSnap.isDeadReckoning);
    const double refLon = validLon
        ? gnssSnap.longitude
        : (_hasNvsLastPos ? (static_cast<double>(_nvsLastLonE7) * 1e-7) : 116.4);

    if (activeEpoch != _lastRenderedEpoch || _lastRenderedEpoch == 0UL || saveClocksReq) {
        _lastRenderedEpoch = activeEpoch;
        refreshAllClocksInState(activeEpoch, refLon, validLon || _hasNvsLastPos);
    }
}
