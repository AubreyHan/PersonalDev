#include "GnssDriver.h"

#include "../service/DeadReckoningService.h"
#include "../service/TunnelService.h"

#include <Preferences.h>
#include <u-blox_config_keys.h>
#include <u-blox_structs.h>
#include <math.h>
#include <string.h>
#include <time.h>

namespace {
    constexpr float  GRAVITY_MPS2          = 9.80665f;
    constexpr uint32_t HISTORY_PUSH_MS     = 30000UL;   // 1 sample / 30s -> 60 min (120 samples)
    constexpr uint32_t POS_NVS_SAVE_MS     = 300000UL;  // Save warm-start LLH every 5 min
    constexpr uint32_t TRIP_NVS_SAVE_MS    = 60000UL;   // Persist daily/manual distance every 60s
    constexpr double TRAIL_MIN_STEP_METERS = 8.0;       // Minimum 8m spacing between PSRAM trail pts

    class UartLock {
    public:
        explicit UartLock(SemaphoreHandle_t mtx, TickType_t wait = portMAX_DELAY)
            : _mtx(mtx), _ok(false) {
            if (_mtx != nullptr) {
                _ok = (xSemaphoreTake(_mtx, wait) == pdTRUE);
            }
        }
        ~UartLock() {
            if (_ok && _mtx != nullptr) {
                xSemaphoreGive(_mtx);
            }
        }
        explicit operator bool() const { return _ok; }
    private:
        SemaphoreHandle_t _mtx;
        bool              _ok;
    };
}

GnssDriver::GnssDriver() : _serial(1) {
    ensureMutex();
}

GnssDriver& GnssDriver::instance() {
    static GnssDriver s_instance;
    return s_instance;
}

void GnssDriver::ensureMutex() {
    if (_uartMutex == nullptr) {
        _uartMutex = xSemaphoreCreateMutex();
    }
}

void GnssDriver::computeUbxChecksum(uint8_t* frame, size_t payloadLen) {
    uint8_t ckA = 0;
    uint8_t ckB = 0;
    const size_t endIdx = 6U + payloadLen;
    for (size_t i = 2U; i < endIdx; ++i) {
        ckA = static_cast<uint8_t>(ckA + frame[i]);
        ckB = static_cast<uint8_t>(ckB + ckA);
    }
    frame[endIdx]     = ckA;
    frame[endIdx + 1] = ckB;
}

uint32_t GnssDriver::utcToEpoch(uint16_t year, uint8_t month, uint8_t day,
                                uint8_t hour, uint8_t minute, uint8_t second) {
    if (year < 2020 || month < 1 || month > 12 || day < 1 || day > 31) {
        return 0;
    }
    // Howard Hinnant civil-from-days inverse algorithm (UTC, leap-year exact)
    int32_t y = static_cast<int32_t>(year);
    const uint32_t m = month;
    const uint32_t d = day;
    y -= (m <= 2) ? 1 : 0;
    const int32_t era = (y >= 0 ? y : y - 399) / 400;
    const uint32_t yoe = static_cast<uint32_t>(y - era * 400);
    const uint32_t doy = (153U * (m + (m > 2 ? -3 : 9)) + 2U) / 5U + d - 1U;
    const uint32_t doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    const int64_t days = static_cast<int64_t>(era) * 146097LL + static_cast<int64_t>(doe) - 719468LL;
    if (days < 0) {
        return 0;
    }
    const int64_t secs = days * 86400LL +
                         static_cast<int64_t>(hour) * 3600LL +
                         static_cast<int64_t>(minute) * 60LL +
                         static_cast<int64_t>(second);
    return (secs > 0 && secs < 0xFFFFFFFFLL) ? static_cast<uint32_t>(secs) : 0U;
}

void GnssDriver::loadNvsState() {
    Preferences prefs;
    if (!prefs.begin(NvsConfig::NAMESPACE, true)) {
        return;
    }

    _savedLatE7      = prefs.getInt(NvsConfig::KEY_LAST_LAT_E7, 0);
    _savedLonE7      = prefs.getInt(NvsConfig::KEY_LAST_LON_E7, 0);
    _savedAltCm      = prefs.getInt(NvsConfig::KEY_LAST_ALT_CM, 0);
    _savedPosUtc     = prefs.getUInt(NvsConfig::KEY_LAST_POS_UTC, 0);
    _hasSavedWarmPos = (_savedLatE7 != 0 || _savedLonE7 != 0);

    const uint8_t  actView    = prefs.getUChar(NvsConfig::KEY_ACT_TRIP_VIEW, static_cast<uint8_t>(TRIP_SINGLE));
    const bool     manActive  = prefs.getBool(NvsConfig::KEY_MAN_TRIP_ACT, false);
    const uint16_t subIdx     = prefs.getUShort(NvsConfig::KEY_SUB_IDX, 0);
    const uint16_t subSplit   = prefs.getUShort(NvsConfig::KEY_SUB_SPLIT_IDX, 1);
    const uint32_t lastDayYmd = prefs.getUInt(NvsConfig::KEY_LAST_DAY_INT, 0);
    const float    dayDistKm  = prefs.getFloat(NvsConfig::KEY_DAY_DIST_KM, 0.0f);
    const float    manDistKm  = prefs.getFloat(NvsConfig::KEY_MAN_DIST_KM, 0.0f);
    prefs.end();

    StateGuard guard(SystemState::instance());
    if (guard) {
        auto& st = SystemState::instance().data();
        if (actView < TRIP_CATEGORY_COUNT) {
            st.tripControl.activeViewCategory = static_cast<TripCategory>(actView);
        }
        st.tripControl.manualTripActive = manActive;
        st.tripControl.manualSubIndex   = subIdx;
        st.tripControl.manualSplitIndex = (subSplit >= 1U) ? subSplit : 1U;
        st.tripControl.lastLocalDayYmd  = lastDayYmd;
        if (isfinite(dayDistKm) && dayDistKm >= 0.0f) {
            st.trips[TRIP_DAILY].tripDistanceKm = dayDistKm;
        }
        if (manActive && isfinite(manDistKm) && manDistKm >= 0.0f) {
            st.trips[TRIP_MANUAL].tripDistanceKm = manDistKm;
        }
    }
}

void GnssDriver::saveWarmStartPosToNvs(int32_t latE7, int32_t lonE7, int32_t altCm, uint32_t utcEpoch) {
    Preferences prefs;
    if (prefs.begin(NvsConfig::NAMESPACE, false)) {
        prefs.putInt(NvsConfig::KEY_LAST_LAT_E7, latE7);
        prefs.putInt(NvsConfig::KEY_LAST_LON_E7, lonE7);
        prefs.putInt(NvsConfig::KEY_LAST_ALT_CM, altCm);
        if (utcEpoch > 0) {
            prefs.putUInt(NvsConfig::KEY_LAST_POS_UTC, utcEpoch);
        }
        prefs.end();
    }
    _savedLatE7      = latE7;
    _savedLonE7      = lonE7;
    _savedAltCm      = altCm;
    if (utcEpoch > 0) {
        _savedPosUtc = utcEpoch;
    }
    _hasSavedWarmPos = true;
}

