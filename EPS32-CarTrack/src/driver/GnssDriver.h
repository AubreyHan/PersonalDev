#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#include <stdint.h>
#include <stddef.h>
#include "../config.h"
#include "../model/SystemState.h"

// =============================================================================
// GnssDriver - u-blox NEO-M9N (SPG 4.04) Automotive Multi-GNSS & Trip Engine
//
// Features:
// - Auto-probes UART candidates: GPIO RX=7/TX=6 (primary M135 DIP), 48/47, 38/37
// - Auto-handshakes 38400 / 115200 bps and switches operational UART to 115200 bps
// - UBX-CFG-VALSET tuning: DYN_MODEL_AUTOMOTIVE, CFG-MOT-GNSSSPEED_THRS=50 (1.8km/h),
//   concurrent GPS+BDS+GLO+GAL, CFG-ITFM-ENABLE=1, NAV-PVT + NAV-DOP + NAV-SAT
// - Two-stage dynamic anti-multipath filter:
//     Stage 1 (Search/Re-acq): minElev = 5 deg,  minCNO = 14 dBHz
//     Stage 2 (Stable >=10s) : minElev = 10 deg, minCNO = 20 dBHz (>= 6 sats)
// - 45s no-fix hot-restart watchdog (UBX-CFG-RST navBbrMask=0x0000, resetMode=0x02)
//   when sky satellites are visible, followed by automatic MGA TIME_UTC & POS_LLH re-injection
// - 2s UBX-MON-RF (0x0A 0x38) active antenna & RF jamming monitor
// - Thread-safe AssistNow Online MGA chunked stream injector (frame-boundary safe)
// - Kinematic outlier rejection (>180 km/h 1s jump or >1.2G accel) + 3-tier TripStats,
//   30s 1-hour HistoryBuffer sampling, PSRAM trail points & cmdFullTripReset handling
// =============================================================================
class GnssDriver {
public:
    static GnssDriver& instance();

    // Auto-probe UART pins & baudrate, configure NEO-M9N via UBX-CFG-VALSET,
    // restore NVS warm-start & trip state, and inject initial MGA assistance.
    bool begin(uint16_t measRateMs = GnssConfig::MEAS_RATE_MS);

    // Non-blocking Core 0 update tick: drains UBX stream, runs two-stage filter,
    // 45s hotstart watchdog, 2s MON-RF poll, trip odometer, 30s charts & trail.
    void update(uint32_t nowMs = millis());

    // Dynamically adjust navigation rate (e.g. 5Hz or 10Hz) via UBX-CFG-VALSET.
    bool setNavigationRateHz(uint8_t navRateHz);

    // Thread-safe UBX-MGA-INI-TIME_UTC (0x13 0x40, type 0x10, 24B payload) injection.
    bool injectMgaTimeUtc(
        uint16_t year,
        uint8_t  month,
        uint8_t  day,
        uint8_t  hour,
        uint8_t  minute,
        uint8_t  second,
        uint16_t tAccSec = GnssConfig::MGA_RTC_TACC_SEC,
        uint32_t tAccNs  = GnssConfig::MGA_RTC_TACC_NS
    );

    // Thread-safe UBX-MGA-INI-POS_LLH (0x13 0x40, type 0x01, 20B payload) injection.
    // Units: latE7/lonE7 in deg*1e-7, altCm & posAccCm in centimeters.
    bool injectMgaPosLlh(
        int32_t  latE7,
        int32_t  lonE7,
        int32_t  altCm,
        uint32_t posAccCm
    );

    // Re-inject best available system UTC time + NVS/cached position into NEO-M9N.
    bool injectWarmStartAssistance();

    // Thread-safe AssistNow Online MGA binary chunk injector (handles TCP chunk splits).
    void   resetAssistNowStream();
    size_t pushAssistNowChunk(const uint8_t* data, size_t len, bool skipTime = true);
    void   flushAssistNowStream();

    // Thread-safe raw UBX frame sender.
    bool pushRawUbx(const uint8_t* data, size_t len);

