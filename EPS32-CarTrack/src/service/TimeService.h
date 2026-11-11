#pragma once

#include <Arduino.h>
#include <functional>
#include <stdint.h>
#include "../config.h"
#include "../driver/RtcDriver.h"
#include "../model/SystemState.h"

// =============================================================================
// TimeService - Dual-Source Adaptive Clock Arbitration & Multi-Zone World Clock
// - Arbitration hierarchy:
//     GNSS Atomic UTC (3D Fix validDate/validTime, Highest Authority)
//       <-> Wi-Fi SNTP (ntp.aliyun.com / cn.pool.ntp.org / pool.ntp.org)
//       <-> Tab5 Onboard Epson RX8130CE Hardware RTC (I2C 0x32)
// - Cold Boot Warm-Start Assistance:
//     Restores UTC from RX8130CE + last 3D coordinates from NVS ("m5track"),
//     constructs binary UBX-MGA-INI-TIME_UTC (0x13 0x40, type 0x10, 24B payload)
//     and UBX-MGA-INI-POS_LLH (0x13 0x40, type 0x01, 20B payload) frames for
//     sub-second NEO-M9N garage exit warm start.
// - Dynamic geodetic longitude timezone inference + 4-card WorldClockEntry
//   live calculation (with US/EU/UK/AU DST rules) & NVS persistence.
// =============================================================================

class TimeService {
public:
    using MgaInjectCallback = std::function<bool(const uint8_t* ubxFrame, size_t len)>;

    static constexpr size_t UBX_MGA_TIME_UTC_FRAME_LEN = 32U; // 6B hdr + 24B payload + 2B ck
    static constexpr size_t UBX_MGA_POS_LLH_FRAME_LEN  = 28U; // 6B hdr + 20B payload + 2B ck

    static TimeService& instance();

    TimeService() = default;

    // Initialize RtcDriver, load NVS timezone/world-clocks/warm-start coords,
    // recover cold-boot system clock, and stage UBX-MGA-INI assistance frames.
    bool begin();

    // Periodic arbitration & 4-card world clock calculation (call at >= 2~5Hz)
    void update();

    // Register callback to push binary UBX-MGA-INI frames to GnssDriver UART.
    // Automatically flushes any cold-boot staged MGA frames immediately if ready.
    void setMgaInjectCallback(MgaInjectCallback cb);

    // Pull/flush pending UBX-MGA-INI frames via callback (returns count sent)
    uint8_t flushPendingMgaAssistance();

    // Pull raw pending frames directly if integrator prefers polling over callback
    bool popPendingMgaTimeFrame(uint8_t outFrame[UBX_MGA_TIME_UTC_FRAME_LEN]);
    bool popPendingMgaPosFrame(uint8_t outFrame[UBX_MGA_POS_LLH_FRAME_LEN]);

    // Persist latest valid 3D GNSS position & UTC to NVS for next cold boot / sleep
    bool saveWarmStartPositionToNvs();

    // Persist 4 WorldClockEntry slots & timezone settings to NVS
    bool saveWorldClocksToNvs();

    // Static UBX-MGA-INI frame builders (verified against u-blox M9 SPG 4.04 spec)
    static size_t buildMgaIniTimeUtcFrame(const RtcDateTime& utc,
                                          uint32_t nanosecond,
                                          uint16_t tAccSec,
                                          uint32_t tAccNs,
                                          uint8_t outFrame[UBX_MGA_TIME_UTC_FRAME_LEN]);

    static size_t buildMgaIniPosLlhFrame(int32_t latE7,
                                         int32_t lonE7,
                                         int32_t altCm,
                                         uint32_t posAccCm,
                                         uint8_t outFrame[UBX_MGA_POS_LLH_FRAME_LEN]);

    // Compute dynamic timezone offset (minutes) from geodetic longitude with hysteresis
    static int16_t computeLongitudeTzOffsetMin(double longitudeDeg, int16_t prevOffsetMin);

    // Evaluate automatic Daylight Saving Time (+60 min when active, else 0)
    static bool isDstActiveForUtc(uint32_t epochUtc, int16_t baseOffsetMin, DstRuleType rule);

private:
    static void computeUbxChecksum(const uint8_t* body, size_t len, uint8_t& ckA, uint8_t& ckB);
    static void setSystemClockUtc(uint32_t epochUtc, uint32_t usec = 0UL);
    static uint32_t readSystemClockUtc(uint32_t* outUsec = nullptr);

    void loadFromNvs();
    void saveUserSettingsToNvs(const UserSettings& settings);
    void stageMgaTimeFrame(const RtcDateTime& utc, uint32_t ns, uint16_t tAccS, uint32_t tAccNs);
    void stageMgaPosFrameFromNvs(uint32_t currentEpochUtc);
    void refreshAllClocksInState(uint32_t epochUtc, double gnssLon, bool gnssValid);

    MgaInjectCallback _mgaInjectCb        = nullptr;
    uint8_t           _pendingMgaTime[UBX_MGA_TIME_UTC_FRAME_LEN] = {};
    uint8_t           _pendingMgaPos[UBX_MGA_POS_LLH_FRAME_LEN]   = {};
    bool              _hasPendingMgaTime  = false;
    bool              _hasPendingMgaPos   = false;
    bool              _posInjectedOnce    = false;

    bool              _hasNvsLastPos      = false;
    int32_t           _nvsLastLatE7       = 0;
    int32_t           _nvsLastLonE7       = 0;
    int32_t           _nvsLastAltCm       = 0;
    uint32_t          _nvsLastPosUtc      = 0;

    ClockSyncSource   _syncSource         = CLOCK_SRC_UNSYNCED;
    bool              _ntpConfigured      = false;
    uint32_t          _lastNtpConfigMs    = 0;
    uint32_t          _lastRtcCalibMs     = 0;
    uint32_t          _lastPosSaveMs      = 0;
    uint32_t          _lastRenderedEpoch  = 0;
    bool              _gnssSyncLogged     = false;
    bool              _ntpSyncLogged      = false;
};