void GnssDriver::saveTripPersistenceToNvs() {
    uint8_t  actView    = 0;
    bool     manActive  = false;
    uint16_t subIdx     = 0;
    uint16_t subSplit   = 1;
    uint32_t lastDayYmd = 0;
    float    dayDistKm  = 0.0f;
    float    manDistKm  = 0.0f;

    {
        StateGuard guard(SystemState::instance());
        if (!guard) {
            return;
        }
        const auto& st = SystemState::instance().data();
        actView        = static_cast<uint8_t>(st.tripControl.activeViewCategory);
        manActive      = st.tripControl.manualTripActive;
        subIdx         = st.tripControl.manualSubIndex;
        subSplit       = st.tripControl.manualSplitIndex;
        lastDayYmd     = st.tripControl.lastLocalDayYmd;
        dayDistKm      = st.trips[TRIP_DAILY].tripDistanceKm;
        manDistKm      = st.trips[TRIP_MANUAL].tripDistanceKm;
    }

    Preferences prefs;
    if (prefs.begin(NvsConfig::NAMESPACE, false)) {
        prefs.putUChar(NvsConfig::KEY_ACT_TRIP_VIEW, actView);
        prefs.putBool(NvsConfig::KEY_MAN_TRIP_ACT, manActive);
        prefs.putUShort(NvsConfig::KEY_SUB_IDX, subIdx);
        prefs.putUShort(NvsConfig::KEY_SUB_SPLIT_IDX, subSplit);
        prefs.putUInt(NvsConfig::KEY_LAST_DAY_INT, lastDayYmd);
        prefs.putFloat(NvsConfig::KEY_DAY_DIST_KM, dayDistKm);
        prefs.putFloat(NvsConfig::KEY_MAN_DIST_KM, manDistKm);
        prefs.end();
    }
}

bool GnssDriver::probeUartAndHandshake() {
    _serial.setRxBufferSize(2048);

    // Pre-allocate sufficient payload buffer for large UBX-NAV-SAT & CFG-VALSET frames
    _gnss.setPacketCfgPayloadSize(768);

    for (size_t i = 0; i < GnssConfig::UART_CANDIDATE_COUNT; ++i) {
        const int8_t rxPin = GnssConfig::UART_CANDIDATES[i].rxPin;
        const int8_t txPin = GnssConfig::UART_CANDIDATES[i].txPin;

        // 1. Probe operational baudrate 115200 bps first (fast path on warm reboot)
        _serial.end();
        _serial.begin(GnssConfig::BAUD_OPERATIONAL, SERIAL_8N1, rxPin, txPin);
        delay(25);
        while (_serial.available() > 0) {
            (void)_serial.read();
        }
        if (_gnss.begin(_serial, 420, true)) {
            _activeRxPin = rxPin;
            _activeTxPin = txPin;
            return true;
        }

        // 2. Probe NEO-M9N factory baudrate 38400 bps and upgrade UART1 to 115200 bps
        _serial.end();
        _serial.begin(GnssConfig::BAUD_INIT, SERIAL_8N1, rxPin, txPin);
        delay(25);
        while (_serial.available() > 0) {
            (void)_serial.read();
        }
        if (_gnss.begin(_serial, 480, true)) {
            // Switch NEO-M9N UART1 baudrate via UBX-CFG-VALSET (RAM + BBR) + legacy fallback
            _gnss.newCfgValset(VAL_LAYER_RAM | VAL_LAYER_BBR);
            _gnss.addCfgValset32(GnssConfig::KEY_UART1_BAUDRATE, GnssConfig::BAUD_OPERATIONAL);
            (void)_gnss.sendCfgValset(120);
            _gnss.setSerialRate(GnssConfig::BAUD_OPERATIONAL, COM_PORT_UART1, 120);
            _serial.flush();
            delay(60);

            _serial.end();
            _serial.begin(GnssConfig::BAUD_OPERATIONAL, SERIAL_8N1, rxPin, txPin);
            delay(30);
            if (_gnss.begin(_serial, 500, true)) {
                _activeRxPin = rxPin;
                _activeTxPin = txPin;
                return true;
            }
        }
    }

    _serial.end();
    return false;
}

bool GnssDriver::applyM9nConfiguration(uint16_t measRateMs) {
    if (measRateMs < 50U)   measRateMs = 100U;  // Max 20Hz guard; 100ms = 10Hz, 200ms = 5Hz
    if (measRateMs > 1000U) measRateMs = 1000U;
    _measRateMs = measRateMs;

    const uint8_t cyclesPerSec = static_cast<uint8_t>((1000U / measRateMs) > 0U ? (1000U / measRateMs) : 1U);

    // Batch automotive NEO-M9N tuning via official UBX-CFG-VALSET (SPG 4.04 keys)
    _gnss.newCfgValset(VAL_LAYER_RAM);
    _gnss.addCfgValset8(GnssConfig::KEY_UART1OUTPROT_UBX, 1);
    _gnss.addCfgValset8(GnssConfig::KEY_UART1OUTPROT_NMEA, 0);
    _gnss.addCfgValset16(GnssConfig::KEY_RATE_MEAS, _measRateMs);
    _gnss.addCfgValset16(GnssConfig::KEY_RATE_NAV, 1);
    _gnss.addCfgValset8(GnssConfig::KEY_NAVSPG_DYNMODEL, static_cast<uint8_t>(DYN_MODEL_AUTOMOTIVE)); // 4
    _gnss.addCfgValset8(GnssConfig::KEY_MOT_GNSSSPEED_THRS, GnssConfig::STATIC_HOLD_THRS_CMS);        // 50 cm/s = 1.8 km/h
    _gnss.addCfgValset8(GnssConfig::KEY_ITFM_ENABLE, 1);                                              // Enable Jamming Monitor
    _gnss.addCfgValset8(GnssConfig::KEY_NAVSPG_INFIL_MINELEV,
                        static_cast<uint8_t>(GnssConfig::STAGE1_ACQ_MIN_ELEV_DEG));                   // Stage 1: 5 deg
    _gnss.addCfgValset8(GnssConfig::KEY_NAVSPG_INFIL_MINCNO, GnssConfig::STAGE1_ACQ_MIN_CNO_DBHZ);    // Stage 1: 14 dBHz

    // Enable 4-constellation concurrent reception (GPS + BeiDou + GLONASS + Galileo)
    _gnss.addCfgValset8(GnssConfig::KEY_SIGNAL_GPS_ENA, 1);
    _gnss.addCfgValset8(GnssConfig::KEY_SIGNAL_BDS_ENA, 1);
    _gnss.addCfgValset8(GnssConfig::KEY_SIGNAL_GLO_ENA, 1);
    _gnss.addCfgValset8(GnssConfig::KEY_SIGNAL_GAL_ENA, 1);

    // Periodic UBX message output rates on UART1:
    // NAV-PVT @ full nav rate (5Hz/10Hz), NAV-DOP @ 1Hz, NAV-SAT @ 1Hz
    _gnss.addCfgValset8(GnssConfig::KEY_MSGOUT_UBX_NAV_PVT_UART1, 1);
    _gnss.addCfgValset8(GnssConfig::KEY_MSGOUT_UBX_NAV_DOP_UART1, cyclesPerSec);
    _gnss.addCfgValset8(GnssConfig::KEY_MSGOUT_UBX_NAV_SAT_UART1, cyclesPerSec);

    const bool cfgOk = (_gnss.sendCfgValset(350) != 0);

    // Allocate non-blocking auto buffers (implicitUpdate=false -> checkUblox() drives parsing)
    _gnss.assumeAutoPVT(true, false);
    _gnss.assumeAutoDOP(true, false);
    _gnss.assumeAutoNAVSAT(true, false);

    _stage2Active = false;
    return cfgOk;
}

bool GnssDriver::applyElevationCnoFilter(int8_t minElevDeg, uint8_t minCnoDbHz) {
    _gnss.newCfgValset(VAL_LAYER_RAM);
    _gnss.addCfgValset8(GnssConfig::KEY_NAVSPG_INFIL_MINELEV, static_cast<uint8_t>(minElevDeg));
    _gnss.addCfgValset8(GnssConfig::KEY_NAVSPG_INFIL_MINCNO, minCnoDbHz);
    return (_gnss.sendCfgValset(180) != 0);
}