    bool   isOnline() const { return _uartOnline; }
    int8_t activeRxPin() const { return _activeRxPin; }
    int8_t activeTxPin() const { return _activeTxPin; }

private:
    GnssDriver();
    ~GnssDriver() = default;
    GnssDriver(const GnssDriver&) = delete;
    GnssDriver& operator=(const GnssDriver&) = delete;

    void ensureMutex();
    bool probeUartAndHandshake();
    bool applyM9nConfiguration(uint16_t measRateMs);
    bool applyElevationCnoFilter(int8_t minElevDeg, uint8_t minCnoDbHz);
    void triggerHotStartReset(uint32_t nowMs);

    void processNavDop();
    void processNavSat();
    void processNavPvt(uint32_t nowMs);
    void pollMonRf(uint32_t nowMs);

    void handleTripCommandsAndRollover(uint32_t nowMs);
    void updateTripOdometerAndTrails(uint32_t nowMs);
    void loadNvsState();
    void saveWarmStartPosToNvs(int32_t latE7, int32_t lonE7, int32_t altCm, uint32_t utcEpoch);
    void saveTripPersistenceToNvs();

    static uint32_t utcToEpoch(uint16_t year, uint8_t month, uint8_t day,
                               uint8_t hour, uint8_t minute, uint8_t second);
    static void     computeUbxChecksum(uint8_t* frame, size_t payloadLen);

    HardwareSerial    _serial;
    SFE_UBLOX_GNSS    _gnss;
    SemaphoreHandle_t _uartMutex         = nullptr;

    bool              _uartOnline        = false;
    int8_t            _activeRxPin       = GnssConfig::PIN_RX_PRIMARY;
    int8_t            _activeTxPin       = GnssConfig::PIN_TX_PRIMARY;
    uint16_t          _measRateMs        = GnssConfig::MEAS_RATE_MS;

    // Cached DOP & Sky Satellite Counts (merged into GnssData)
    float             _latestHdop        = 99.9f;
    float             _latestVdop        = 99.9f;
    float             _latestPdop        = 99.9f;
    uint8_t           _latestSatsInView  = 0;

    // Kinematic Outlier Filter Anchor
    bool              _hasPrevKinFix     = false;
    double            _prevKinLat        = 0.0;
    double            _prevKinLon        = 0.0;
    float             _prevKinSpeedKmh   = 0.0f;
    uint32_t          _prevKinMs         = 0;

    // Two-Stage Filter & 45s Watchdog Timers
    bool              _stage2Active      = false;
    uint32_t          _stable3dStartMs   = 0;
    uint32_t          _lost3dStartMs     = 0;
    uint32_t          _noFixWatchdogMs   = 0;
    uint32_t          _lastMonRfPollMs   = 0;

    // Warm-Start NVS Cache
    bool              _hasSavedWarmPos   = false;
    int32_t           _savedLatE7        = 0;
    int32_t           _savedLonE7        = 0;
    int32_t           _savedAltCm        = 0;
    uint32_t          _savedPosUtc       = 0;
    uint32_t          _lastPosNvsSaveMs  = 0;

    // Odometer, 1Hz Duration Buckets, 30s History Charts & PSRAM Trail Anchors
    uint32_t          _last1HzTickMs     = 0;
    uint32_t          _last30sChartMs    = 0;
    uint32_t          _lastTripNvsSaveMs = 0;
    bool              _hasOdoAnchor      = false;
    double            _odoPrevLat        = 0.0;
    double            _odoPrevLon        = 0.0;
    double            _trailLastLat[TRIP_CATEGORY_COUNT] = {};
    double            _trailLastLon[TRIP_CATEGORY_COUNT] = {};
    bool              _trailHasLast[TRIP_CATEGORY_COUNT] = {};

    // AssistNow MGA Frame Reassembly Buffer (handles arbitrary 512B TCP chunks)
    static constexpr size_t MGA_CARRY_CAP = 320;
    uint8_t           _mgaCarry[MGA_CARRY_CAP] = {};
    size_t            _mgaCarryLen       = 0;
};