bool GnssDriver::setNavigationRateHz(uint8_t navRateHz) {
    if (navRateHz == 0 || navRateHz > 20) {
        return false;
    }
    ensureMutex();
    UartLock lock(_uartMutex);
    if (!lock || !_uartOnline) {
        return false;
    }
    const uint16_t measMs = static_cast<uint16_t>(1000U / navRateHz);
    _measRateMs           = measMs;

    _gnss.newCfgValset(VAL_LAYER_RAM);
    _gnss.addCfgValset16(GnssConfig::KEY_RATE_MEAS, measMs);
    _gnss.addCfgValset16(GnssConfig::KEY_RATE_NAV, 1);
    _gnss.addCfgValset8(GnssConfig::KEY_MSGOUT_UBX_NAV_DOP_UART1, navRateHz);
    _gnss.addCfgValset8(GnssConfig::KEY_MSGOUT_UBX_NAV_SAT_UART1, navRateHz);
    return (_gnss.sendCfgValset(250) != 0);
}

bool GnssDriver::begin(uint16_t measRateMs) {
    ensureMutex();
    loadNvsState();

    bool ok = false;
    {
        UartLock lock(_uartMutex);
        if (lock) {
            ok = probeUartAndHandshake();
            _uartOnline = ok;
            if (ok) {
                (void)applyM9nConfiguration(measRateMs);
            }
        }
    }

    const uint32_t nowMs = millis();
    _noFixWatchdogMs  = nowMs;
    _lastMonRfPollMs  = nowMs;
    _last1HzTickMs    = nowMs;
    _last30sChartMs   = nowMs;
    _lastPosNvsSaveMs = nowMs;

    {
        StateGuard guard(SystemState::instance());
        if (guard) {
            auto& st                 = SystemState::instance().data();
            st.boot.gnssUartOnline   = _uartOnline;
            st.boot.gnssActiveRxPin  = _activeRxPin;
            st.boot.gnssActiveTxPin  = _activeTxPin;
            st.gnss.stage2TightFilter = false;
        }
    }

    if (_uartOnline) {
        (void)injectWarmStartAssistance();
    }
    return _uartOnline;
}

bool GnssDriver::injectMgaTimeUtc(
    uint16_t year, uint8_t month, uint8_t day,
    uint8_t hour, uint8_t minute, uint8_t second,
    uint16_t tAccSec, uint32_t tAccNs) {

    if (year < 2024 || month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }

    // Authoritative 24-byte payload UBX-MGA-INI-TIME_UTC (0x13 0x40, type=0x10)
    // Total frame = 6B header + 24B payload + 2B checksum = 32 bytes
    uint8_t frame[32] = {};
    frame[0]  = 0xB5; // Sync 1
    frame[1]  = 0x62; // Sync 2
    frame[2]  = 0x13; // Class: MGA
    frame[3]  = 0x40; // ID: MGA-INI
    frame[4]  = 24;   // Payload length LSB (24 bytes)
    frame[5]  = 0x00; // Payload length MSB

    // Payload [0..23] -> frame [6..29]
    frame[6]  = 0x10; // [0] type = 0x10 (TIME_UTC)
    frame[7]  = 0x00; // [1] version = 0x00
    frame[8]  = 0x00; // [2] ref = 0x00 (none / on receipt)
    frame[9]  = static_cast<uint8_t>(GnssConfig::MGA_GPS_LEAP_SECONDS); // [3] leapSecs = 18
    frame[10] = static_cast<uint8_t>(year & 0xFFU);        // [4] year LSB
    frame[11] = static_cast<uint8_t>((year >> 8) & 0xFFU); // [5] year MSB
    frame[12] = month;  // [6]
    frame[13] = day;    // [7]
    frame[14] = hour;   // [8]
    frame[15] = minute; // [9]
    frame[16] = second; // [10]
    frame[17] = 0x00;   // [11] reserved1
    // [12..15] ns = 0 (already zeroed)
    frame[22] = static_cast<uint8_t>(tAccSec & 0xFFU);        // [16] tAccS LSB
    frame[23] = static_cast<uint8_t>((tAccSec >> 8) & 0xFFU); // [17] tAccS MSB
    frame[24] = 0x00;                                         // [18] reserved2 LSB
    frame[25] = 0x00;                                         // [19] reserved2 MSB
    frame[26] = static_cast<uint8_t>(tAccNs & 0xFFU);         // [20] tAccNs byte 0
    frame[27] = static_cast<uint8_t>((tAccNs >> 8) & 0xFFU);  // [21] tAccNs byte 1
    frame[28] = static_cast<uint8_t>((tAccNs >> 16) & 0xFFU); // [22] tAccNs byte 2
    frame[29] = static_cast<uint8_t>((tAccNs >> 24) & 0xFFU); // [23] tAccNs byte 3

    computeUbxChecksum(frame, 24U);
    return pushRawUbx(frame, sizeof(frame));
}

bool GnssDriver::injectMgaPosLlh(int32_t latE7, int32_t lonE7, int32_t altCm, uint32_t posAccCm) {
    if (latE7 == 0 && lonE7 == 0) {
        return false;
    }

    // Authoritative 20-byte payload UBX-MGA-INI-POS_LLH (0x13 0x40, type=0x01)
    // Total frame = 6B header + 20B payload + 2B checksum = 28 bytes
    uint8_t frame[28] = {};
    frame[0] = 0xB5;
    frame[1] = 0x62;
    frame[2] = 0x13;
    frame[3] = 0x40;
    frame[4] = 20;
    frame[5] = 0x00;

    frame[6] = 0x01; // [0] type = 0x01 (POS_LLH)
    frame[7] = 0x00; // [1] version = 0x00
    // [2..3] reserved1 = 0

    auto putI32 = [&](size_t frameOffset, int32_t val) {
        const uint32_t u = static_cast<uint32_t>(val);
        frame[frameOffset]     = static_cast<uint8_t>(u & 0xFFU);
        frame[frameOffset + 1] = static_cast<uint8_t>((u >> 8) & 0xFFU);
        frame[frameOffset + 2] = static_cast<uint8_t>((u >> 16) & 0xFFU);
        frame[frameOffset + 3] = static_cast<uint8_t>((u >> 24) & 0xFFU);
    };
    auto putU32 = [&](size_t frameOffset, uint32_t u) {
        frame[frameOffset]     = static_cast<uint8_t>(u & 0xFFU);
        frame[frameOffset + 1] = static_cast<uint8_t>((u >> 8) & 0xFFU);
        frame[frameOffset + 2] = static_cast<uint8_t>((u >> 16) & 0xFFU);
        frame[frameOffset + 3] = static_cast<uint8_t>((u >> 24) & 0xFFU);
    };

    putI32(10, latE7);     // [4..7]   lat (1e-7 deg)
    putI32(14, lonE7);     // [8..11]  lon (1e-7 deg)
    putI32(18, altCm);     // [12..15] alt (cm)
    putU32(22, posAccCm);  // [16..19] posAcc (cm)

    computeUbxChecksum(frame, 20U);
    return pushRawUbx(frame, sizeof(frame));
}

bool GnssDriver::injectWarmStartAssistance() {
    if (!_uartOnline) {
        return false;
    }

    bool     timeValid   = false;
    ClockSyncSource src  = CLOCK_SRC_UNSYNCED;
    uint16_t uYear       = 2026;
    uint8_t  uMon        = 1;
    uint8_t  uDay        = 1;
    uint8_t  uHour       = 0;
    uint8_t  uMin        = 0;
    uint8_t  uSec        = 0;
    uint32_t curEpoch    = 0;

    {
        StateGuard guard(SystemState::instance());
        if (guard) {
            const auto& td = SystemState::instance().data().time;
            if (td.isCalibrated && td.utcYear >= 2025) {
                timeValid = true;
                src       = td.source;
                uYear     = td.utcYear;
                uMon      = td.utcMonth;
                uDay      = td.utcDay;
                uHour     = td.utcHour;
                uMin      = td.utcMinute;
                uSec      = td.utcSecond;
                curEpoch  = td.epochUtc;
            }
        }
    }

    if (!timeValid) {
        const time_t nowEpoch = time(nullptr);
        if (nowEpoch > 1735689600) { // >= 2025-01-01 UTC
            struct tm utcTm = {};
            gmtime_r(&nowEpoch, &utcTm);
            timeValid = true;
            src       = CLOCK_SRC_RTC;
            uYear     = static_cast<uint16_t>(utcTm.tm_year + 1900);
            uMon      = static_cast<uint8_t>(utcTm.tm_mon + 1);
            uDay      = static_cast<uint8_t>(utcTm.tm_mday);
            uHour     = static_cast<uint8_t>(utcTm.tm_hour);
            uMin      = static_cast<uint8_t>(utcTm.tm_min);
            uSec      = static_cast<uint8_t>(utcTm.tm_sec);
            curEpoch  = static_cast<uint32_t>(nowEpoch);
        }
    }

    bool anySent = false;
    if (timeValid) {
        const uint16_t tAccS = (src == CLOCK_SRC_NTP || src == CLOCK_SRC_GNSS)
                                   ? GnssConfig::MGA_NTP_TACC_SEC
                                   : GnssConfig::MGA_RTC_TACC_SEC;
        const uint32_t tAccNs = (src == CLOCK_SRC_NTP || src == CLOCK_SRC_GNSS)
                                    ? GnssConfig::MGA_NTP_TACC_NS
                                    : GnssConfig::MGA_RTC_TACC_NS;
        anySent = injectMgaTimeUtc(uYear, uMon, uDay, uHour, uMin, uSec, tAccS, tAccNs) || anySent;
        delay(6);
    }

    if (_hasSavedWarmPos) {
        uint32_t posAccCm = GnssConfig::MGA_POS_ACC_STALE_CM;
        if (curEpoch > 0 && _savedPosUtc > 0 && curEpoch >= _savedPosUtc) {
            if ((curEpoch - _savedPosUtc) <= GnssConfig::MGA_POS_FRESH_MAX_AGE_SEC) {
                posAccCm = GnssConfig::MGA_POS_ACC_FRESH_CM;
            }
        }
        anySent = injectMgaPosLlh(_savedLatE7, _savedLonE7, _savedAltCm, posAccCm) || anySent;
    }
    return anySent;
}

bool GnssDriver::pushRawUbx(const uint8_t* data, size_t len) {
    if (data == nullptr || len == 0) {
        return false;
    }
    ensureMutex();
    UartLock lock(_uartMutex);
    if (!lock || !_uartOnline) {
        return false;
    }
    return _gnss.pushRawData(const_cast<uint8_t*>(data), len);
}

void GnssDriver::resetAssistNowStream() {
    ensureMutex();
    UartLock lock(_uartMutex);
    _mgaCarryLen = 0;
}

size_t GnssDriver::pushAssistNowChunk(const uint8_t* data, size_t len, bool skipTime) {
    if (data == nullptr || len == 0 || !_uartOnline) {
        return 0;
    }

    size_t writtenTotal = 0;
    size_t idx          = 0;

    while (idx < len) {
        // Fill carryover buffer or parse complete UBX frames (0xB5 0x62 cls id lenL lenH payload ckA ckB)
        if (_mgaCarryLen > 0) {
            // Ensure header (6 bytes) is complete
            while (_mgaCarryLen < 6U && idx < len) {
                _mgaCarry[_mgaCarryLen++] = data[idx++];
            }
            if (_mgaCarryLen < 6U) {
                break;
            }
            if (_mgaCarry[0] != 0xB5U || _mgaCarry[1] != 0x62U) {
                // Resynchronize if stream lost sync
                _mgaCarryLen = 0;
                continue;
            }
            const size_t payLen   = static_cast<size_t>(_mgaCarry[4]) |
                                    (static_cast<size_t>(_mgaCarry[5]) << 8);
            const size_t frameLen = 8U + payLen;
            if (frameLen > MGA_CARRY_CAP) {
                // Oversized frame: stream directly
                if (pushRawUbx(_mgaCarry, _mgaCarryLen)) {
                    writtenTotal += _mgaCarryLen;
                }
                _mgaCarryLen = 0;
                continue;
            }
            const size_t need = frameLen - _mgaCarryLen;
            const size_t take = (len - idx < need) ? (len - idx) : need;
            memcpy(_mgaCarry + _mgaCarryLen, data + idx, take);
            _mgaCarryLen += take;
            idx          += take;

            if (_mgaCarryLen == frameLen) {
                const bool isMgaTime = (_mgaCarry[2] == 0x13U && _mgaCarry[3] == 0x40U &&
                                        frameLen >= 8U && (_mgaCarry[6] == 0x10U || _mgaCarry[6] == 0x11U));
                if (!(skipTime && isMgaTime)) {
                    if (pushRawUbx(_mgaCarry, frameLen)) {
                        writtenTotal += frameLen;
                    }
                    vTaskDelay(pdMS_TO_TICKS(4));
                }
                _mgaCarryLen = 0;
            }
            continue;
        }

        // Scan for next UBX 0xB5 0x62 sync header directly inside data[]
        if (data[idx] != 0xB5U) {
            idx++;
            continue;
        }
        if (idx + 6U > len) {
            const size_t rem = len - idx;
            memcpy(_mgaCarry, data + idx, rem);
            _mgaCarryLen = rem;
            break;
        }
        if (data[idx + 1] != 0x62U) {
            idx++;
            continue;
        }

        const size_t payLen   = static_cast<size_t>(data[idx + 4]) |
                                (static_cast<size_t>(data[idx + 5]) << 8);
        const size_t frameLen = 8U + payLen;

        if (idx + frameLen <= len) {
            const bool isMgaTime = (data[idx + 2] == 0x13U && data[idx + 3] == 0x40U &&
                                    payLen >= 1U && (data[idx + 6] == 0x10U || data[idx + 6] == 0x11U));
            if (!(skipTime && isMgaTime)) {
                if (pushRawUbx(data + idx, frameLen)) {
                    writtenTotal += frameLen;
                }
                vTaskDelay(pdMS_TO_TICKS(4));
            }
            idx += frameLen;
        } else if (frameLen <= MGA_CARRY_CAP) {
            const size_t rem = len - idx;
            memcpy(_mgaCarry, data + idx, rem);
            _mgaCarryLen = rem;
            break;
        } else {
            // Very large raw frame: write remainder directly
            const size_t rem = len - idx;
            if (pushRawUbx(data + idx, rem)) {
                writtenTotal += rem;
            }
            break;
        }
    }

    return writtenTotal;
}

void GnssDriver::flushAssistNowStream() {
    if (_mgaCarryLen > 0) {
        (void)pushRawUbx(_mgaCarry, _mgaCarryLen);
        _mgaCarryLen = 0;
    }
}

void GnssDriver::triggerHotStartReset(uint32_t nowMs) {
    {
        UartLock lock(_uartMutex);
        if (lock && _uartOnline) {
            // UBX-CFG-RST (0x06 0x04): navBbrMask=0x0000 (Hotstart), resetMode=0x02 (GNSS-only)
            _gnss.softwareResetGNSSOnly();
        }
    }

    vTaskDelay(pdMS_TO_TICKS(60));

    {
        UartLock lock(_uartMutex);
        if (lock && _uartOnline) {
            (void)applyElevationCnoFilter(
                GnssConfig::STAGE1_ACQ_MIN_ELEV_DEG,
                GnssConfig::STAGE1_ACQ_MIN_CNO_DBHZ
            );
        }
    }

    _stage2Active    = false;
    _stable3dStartMs = 0;
    _noFixWatchdogMs = nowMs;

    {
        StateGuard guard(SystemState::instance());
        if (guard) {
            auto& gnss             = SystemState::instance().data().gnss;
            gnss.hotRestartCount  += 1U;
            gnss.stage2TightFilter = false;
        }
    }

    // Re-inject latest time & position assistance immediately after GNSS-only hot restart
    (void)injectWarmStartAssistance();
}

void GnssDriver::processNavDop() {
    if (_gnss.packetUBXNAVDOP == nullptr) {
        return;
    }
    const UBX_NAV_DOP_data_t& dop = _gnss.packetUBXNAVDOP->data;
    if (dop.hDOP > 0) _latestHdop = static_cast<float>(dop.hDOP) * 0.01f;
    if (dop.vDOP > 0) _latestVdop = static_cast<float>(dop.vDOP) * 0.01f;
    if (dop.pDOP > 0) _latestPdop = static_cast<float>(dop.pDOP) * 0.01f;
    _gnss.flushDOP();

    StateGuard guard(SystemState::instance());
    if (guard) {
        auto& gnss = SystemState::instance().data().gnss;
        gnss.hdop  = _latestHdop;
        gnss.vdop  = _latestVdop;
        gnss.pdop  = _latestPdop;
    }
}

void GnssDriver::processNavSat() {
    if (_gnss.packetUBXNAVSAT == nullptr) {
        return;
    }
    const UBX_NAV_SAT_data_t& navSat = _gnss.packetUBXNAVSAT->data;
    const uint8_t totalSvs = navSat.header.numSvs;

    uint8_t usedBds = 0, usedGps = 0, usedGlo = 0, usedGal = 0;
    uint8_t viewBds = 0, viewGps = 0, viewGlo = 0, viewGal = 0;
    uint8_t inViewTotal = 0;

    // Collect and rank satellites (used in fix first, then highest C/N0) up to MAX_TRACKED_SATS (32)
    SatInfo topSats[GnssData::MAX_TRACKED_SATS] = {};
    uint8_t topCount = 0;

    for (uint8_t i = 0; i < totalSvs; ++i) {
        const UBX_NAV_SAT_block_t& b = navSat.blocks[i];
        const bool svUsed  = (b.flags.bits.svUsed != 0);
        const bool healthy = (b.flags.bits.health != 2U);
        const bool visible = (b.cno > 0U) || (b.elev >= 0) || svUsed;

        if (visible) {
            if (inViewTotal < 255U) inViewTotal++;
            switch (b.gnssId) {
                case GNSS_CONST_GPS:     if (viewGps < 255U) viewGps++; break;
                case GNSS_CONST_GALILEO: if (viewGal < 255U) viewGal++; break;
                case GNSS_CONST_BEIDOU:  if (viewBds < 255U) viewBds++; break;
                case GNSS_CONST_GLONASS: if (viewGlo < 255U) viewGlo++; break;
                default: break;
            }
        }

        if (svUsed) {
            switch (b.gnssId) {
                case GNSS_CONST_GPS:     if (usedGps < 255U) usedGps++; break;
                case GNSS_CONST_GALILEO: if (usedGal < 255U) usedGal++; break;
                case GNSS_CONST_BEIDOU:  if (usedBds < 255U) usedBds++; break;
                case GNSS_CONST_GLONASS: if (usedGlo < 255U) usedGlo++; break;
                default: break;
            }
        }

        if (!visible) {
            continue;
        }

        SatInfo item    = {};
        item.gnssId     = b.gnssId;
        item.svId       = b.svId;
        item.cno        = b.cno;
        item.elev       = b.elev;
        item.azim       = b.azim;
        item.qualityInd = static_cast<uint8_t>(b.flags.bits.qualityInd);
        item.healthy    = healthy;
        item.used       = svUsed;

        const uint16_t score = static_cast<uint16_t>(item.cno) + (item.used ? 100U : 0U);
        if (topCount < GnssData::MAX_TRACKED_SATS) {
            topSats[topCount++] = item;
        } else {
            // Replace weakest entry if new satellite has stronger score
            uint8_t  worstIdx   = 0;
            uint16_t worstScore = 9999U;
            for (uint8_t k = 0; k < topCount; ++k) {
                const uint16_t ks = static_cast<uint16_t>(topSats[k].cno) + (topSats[k].used ? 100U : 0U);
                if (ks < worstScore) {
                    worstScore = ks;
                    worstIdx   = k;
                }
            }
            if (score > worstScore) {
                topSats[worstIdx] = item;
            }
        }
    }

    // Sort topSats descending by (used, cno) for clean C/N0 bar chart rendering
    for (uint8_t i = 0; i + 1U < topCount; ++i) {
        for (uint8_t j = i + 1U; j < topCount; ++j) {
            const uint16_t si = static_cast<uint16_t>(topSats[i].cno) + (topSats[i].used ? 100U : 0U);
            const uint16_t sj = static_cast<uint16_t>(topSats[j].cno) + (topSats[j].used ? 100U : 0U);
            if (sj > si) {
                const SatInfo tmp = topSats[i];
                topSats[i]        = topSats[j];
                topSats[j]        = tmp;
            }
        }
    }

    _latestSatsInView = inViewTotal;
    _gnss.flushNAVSAT();

    StateGuard guard(SystemState::instance());
    if (guard) {
        auto& gnss          = SystemState::instance().data().gnss;
        gnss.satsInView     = inViewTotal;
        gnss.numBdsSats     = usedBds;
        gnss.numGpsSats     = usedGps;
        gnss.numGloSats     = usedGlo;
        gnss.numGalSats     = usedGal;
        gnss.viewBdsSats    = viewBds;
        gnss.viewGpsSats    = viewGps;
        gnss.viewGloSats    = viewGlo;
        gnss.viewGalSats    = viewGal;
        gnss.activeSatCount = topCount;
        memcpy(gnss.sats, topSats, sizeof(topSats));
    }
}

void GnssDriver::processNavPvt(uint32_t nowMs) {
    if (_gnss.packetUBXNAVPVT == nullptr) {
        return;
    }
    const UBX_NAV_PVT_data_t pvt = _gnss.packetUBXNAVPVT->data;
    _gnss.flushPVT();

    const GnssFixType fixType = (pvt.fixType <= 5U)
                                    ? static_cast<GnssFixType>(pvt.fixType)
                                    : FIX_NONE;
    const bool fixOk      = (pvt.flags.bits.gnssFixOK != 0);
    const bool invalidLlh = (pvt.flags3.bits.invalidLlh != 0);
    const bool valid3d    = (fixType >= FIX_3D && fixType <= FIX_GNSS_DR) && fixOk && !invalidLlh;

    const float rawSpdKmh = (pvt.gSpeed > 0) ? (static_cast<float>(pvt.gSpeed) * 0.0036f) : 0.0f;
    const bool  staticHold = (rawSpdKmh <= GnssConfig::STATIC_DEADBAND_KMH);
    const float cleanSpdKmh = staticHold ? 0.0f : rawSpdKmh;

    float rawCourseDeg = static_cast<float>(pvt.headMot) * 1e-5f;
    rawCourseDeg       = fmodf(rawCourseDeg, 360.0f);
    if (rawCourseDeg < 0.0f) rawCourseDeg += 360.0f;

    const float pvtPdop = (pvt.pDOP > 0) ? (static_cast<float>(pvt.pDOP) * 0.01f) : _latestPdop;
    _latestPdop         = pvtPdop;

    const double rawLat  = static_cast<double>(pvt.lat) * 1e-7;
    const double rawLon  = static_cast<double>(pvt.lon) * 1e-7;
    const float  rawAltM = static_cast<float>(pvt.hMSL) * 0.001f;
    const float  ellipM  = static_cast<float>(pvt.height) * 0.001f;
    const float  hAccM   = static_cast<float>(pvt.hAcc) * 0.001f;
    const float  vAccM   = static_cast<float>(pvt.vAcc) * 0.001f;

    const int32_t msFrac = (pvt.nano >= 0) ? (pvt.nano / 1000000) : 0;
    const uint16_t utcMs = static_cast<uint16_t>((msFrac >= 0 && msFrac < 1000) ? msFrac : 0);

    // Kinematic Outlier Rejection on valid 3D Fixes
    if (valid3d) {
        if (_hasPrevKinFix && nowMs > _prevKinMs) {
            const float dtSec = static_cast<float>(nowMs - _prevKinMs) * 0.001f;
            if (dtSec >= 0.04f && dtSec <= 5.0f) {
                const float speedJumpPerSec = fabsf(cleanSpdKmh - _prevKinSpeedKmh) / dtSec;
                const float accelG = (fabsf(cleanSpdKmh - _prevKinSpeedKmh) / 3.6f) / (dtSec * GRAVITY_MPS2);
                const double distJumpM = TunnelService::haversineMeters(
                    _prevKinLat, _prevKinLon, rawLat, rawLon
                );
                const double impliedSpeedKmh = (distJumpM / static_cast<double>(dtSec)) * 3.6;

                const bool isOutlier =
                    (speedJumpPerSec > GnssConfig::KINEMATIC_MAX_SPEED_JUMP_KMH) ||
                    (accelG > GnssConfig::KINEMATIC_MAX_ACCEL_G && dtSec <= 1.2f) ||
                    (impliedSpeedKmh > 320.0 && distJumpM > 60.0);

                if (isOutlier) {
                    StateGuard guard(SystemState::instance());
                    if (guard) {
                        SystemState::instance().data().gnss.outlierDropCount += 1U;
                    }
                    return;
                }
            }
        }
        _hasPrevKinFix   = true;
        _prevKinLat      = rawLat;
        _prevKinLon      = rawLon;
        _prevKinSpeedKmh = cleanSpdKmh;
        _prevKinMs       = nowMs;
    }

    // Route valid 3D Fix through DeadReckoningService for 2s smooth tunnel-exit blending
    double pubLat     = rawLat;
    double pubLon     = rawLon;
    float  pubAltM    = rawAltM;
    float  pubHeading = DeadReckoningService::instance().fusedHeadingDeg();

    if (valid3d) {
        DeadReckoningService::instance().processValid3dFix(
            rawLat, rawLon, rawAltM, cleanSpdKmh, rawCourseDeg, nowMs,
            pubLat, pubLon, pubAltM, pubHeading
        );
        _noFixWatchdogMs = nowMs;
    }

    // Periodically persist warm-start position to NVS (every 5 minutes)
    uint32_t fixEpochUtc = 0;
    if (pvt.valid.bits.validDate && pvt.valid.bits.validTime) {
        fixEpochUtc = utcToEpoch(pvt.year, pvt.month, pvt.day, pvt.hour, pvt.min, pvt.sec);
    }

    if (valid3d && (!_hasSavedWarmPos || (nowMs - _lastPosNvsSaveMs) >= POS_NVS_SAVE_MS)) {
        _lastPosNvsSaveMs = nowMs;
        saveWarmStartPosToNvs(
            pvt.lat,
            pvt.lon,
            static_cast<int32_t>(llround(static_cast<double>(rawAltM) * 100.0)),
            fixEpochUtc
        );
    }

    // Publish atomic NAV-PVT telemetry to SystemState::gnss
    {
        StateGuard guard(SystemState::instance());
        if (guard) {
            auto& st   = SystemState::instance().data();
            auto& gnss = st.gnss;

            gnss.fixType      = fixType;
            gnss.gnssFixOk    = fixOk;
            gnss.numSats      = pvt.numSV;
            gnss.pdop         = pvtPdop;
            gnss.hdop         = _latestHdop;
            gnss.vdop         = _latestVdop;
            gnss.hAccMeters   = hAccM;
            gnss.vAccMeters   = vAccM;
            gnss.gpsCourseDeg = rawCourseDeg;

            gnss.utcDateValid = (pvt.valid.bits.validDate != 0);
            gnss.utcTimeValid = (pvt.valid.bits.validTime != 0);
            if (gnss.utcDateValid) {
                gnss.year  = pvt.year;
                gnss.month = pvt.month;
                gnss.day   = pvt.day;
            }
            if (gnss.utcTimeValid) {
                gnss.hour        = pvt.hour;
                gnss.minute      = pvt.min;
                gnss.second      = pvt.sec;
                gnss.millisecond = utcMs;
            }

            if (valid3d) {
                gnss.isValid          = true;
                gnss.isDeadReckoning  = false;
                gnss.drDurationSec    = 0;
                gnss.latitude         = pubLat;
                gnss.longitude        = pubLon;
                gnss.latE7            = static_cast<int32_t>(llround(pubLat * 1e7));
                gnss.lonE7            = static_cast<int32_t>(llround(pubLon * 1e7));
                gnss.altitudeMeters   = pubAltM;
                gnss.ellipsoidHeightM = ellipM;
                gnss.rawSpeedKmh      = rawSpdKmh;
                gnss.speedKmh         = cleanSpdKmh;
                gnss.staticHoldLocked = staticHold;
                gnss.headingDeg       = pubHeading;
                gnss.lastFixMillis    = nowMs;
            } else if (!DeadReckoningService::instance().isDeadReckoning()) {
                // Before first 3D anchor is ever acquired
                gnss.isValid          = false;
                gnss.rawSpeedKmh      = 0.0f;
                gnss.speedKmh         = 0.0f;
                gnss.staticHoldLocked = true;
            }
        }
    }
}

void GnssDriver::pollMonRf(uint32_t nowMs) {
    if ((nowMs - _lastMonRfPollMs) < GnssConfig::MON_RF_POLL_MS) {
        return;
    }
    _lastMonRfPollMs = nowMs;

    UBX_MON_RF_data_t rfData = {};
    bool rfOk = false;
    {
        UartLock lock(_uartMutex);
        if (lock && _uartOnline) {
            rfOk = _gnss.getRFinformation(&rfData, 120);
        }
    }

    if (!rfOk || rfData.header.nBlocks == 0) {
        return;
    }

    // Parse Block 0 (L1 GNSS RF Front-End) strictly per official u-blox M9 SPG 4.04 spec (#3)
    const UBX_MON_RF_block_t& b0 = rfData.blocks[0];
    const uint8_t jamBits = static_cast<uint8_t>(b0.flags.bits.jammingState & 0x03U);
    const RfJammingState jamState = static_cast<RfJammingState>(jamBits);
    const GnssAntennaStatus antStat = (b0.antStatus <= 4U)
                                          ? static_cast<GnssAntennaStatus>(b0.antStatus)
                                          : ANT_DONTKNOW;

    StateGuard guard(SystemState::instance());
    if (guard) {
        auto& gnss              = SystemState::instance().data().gnss;
        gnss.jammingState       = jamState;
        gnss.antennaStatus      = antStat;
        gnss.antennaPower       = b0.antPower;
        gnss.rfNoiseLevel       = b0.noisePerMS;
        gnss.rfAgcCount         = b0.agcCnt;
        gnss.cwJammingIndicator = b0.jamInd;
    }
}

void GnssDriver::handleTripCommandsAndRollover(uint32_t /*nowMs*/) {
    bool doFullReset       = false;
    bool doStartManual     = false;
    bool doStopManual      = false;
    bool doMidnightRoll    = false;
    uint32_t curLocalYmd   = 0;
    FullTripResetPayload resetPayload = {};

    double curLat   = 0.0;
    double curLon   = 0.0;
    bool   validPos = false;
    float  curAltM  = 0.0f;
    float  curSpd   = 0.0f;
    uint32_t nowUtc = 0;

    {
        StateGuard guard(SystemState::instance());
        if (!guard) {
            return;
        }
        auto& st = SystemState::instance().data();
        validPos = st.gnss.isValid;
        curLat   = st.gnss.latitude;
        curLon   = st.gnss.longitude;
        curAltM  = (st.baro.isOnline || st.baro.isP0Calibrated)
                       ? st.baro.fusedAltitudeM
                       : st.gnss.altitudeMeters;
        curSpd   = st.gnss.speedKmh;

        if (st.time.epochUtc > 0) {
            nowUtc = st.time.epochUtc;
        } else if (st.gnss.utcDateValid && st.gnss.utcTimeValid) {
            nowUtc = utcToEpoch(st.gnss.year, st.gnss.month, st.gnss.day,
                                st.gnss.hour, st.gnss.minute, st.gnss.second);
        }

        // Compute local YYYYMMDD for automatic midnight daily-trip rollover
        if (st.time.isCalibrated && st.time.localYear >= 2025) {
            curLocalYmd = static_cast<uint32_t>(st.time.localYear) * 10000U +
                          static_cast<uint32_t>(st.time.localMonth) * 100U +
                          static_cast<uint32_t>(st.time.localDay);
        } else if (nowUtc >= 1735689600UL) {
            const time_t localEpoch = static_cast<time_t>(nowUtc) +
                                      static_cast<time_t>(st.time.localTzOffsetMin) * 60;
            struct tm ltm = {};
            gmtime_r(&localEpoch, &ltm);
            curLocalYmd = static_cast<uint32_t>(ltm.tm_year + 1900) * 10000U +
                          static_cast<uint32_t>(ltm.tm_mon + 1) * 100U +
                          static_cast<uint32_t>(ltm.tm_mday);
        }

        if (curLocalYmd >= 20250101UL) {
            if (st.tripControl.lastLocalDayYmd == 0) {
                st.tripControl.lastLocalDayYmd = curLocalYmd;
            } else if (curLocalYmd != st.tripControl.lastLocalDayYmd) {
                st.tripControl.lastLocalDayYmd = curLocalYmd;
                st.trips[TRIP_DAILY].resetWithAnchor(nowUtc, curLat, curLon, validPos, curAltM, curSpd);
                doMidnightRoll = true;
            }
        }

        if (st.tripControl.cmdFullTripReset) {
            doFullReset                   = true;
            resetPayload.timestampUtc     = nowUtc;
            resetPayload.newStartLatitude = validPos ? curLat : 0.0;
            resetPayload.newStartLongitude= validPos ? curLon : 0.0;
            resetPayload.currentAltitudeM = curAltM;
            resetPayload.currentSpeedKmh  = curSpd;
        }

        if (st.tripControl.cmdStartManualTrip) {
            st.tripControl.cmdStartManualTrip = false;
            st.tripControl.manualTripActive   = true;
            st.tripControl.manualSubIndex     = static_cast<uint16_t>(st.tripControl.manualSubIndex + 1U);
            st.tripControl.manualSplitIndex   = 1U;
            st.tripControl.activeViewCategory = TRIP_MANUAL;
            st.trips[TRIP_MANUAL].resetWithAnchor(nowUtc, curLat, curLon, validPos, curAltM, curSpd);
            doStartManual = true;
        }

        if (st.tripControl.cmdStopManualTrip) {
            st.tripControl.cmdStopManualTrip = false;
            st.tripControl.manualTripActive  = false;
            if (st.tripControl.activeViewCategory == TRIP_MANUAL) {
                st.tripControl.activeViewCategory = TRIP_SINGLE;
            }
            doStopManual = true;
        }
    }

    if (doFullReset) {
        SystemState::instance().executeFullTripReset(resetPayload);
        _trailHasLast[TRIP_SINGLE] = validPos;
        if (validPos) {
            _trailLastLat[TRIP_SINGLE] = curLat;
            _trailLastLon[TRIP_SINGLE] = curLon;
        }
    }

    if (doMidnightRoll) {
        SystemState::instance().resetTrail(TRIP_DAILY);
        _trailHasLast[TRIP_DAILY] = false;
        if (validPos) {
            SystemState::instance().appendTrailPoint(
                TRIP_DAILY,
                static_cast<int32_t>(llround(curLat * 1e7)),
                static_cast<int32_t>(llround(curLon * 1e7)),
                0.0f, 0.0f
            );
            _trailLastLat[TRIP_DAILY] = curLat;
            _trailLastLon[TRIP_DAILY] = curLon;
            _trailHasLast[TRIP_DAILY] = true;
        }
        saveTripPersistenceToNvs();
    }

    if (doStartManual) {
        SystemState::instance().resetTrail(TRIP_MANUAL);
        _trailHasLast[TRIP_MANUAL] = false;
        if (validPos) {
            SystemState::instance().appendTrailPoint(
                TRIP_MANUAL,
                static_cast<int32_t>(llround(curLat * 1e7)),
                static_cast<int32_t>(llround(curLon * 1e7)),
                0.0f, 0.0f
            );
            _trailLastLat[TRIP_MANUAL] = curLat;
            _trailLastLon[TRIP_MANUAL] = curLon;
            _trailHasLast[TRIP_MANUAL] = true;
        }
        saveTripPersistenceToNvs();
    } else if (doStopManual) {
        saveTripPersistenceToNvs();
    }
}

void GnssDriver::updateTripOdometerAndTrails(uint32_t nowMs) {
    bool     posValid   = false;
    double   curLat     = 0.0;
    double   curLon     = 0.0;
    int32_t  curLatE7   = 0;
    int32_t  curLonE7   = 0;
    float    speedKmh   = 0.0f;
    float    fusedAltM  = 0.0f;
    bool     manActive  = false;
    uint32_t nowUtc     = 0;

    {
        StateGuard guard(SystemState::instance());
        if (!guard) {
            return;
        }
        const auto& st = SystemState::instance().data();
        posValid       = st.gnss.isValid;
        curLat         = st.gnss.latitude;
        curLon         = st.gnss.longitude;
        curLatE7       = st.gnss.latE7;
        curLonE7       = st.gnss.lonE7;
        speedKmh       = st.gnss.speedKmh;
        fusedAltM      = (st.baro.isOnline || st.baro.isP0Calibrated)
                             ? st.baro.fusedAltitudeM
                             : st.gnss.altitudeMeters;
        manActive      = st.tripControl.manualTripActive;
        nowUtc         = st.time.epochUtc;
    }

    // 1. Odometer distance step integration whenever valid/DR position advances
    float stepDistKm = 0.0f;
    if (posValid) {
        if (!_hasOdoAnchor) {
            _odoPrevLat   = curLat;
            _odoPrevLon   = curLon;
            _hasOdoAnchor = true;
        } else if (speedKmh > GnssConfig::STATIC_DEADBAND_KMH) {
            const double dMeters = TunnelService::haversineMeters(_odoPrevLat, _odoPrevLon, curLat, curLon);
            if (dMeters >= 0.4 && dMeters <= 120.0) {
                stepDistKm  = static_cast<float>(dMeters * 0.001);
                _odoPrevLat = curLat;
                _odoPrevLon = curLon;
            } else if (dMeters > 120.0) {
                // Re-anchor without adding spike if position jumped across a cold gap
                _odoPrevLat = curLat;
                _odoPrevLon = curLon;
            }
        } else {
            _odoPrevLat = curLat;
            _odoPrevLon = curLon;
        }
    }

    // 2. 1Hz duration buckets, max/avg speed & PSRAM trail point appends
    const bool run1Hz = (nowMs - _last1HzTickMs) >= 1000UL;
    if (run1Hz) {
        _last1HzTickMs += 1000UL;
        if ((nowMs - _last1HzTickMs) >= 2000UL) {
            _last1HzTickMs = nowMs;
        }
    }

    // 3. 30-second HistoryBuffer pushes (120 samples = 60-min rolling window)
    const bool run30s = (nowMs - _last30sChartMs) >= HISTORY_PUSH_MS;
    if (run30s) {
        _last30sChartMs = nowMs;
    }

    double anchorLat[TRIP_CATEGORY_COUNT] = {};
    double anchorLon[TRIP_CATEGORY_COUNT] = {};
    bool   hasAnchor[TRIP_CATEGORY_COUNT] = {};

    if (stepDistKm > 0.0f || run1Hz || run30s) {
        StateGuard guard(SystemState::instance());
        if (guard) {
            auto& st          = SystemState::instance().data();
            const bool moving = posValid && (speedKmh > GnssConfig::STATIC_DEADBAND_KMH);

            for (uint8_t cat = 0; cat < TRIP_CATEGORY_COUNT; ++cat) {
                if (cat == TRIP_MANUAL && !st.tripControl.manualTripActive) {
                    continue;
                }
                TripStats& t = st.trips[cat];

                if (posValid && !t.hasStartAnchor) {
                    t.hasStartAnchor    = true;
                    t.startLatitude     = curLat;
                    t.startLongitude    = curLon;
                    if (t.startTimestampUtc == 0) {
                        t.startTimestampUtc = nowUtc;
                    }
                }

                if (stepDistKm > 0.0f) {
                    t.tripDistanceKm += stepDistKm;
                }

                if (speedKmh > t.maxSpeedKmh) {
                    t.maxSpeedKmh = speedKmh;
                }

                if (run1Hz && posValid) {
                    if (moving) {
                        t.movingDurationSec += 1U;
                        if (speedKmh > 80.0f) {
                            t.highwayMovingSec += 1U;
                        } else if (speedKmh > 40.0f) {
                            t.urbanMovingSec += 1U;
                        } else {
                            t.slowMovingSec += 1U;
                        }
                    } else {
                        t.idleDurationSec += 1U;
                    }

                    if (t.movingDurationSec > 0) {
                        t.avgSpeedKmh = t.tripDistanceKm /
                                        (static_cast<float>(t.movingDurationSec) / 3600.0f);
                    }
                }

                if (run30s) {
                    t.altitudeHistory.push(fusedAltM);
                    t.speedHistory.push(speedKmh);
                }

                hasAnchor[cat] = t.hasStartAnchor;
                anchorLat[cat] = t.startLatitude;
                anchorLon[cat] = t.startLongitude;
            }
        }
    }

    // Append PSRAM vector trail points (with straight-line distance & return bearing to start)
    if (run1Hz && posValid) {
        for (uint8_t cat = 0; cat < TRIP_CATEGORY_COUNT; ++cat) {
            if (cat == TRIP_MANUAL && !manActive) {
                continue;
            }
            const bool needPoint =
                !_trailHasLast[cat] ||
                (speedKmh > GnssConfig::STATIC_DEADBAND_KMH &&
                 TunnelService::haversineMeters(_trailLastLat[cat], _trailLastLon[cat], curLat, curLon) >=
                     TRAIL_MIN_STEP_METERS);

            if (needPoint) {
                float straightDistKm = 0.0f;
                float returnBearDeg  = 0.0f;
                if (hasAnchor[cat]) {
                    const double dStartM = TunnelService::haversineMeters(
                        curLat, curLon, anchorLat[cat], anchorLon[cat]
                    );
                    straightDistKm = static_cast<float>(dStartM * 0.001);
                    if (dStartM >= 5.0) {
                        returnBearDeg = TunnelService::forwardBearingDeg(
                            curLat, curLon, anchorLat[cat], anchorLon[cat]
                        );
                    }
                }
                SystemState::instance().appendTrailPoint(
                    static_cast<TripCategory>(cat),
                    curLatE7,
                    curLonE7,
                    straightDistKm,
                    returnBearDeg
                );
                _trailLastLat[cat] = curLat;
                _trailLastLon[cat] = curLon;
                _trailHasLast[cat] = true;
            }
        }
    }

    if ((nowMs - _lastTripNvsSaveMs) >= TRIP_NVS_SAVE_MS) {
        _lastTripNvsSaveMs = nowMs;
        saveTripPersistenceToNvs();
    }
}

void GnssDriver::update(uint32_t nowMs) {
    handleTripCommandsAndRollover(nowMs);

    if (_uartOnline) {
        bool newDop = false;
        bool newSat = false;
        bool newPvt = false;

        {
            UartLock lock(_uartMutex, pdMS_TO_TICKS(10));
            if (lock) {
                _gnss.checkUblox();
                newDop = _gnss.getDOP(0);
                newSat = _gnss.getNAVSAT(0);
                newPvt = _gnss.getPVT(0);
            }
        }

        if (newDop) processNavDop();
        if (newSat) processNavSat();
        if (newPvt) processNavPvt(nowMs);

        // Two-Stage Dynamic Elevation / C/N0 Filter & 45s Hotstart Watchdog
        bool    valid3d = false;
        uint8_t numSats = 0;
        uint8_t inView  = _latestSatsInView;

        {
            StateGuard guard(SystemState::instance());
            if (guard) {
                const auto& gnss = SystemState::instance().data().gnss;
                valid3d = (gnss.fixType >= FIX_3D && gnss.fixType <= FIX_GNSS_DR) &&
                          gnss.gnssFixOk && !gnss.isDeadReckoning;
                numSats = gnss.numSats;
                inView  = gnss.satsInView;
            }
        }

        if (valid3d) {
            _lost3dStartMs   = 0;
            _noFixWatchdogMs = nowMs;

            if (numSats >= GnssConfig::STAGE2_MIN_SATS) {
                if (_stable3dStartMs == 0) {
                    _stable3dStartMs = nowMs;
                } else if (!_stage2Active &&
                           (nowMs - _stable3dStartMs) >= GnssConfig::STAGE2_STABLE_HOLD_MS) {
                    UartLock lock(_uartMutex, pdMS_TO_TICKS(25));
                    if (lock && applyElevationCnoFilter(
                                    GnssConfig::STAGE2_TRK_MIN_ELEV_DEG,
                                    GnssConfig::STAGE2_TRK_MIN_CNO_DBHZ)) {
                        _stage2Active = true;
                        StateGuard guard(SystemState::instance());
                        if (guard) {
                            SystemState::instance().data().gnss.stage2TightFilter = true;
                        }
                    }
                }
            } else {
                _stable3dStartMs = nowMs;
            }
        } else {
            _stable3dStartMs = 0;

            // Relax Stage 2 -> Stage 1 (5 deg / 14 dBHz) after 1.5s of lost 3D Fix for fast re-acquisition
            if (_stage2Active) {
                if (_lost3dStartMs == 0) {
                    _lost3dStartMs = nowMs;
                } else if ((nowMs - _lost3dStartMs) >= 1500UL) {
                    UartLock lock(_uartMutex, pdMS_TO_TICKS(25));
                    if (lock) {
                        (void)applyElevationCnoFilter(
                            GnssConfig::STAGE1_ACQ_MIN_ELEV_DEG,
                            GnssConfig::STAGE1_ACQ_MIN_CNO_DBHZ
                        );
                        _stage2Active = false;
                        StateGuard guard(SystemState::instance());
                        if (guard) {
                            SystemState::instance().data().gnss.stage2TightFilter = false;
                        }
                    }
                }
            }

            // 45-Second No-Fix Hot-Start Watchdog (only when sky satellites are visible)
            if (inView > 0 && (nowMs - _noFixWatchdogMs) >= GnssConfig::HOTSTART_WATCHDOG_MS) {
                triggerHotStartReset(nowMs);
            } else if (inView == 0) {
                // Inside underground garage or deep tunnel: hold watchdog timer so it doesn't spam resets
                _noFixWatchdogMs = nowMs;
            }
        }

        // Poll UBX-MON-RF (0x0A 0x38) every 2000ms
        pollMonRf(nowMs);
    }

    // Run trip odometer, 1Hz duration buckets, 30s history charts & PSRAM trail updates
    updateTripOdometerAndTrails(nowMs);
}
