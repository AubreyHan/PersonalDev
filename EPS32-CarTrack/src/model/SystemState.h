#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#include "../config.h"

// =============================================================================
// M5Track-Tab5 Thread-Safe Global System State & Cross-Module Contract Pool
// All cross-task telemetry, UI snapshots, PSRAM trail preview buffers, and
// asynchronous command flags are defined here with zero ambiguity.
// =============================================================================

// -----------------------------------------------------------------------------
// 1. Boot Hardware Self-Test State (Drives PageInit)
// -----------------------------------------------------------------------------
struct BootCheckState {
    bool    psramOk                  = false;
    bool    littlefsOk               = false;
    bool    tfCardMounted            = false;
    bool    tfCardFormatIncompatible = false; // Prompts [Format to FAT32] modal on PageInit
    bool    gnssUartOnline           = false; // M135 NEO-M9N UART communication OK
    int8_t  gnssActiveRxPin          = GnssConfig::PIN_RX_PRIMARY;
    int8_t  gnssActiveTxPin          = GnssConfig::PIN_TX_PRIMARY;
    bool    imuMasterOnline          = false; // Tab5 BMI270 (0x68)
    bool    imuSlaveOnline           = false; // M135 BMI270 (0x69)
    bool    baroOnline               = false; // M135 BMP280 (0x76)
    bool    rtcOnline                = false; // Tab5 RX8130CE (0x32)
    bool    ina226Online             = false; // Tab5 INA226 (0x41, 5mOhm)
    bool    wifiCoProcOnline         = false; // ESP32-C6 SDIO initialized
    uint8_t progressPercent          = 0;     // 0 ~ 100%
    bool    selfTestComplete         = false;
    bool    autoEnteredDashboard     = false; // Set true once first 3D Fix (>=4 sats) switches PageInit -> Page1
    char    statusMessage[64]        = "Initializing Tab5 hardware...";
};

// -----------------------------------------------------------------------------
// 2. Authoritative u-blox NEO-M9N GNSS & RF Diagnostic Models
// -----------------------------------------------------------------------------
enum GnssFixType : uint8_t {
    FIX_NONE      = 0, // No fix
    FIX_DEAD_REC  = 1, // Dead reckoning only
    FIX_2D        = 2, // 2D fix
    FIX_3D        = 3, // 3D fix (>= FIX_3D with gnssFixOk considered valid 3D lock)
    FIX_GNSS_DR   = 4, // GNSS + Dead reckoning combined
    FIX_TIME_ONLY = 5  // Time-only fix
};

// Official u-blox M9 UBX-MON-RF flags[1:0] jammingState (requires CFG-ITFM-ENABLE=1)
enum RfJammingState : uint8_t {
    JAMMING_UNKNOWN = 0, // Unknown / ITFM disabled
    JAMMING_OK      = 1, // OK - no significant jamming
    JAMMING_WARN    = 2, // Warning - interference visible but fix maintained
    JAMMING_CRIT    = 3  // Critical - severe RF jamming, no fix reliable
};

// Official u-blox M9 UBX-MON-RF antStatus enumeration (Note: 2 = OK, NOT Open!)
enum GnssAntennaStatus : uint8_t {
    ANT_INIT     = 0,
    ANT_DONTKNOW = 1,
    ANT_OK       = 2, // Normal active antenna circuit
    ANT_SHORT    = 3, // Antenna shorted
    ANT_OPEN     = 4  // Antenna open / unplugged
};

enum GnssConstellationId : uint8_t {
    GNSS_CONST_GPS     = 0,
    GNSS_CONST_SBAS    = 1,
    GNSS_CONST_GALILEO = 2,
    GNSS_CONST_BEIDOU  = 3,
    GNSS_CONST_QZSS    = 5,
    GNSS_CONST_GLONASS = 6
};

struct SatInfo {
    uint8_t gnssId     = 0;     // GnssConstellationId (0=GPS, 2=GAL, 3=BDS, 6=GLO)
    uint8_t svId       = 0;     // Satellite PRN / SV number
    uint8_t cno        = 0;     // Carrier-to-Noise density ratio (dBHz, 0~63)
    int8_t  elev       = 0;     // Elevation angle (-90..+90 deg)
    int16_t azim       = 0;     // Azimuth angle (0..360 deg)
    uint8_t qualityInd = 0;     // UBX-NAV-SAT quality indicator (0..7, >=4 code locked)
    bool    healthy    = true;  // Signal health flag
    bool    used       = false; // True if used in navigation solution (NAV-SAT svUsed bit3)
};

struct GnssData {
    static constexpr size_t MAX_TRACKED_SATS = 32;

    bool              isValid            = false; // Valid 3D/DR navigation position available
    bool              gnssFixOk          = false; // UBX-NAV-PVT flags bit0 (gnssFixOK)
    GnssFixType       fixType            = FIX_NONE;
    int32_t           latE7              = 0;     // Latitude * 1e7
    int32_t           lonE7              = 0;     // Longitude * 1e7
    double            latitude           = 0.0;   // WGS-84 Latitude (deg)
    double            longitude          = 0.0;   // WGS-84 Longitude (deg)
    float             altitudeMeters     = 0.0f;  // hMSL altitude above mean sea level (m)
    float             ellipsoidHeightM   = 0.0f;  // Ellipsoid height (m)
    float             speedKmh           = 0.0f;  // Ground speed after 1.8km/h static deadband (km/h)
    float             rawSpeedKmh        = 0.0f;  // Unfiltered ground speed (km/h)
    float             headingDeg         = 0.0f;  // MagSafe-immune fused heading (0.0~359.9 deg)
    float             gpsCourseDeg       = 0.0f;  // Raw UBX headMot ground course (0.0~359.9 deg)
    bool              staticHoldLocked   = true;  // True when speed <= 1.8 km/h locks position & heading
    bool              stage2TightFilter  = false; // False=Stage1(5deg/14dB), True=Stage2(10deg/20dB)

    // Precision Dilution of Precision (from UBX-NAV-DOP + UBX-NAV-PVT)
    float             hdop               = 99.9f;
    float             vdop               = 99.9f;
    float             pdop               = 99.9f;
    float             hAccMeters         = 999.0f;
    float             vAccMeters         = 999.0f;

    // Satellite Counts (Solution vs Sky Visible)
    uint8_t           numSats            = 0; // Satellites used in fix (NAV-PVT numSV)
    uint8_t           satsInView         = 0; // Total visible satellites in sky (NAV-SAT / GSV)
    uint8_t           numBdsSats         = 0; // BeiDou used count
    uint8_t           numGpsSats         = 0; // GPS used count
    uint8_t           numGloSats         = 0; // GLONASS used count
    uint8_t           numGalSats         = 0; // Galileo used count
    uint8_t           viewBdsSats        = 0; // BeiDou visible count
    uint8_t           viewGpsSats        = 0; // GPS visible count
    uint8_t           viewGloSats        = 0; // GLONASS visible count
    uint8_t           viewGalSats        = 0; // Galileo visible count

    // Blind-Zone Dead Reckoning (DR) & Highway Tunnel Axis Constraint
    bool              isDeadReckoning    = false;
    uint32_t          drDurationSec      = 0;
    bool              isInTunnel         = false;
    char              tunnelName[32]     = "";
    float             tunnelProgressKm   = 0.0f;
    float             tunnelLengthKm     = 0.0f;

    // RF Interference & Active Antenna Diagnostics (UBX-MON-RF)
    RfJammingState    jammingState       = JAMMING_UNKNOWN;
    GnssAntennaStatus antennaStatus      = ANT_INIT;
    uint8_t           antennaPower       = 2; // 0=OFF, 1=ON, 2=DONTKNOW
    uint16_t          rfNoiseLevel       = 0; // noisePerMS
    uint16_t          rfAgcCount         = 0; // agcCnt (0..8191)
    uint8_t           cwJammingIndicator = 0; // jamInd (0..255)

    // Atomic UTC Timestamp from NAV-PVT
    bool              utcDateValid       = false;
    bool              utcTimeValid       = false;
    uint16_t          year               = 2026;
    uint8_t           month              = 1;
    uint8_t           day                = 1;
    uint8_t           hour               = 0;
    uint8_t           minute             = 0;
    uint8_t           second             = 0;
    uint16_t          millisecond        = 0;

    // Tracked Satellites Snapshot for Radar & C/N0 Bar Chart (PageInit & ToolGnss)
    uint8_t           activeSatCount     = 0;
    SatInfo           sats[MAX_TRACKED_SATS] = {};

    // Watchdog & Outlier Counters
    uint32_t          lastFixMillis      = 0;
    uint32_t          hotRestartCount    = 0;
    uint32_t          outlierDropCount   = 0;
};

// -----------------------------------------------------------------------------
// 3. Dual BMI270 (0x68 Master + 0x69 Slave) Attitude & G-Force Model
// -----------------------------------------------------------------------------
struct ImuData {
    static constexpr size_t G_TRAIL_POINTS = 25; // 0.5s comet tail @ 50Hz for Page5Dynamics

    bool    masterOnline        = false; // Tab5 onboard BMI270 (0x68)
    bool    slaveOnline         = false; // M135 onboard BMI270 (0x69)
    bool    isDualFused         = false; // True when 0.5*(Master+Slave) noise reduction active
    bool    failoverActive      = false; // True when Master failed and Slave took over within 20ms
    bool    crossCheckWarning   = false; // True if gyro/accel delta exceeds safety tolerance

    // Fused 3-Axis Linear Acceleration (m/s^2) & Angular Velocity (rad/s)
    float   accelX              = 0.0f;
    float   accelY              = 0.0f;
    float   accelZ              = 9.80665f;
    float   gyroX               = 0.0f;
    float   gyroY               = 0.0f;
    float   gyroZ               = 0.0f;  // Yaw rate (rad/s) for heading & DR integration

    // Vehicle Dynamic G-Force (1G = 9.80665 m/s^2)
    float   lateralG            = 0.0f;  // Cornering centrifugal G (-left / +right)
    float   longitudinalG       = 0.0f;  // Acceleration / braking G (-accel / +brake)
    float   combinedG           = 0.0f;  // hypotf(lateralG, longitudinalG)

    // Ring Buffer for 0.5s Dynamic G-Force Friction Circle Trail
    float   gTrailLat[G_TRAIL_POINTS] = {};
    float   gTrailLon[G_TRAIL_POINTS] = {};
    uint8_t gTrailHead          = 0;
    uint8_t gTrailCount         = 0;

    // Vehicle Pitch / Roll Leveling & MagSafe-Immune Fused Heading
    float   rawPitchDeg         = 0.0f;
    float   rawRollDeg          = 0.0f;
    float   pitchZeroOffsetDeg  = 0.0f;  // Persisted zero-calibration offset
    float   rollZeroOffsetDeg   = 0.0f;  // Persisted zero-calibration offset
    float   pitchDeg            = 0.0f;  // Calibrated vehicle pitch (-45..+45 deg)
    float   rollDeg             = 0.0f;  // Calibrated vehicle roll (-45..+45 deg)
    float   fusedHeadingDeg     = 0.0f;  // 50Hz Gyro-Z + GPS true course (0.0~359.9 deg)
    bool    zuptLocked          = true;  // Zero-Velocity Update active at standstill
    bool    isStationary        = true;  // IMU motion variance below static threshold
    float   accelVariance       = 0.0f;

    // Cross-thread Calibration Command Flag (UI -> ImuDriver)
    bool    cmdCalibrateZeroLevel = false;

    void pushGTrail(float latG, float lonG) {
        gTrailLat[gTrailHead] = latG;
        gTrailLon[gTrailHead] = lonG;
        gTrailHead = static_cast<uint8_t>((gTrailHead + 1U) % G_TRAIL_POINTS);
        if (gTrailCount < G_TRAIL_POINTS) {
            gTrailCount++;
        }
    }
};

// -----------------------------------------------------------------------------
// 4. BMP280 Barometer & Complementary Altitude Fusion Model
// -----------------------------------------------------------------------------
struct BaroData {
    bool  isOnline            = false;    // M135 BMP280 (0x76)
    float pressureHpa         = 1013.25f; // Ambient pressure (hPa)
    float temperatureC        = 25.0f;    // Module temperature (C)
    float seaLevelPressureHpa = 1013.25f; // Dynamically calibrated P0 (persisted in NVS)
    bool  isP0Calibrated      = false;
    float rawBaroAltitudeM    = 0.0f;     // Barometric altitude prior to complementary fusion (m)
    float fusedAltitudeM      = 0.0f;     // Smooth decimeter-grade Baro+GNSS fused altitude (m)
    float verticalSpeedMps    = 0.0f;     // Climb / descent rate (m/s)
    float roadSlopePercent    = 0.0f;     // Real-time road grade percentage (e.g. +7.5%)
    bool  steepSlopeWarning   = false;    // True when |roadSlopePercent| >= 7.0%
};

// -----------------------------------------------------------------------------
// 5. Highway Tunnel Micro-Database Models (LittleFS / PSRAM)
// -----------------------------------------------------------------------------
struct TunnelWaypoint {
    int32_t latE7 = 0;
    int32_t lonE7 = 0;
};

struct TunnelInfo {
    static constexpr size_t MAX_WAYPOINTS = 8;

    char           name[32]        = "";
    int32_t        entryLatE7      = 0;
    int32_t        entryLonE7      = 0;
    int32_t        exitLatE7       = 0;
    int32_t        exitLonE7       = 0;
    float          entryBearingDeg = 0.0f;
    float          lengthMeters    = 0.0f;
    uint8_t        waypointCount   = 0;
    TunnelWaypoint waypoints[MAX_WAYPOINTS] = {};
};

struct TunnelDbStatus {
    bool     loadedInPsram      = false;
    uint16_t tunnelCount        = 0;
    char     version[24]        = "builtin";
    bool     cmdReloadFromFlash = false; // Set by Web /api/tunnel/upload after LittleFS update
};

// -----------------------------------------------------------------------------
// 6. Three-Tier Trip Statistics, History Ring Buffers & Trail Preview Contract
// -----------------------------------------------------------------------------
enum TripCategory : uint8_t {
    TRIP_SINGLE         = 0, // Per-ignition / 20min-standby / 2s long-press reset
    TRIP_DAILY          = 1, // Local GPS day 00:00:00 ~ 23:59:59 auto-stitched
    TRIP_MANUAL         = 2, // User [+ Sub Start] / [Stop] multi-day manual trip (10MB split)
    TRIP_CATEGORY_COUNT = 3
};

struct HistoryBuffer {
    static constexpr size_t CAPACITY = 120; // 1 sample / 30s -> 60 minutes (1050px chart)

    float    samples[CAPACITY] = {};
    uint16_t head              = 0;
    uint16_t count             = 0;

    void push(float val) {
        samples[head] = val;
        head = static_cast<uint16_t>((head + 1U) % CAPACITY);
        if (count < CAPACITY) {
            count++;
        }
    }

    void reset() {
        head  = 0;
        count = 0;
        memset(samples, 0, sizeof(samples));
    }

    // Read sample in chronological order: idx=0 is oldest, idx=count-1 is newest ("Now")
    float getChronological(uint16_t idx) const {
        if (count == 0 || idx >= count) {
            return 0.0f;
        }
        if (count < CAPACITY) {
            return samples[idx];
        }
        return samples[(head + idx) % CAPACITY];
    }
};

struct TripStats {
    uint32_t      startTimestampUtc    = 0;
    double        startLatitude        = 0.0;
    double        startLongitude       = 0.0;
    bool          hasStartAnchor       = false;

    float         tripDistanceKm       = 0.0f;
    uint32_t      movingDurationSec    = 0; // Speed > 1.8 km/h active driving duration
    uint32_t      idleDurationSec      = 0; // Stopped / traffic light idle duration

    // Driving Speed Structure Buckets for Page3Speed (>80 / 40~80 / <=40 km/h)
    uint32_t      highwayMovingSec     = 0; // > 80 km/h
    uint32_t      urbanMovingSec       = 0; // 40 ~ 80 km/h
    uint32_t      slowMovingSec        = 0; // 1.8 ~ 40 km/h

    float         maxSpeedKmh          = 0.0f;
    float         avgSpeedKmh          = 0.0f;
    float         currentAltitudeM     = 0.0f;
    float         maxAltitudeM         = 0.0f;
    float         minAltitudeM         = 0.0f;
    bool          altitudeInitialized  = false;
    float         cumulativeClimbM     = 0.0f;
    float         cumulativeDescentM   = 0.0f;
    float         verticalSpeedMps     = 0.0f;

    // Peak G-Force Memory
    float         maxLateralG          = 0.0f;
    float         maxLongitudinalG     = 0.0f;

    // 1-Hour Rolling Chart Buffers (Page2Altitude & Page3Speed)
    HistoryBuffer altitudeHistory      = {};
    HistoryBuffer speedHistory         = {};

    void resetWithAnchor(uint32_t nowUtc, double lat, double lon, bool validPos, float altM, float spdKmh) {
        startTimestampUtc   = nowUtc;
        startLatitude       = validPos ? lat : 0.0;
        startLongitude      = validPos ? lon : 0.0;
        hasStartAnchor      = validPos;
        tripDistanceKm      = 0.0f;
        movingDurationSec   = 0;
        idleDurationSec     = 0;
        highwayMovingSec    = 0;
        urbanMovingSec      = 0;
        slowMovingSec       = 0;
        maxSpeedKmh         = spdKmh;
        avgSpeedKmh         = spdKmh;
        currentAltitudeM    = altM;
        maxAltitudeM        = altM;
        minAltitudeM        = altM;
        altitudeInitialized = validPos;
        cumulativeClimbM    = 0.0f;
        cumulativeDescentM  = 0.0f;
        verticalSpeedMps    = 0.0f;
        maxLateralG         = 0.0f;
        maxLongitudinalG    = 0.0f;
        altitudeHistory.reset();
        speedHistory.reset();
    }
};

// Compact Trail Point stored in PSRAM for Page4Trail Full-Bleed Vector Map
struct TrailPoint {
    int32_t latE7 = 0;
    int32_t lonE7 = 0;
};

// Lightweight Trail Metadata included in SystemStateSnapshot
struct TrailPreviewMeta {
    static constexpr size_t MAX_POINTS = 2048; // Stored in PSRAM; auto-decimates 2:1 when full

    bool     hasOrigin            = false;
    int32_t  originLatE7          = 0;
    int32_t  originLonE7          = 0;
    int32_t  minLatE7             = 0;
    int32_t  maxLatE7             = 0;
    int32_t  minLonE7             = 0;
    int32_t  maxLonE7             = 0;
    uint16_t pointCount           = 0;
    uint16_t decimationStride     = 1;    // Doubles whenever buffer compacts 2:1
    float    straightDistToStartKm = 0.0f; // HUD top-left: straight-line distance to green flag
    float    returnBearingDeg     = 0.0f; // HUD top-left: bearing back to start point (0~360)
    uint32_t version              = 1;    // Increments on every point push or reset
};

struct FullTripResetPayload {
    uint32_t timestampUtc      = 0;
    double   newStartLatitude  = 0.0;
    double   newStartLongitude = 0.0;
    float    currentAltitudeM  = 0.0f;
    float    currentSpeedKmh   = 0.0f;
};

struct TripControlData {
    TripCategory         activeViewCategory      = TRIP_SINGLE; // Active tab on Page 1/2/3/4
    bool                 manualTripActive        = false;       // Controls [SUB] tab visibility!
    uint16_t             manualSubIndex          = 0;           // sub001, sub002...
    uint16_t             manualSplitIndex        = 1;           // -001, -002...
    uint32_t             lastLocalDayYmd         = 0;           // YYYYMMDD for midnight rollover
    uint32_t             tripResetVersion        = 1;           // Monotonic counter for full reset sync
    uint32_t             lastResetBannerMillis   = 0;           // Drives "[Trip Reset Complete]" toast
    FullTripResetPayload lastResetPayload        = {};

    // Cross-Thread Command Flags (set by UI, consumed & cleared by services)
    bool                 cmdFullTripReset        = false; // Page 1 2s long-press or standby wakeup
    bool                 cmdGpxBreakSegment      = false; // TrackLogger writes </trkseg><trkseg>
    bool                 cmdStartManualTrip      = false; // [+ Sub Start] tapped
    bool                 cmdStopManualTrip       = false; // [Stop Sub] tapped
};

// -----------------------------------------------------------------------------
// 7. INA226 (0x41, 5mOhm) Coulomb Counter & 20-Min Standby Handshake Model
// -----------------------------------------------------------------------------
enum BatteryPowerSource : uint8_t {
    POWER_SOURCE_BATTERY_NPF550 = 0, // Pure NP-F550 discharge (NO external power -> RED 🔋)
    POWER_SOURCE_EXTERNAL_USB   = 1, // Vehicle USB-C / HVIN present with battery -> GREEN ⚡
    POWER_SOURCE_EXT_NO_BATTERY = 2  // External power without battery pack -> GREEN ⚡
};

enum StandbyLifecyclePhase : uint8_t {
    STANDBY_PHASE_ACTIVE         = 0, // Normal driving / monitoring (360MHz, display on)
    STANDBY_PHASE_SETTLING_TRIPS = 1, // 20-min standstill reached: flushing trips.csv & closing GPX
    STANDBY_PHASE_PRE_SLEEP_SYNC = 2, // Uploading closed tracks to OneDrive (max 25s, OSD shown)
    STANDBY_PHASE_SLEEPING       = 3, // Display off, CPU 80MHz, monitoring IMU/Touch/ExtPower
    STANDBY_PHASE_WAKING_UP      = 4  // Restoring 360MHz, waking display & starting new single trip
};

struct PowerData {
    bool                  isAvailable          = false; // INA226 (0x41) online
    bool                  isExternalPower      = true;  // MANDATORY DUAL-COLOR FLAG: true=GREEN ⚡, false=RED 🔋
    bool                  isCharging           = false; // Current > +15mA
    bool                  isBatteryPresent     = true;
    BatteryPowerSource    powerSource          = POWER_SOURCE_EXTERNAL_USB;
    uint32_t              uiDisplayColorRgb888 = ThemeColors::POWER_EXT_GREEN;

    float                 busVoltageV          = 7.80f; // 6.00 ~ 8.40V (2S NP-F550)
    float                 shuntVoltageMv       = 0.0f;
    float                 currentMa            = 0.0f;  // +charge / -discharge (mA)
    float                 avgCurrentMa         = 0.0f;  // 30s smoothed current (mA)
    float                 powerMw              = 0.0f;  // Instantaneous power (mW)
    float                 remainingMah         = 1600.0f;
    float                 socPercent           = 80.0f; // 0.0 ~ 100.0%
    uint16_t              remainingMinutes     = 0;

    // 20-Minute Standstill Auto-Finish & Pre-Sleep Cloud Sync Handshake
    uint32_t              standstillSeconds    = 0;     // Continuous stationary seconds (target: 1200s)
    StandbyLifecyclePhase standbyPhase         = STANDBY_PHASE_ACTIVE;
    bool                  preSleepSettleReq    = false; // PowerTask -> TrackLogger: close GPX & write CSV
    bool                  preSleepSettleAck    = false; // TrackLogger -> PowerTask: files closed safely
    bool                  preSleepCloudSyncReq = false; // PowerTask -> NetCloudTask: run 25s pre-sleep upload
    bool                  preSleepCloudSyncAck = false; // NetCloudTask -> PowerTask: upload finished/timed out
    bool                  lowBattCutoffActive  = false; // True when busVoltageV < 6.00V
};

// -----------------------------------------------------------------------------
// 8. TF Card Storage, Tri-Folder GPX Logger & Format/Export Control Model
// -----------------------------------------------------------------------------
enum StorageFsType : uint8_t {
    STORAGE_FS_NONE        = 0,
    STORAGE_FS_FAT16       = 1,
    STORAGE_FS_FAT32       = 2,
    STORAGE_FS_EXFAT       = 3,
    STORAGE_FS_UNSUPPORTED = 4
};

struct StorageData {
    bool          isMounted               = false;
    bool          formatIncompatible      = false; // Triggers 1-tap FAT32 format prompt
    StorageFsType fsType                  = STORAGE_FS_NONE;
    uint64_t      totalBytes              = 0;
    uint64_t      usedBytes               = 0;
    uint64_t      tracksDirBytes          = 0;     // Monitored strictly <= 50% of totalBytes
    float         tracksQuotaRatio        = 0.0f;  // tracksDirBytes / totalBytes (0.0 ~ 0.50)

    uint16_t      totalTrackPairs         = 0;     // Total W/C GPX pairs across all 3 folders
    uint16_t      singlePairCount         = 0;
    uint16_t      dailyPairCount          = 0;
    uint16_t      subPairCount            = 0;
    uint16_t      healedFilesOnBoot       = 0;

    // Active Recording Streams (OneDriveSync MUST skip files matching these active paths!)
    bool          isRecording             = false; // Drives blinking red REC indicator in LeftDock
    bool          autoRecordEnabled       = true;
    char          activeSingleWgsPath[48] = "";    // e.g. "single/W-20261111-083000.gpx"
    char          activeSingleGcjPath[48] = "";    // e.g. "single/C-20261111-083000.gpx"
    uint16_t      singleSplitSeq          = 1;
    uint32_t      singlePointsInFile      = 0;

    char          activeDailyWgsPath[48]  = "";    // e.g. "daily/W-Day-20261111.gpx"
    char          activeDailyGcjPath[48]  = "";

    char          activeSubWgsPath[48]    = "";    // e.g. "sub/W-sub001-001.gpx"
    char          activeSubGcjPath[48]    = "";
    uint16_t      subIndex                = 0;     // subXXX
    uint16_t      subVolumeIndex          = 1;     // -YYY
    uint32_t      subCurrentBytes         = 0;     // Strictly capped at 10,485,760 bytes (10 MiB)

    // Cross-Thread Storage Management Commands (UI / Web -> TrackLogger)
    bool          cmdFormatFat32          = false;
    bool          isFormatting            = false;
    bool          lastFormatOk            = false;
    bool          cmdRefreshStorage       = false;
    bool          cmdDeleteAllTracks      = false;
    char          cmdDeleteRelPath[64]    = "";    // Non-empty requests deleting a specific file/pair
};

// -----------------------------------------------------------------------------
// 9. Dual-Source Time Engine & 4-Slot World Clock Cards (Page7 & SubPageEditClock)
// -----------------------------------------------------------------------------
enum ClockSyncSource : uint8_t {
    CLOCK_SRC_UNSYNCED = 0,
    CLOCK_SRC_RTC      = 1, // RX8130CE (0x32)
    CLOCK_SRC_NTP      = 2, // Wi-Fi NTP
    CLOCK_SRC_GNSS     = 3  // u-blox NEO-M9N Atomic UTC
};

enum DstRuleType : uint8_t {
    DST_RULE_NONE       = 0,
    DST_RULE_US_EASTERN = 1, // 2nd Sun Mar ~ 1st Sun Nov (EST UTC-5 / EDT UTC-4)
    DST_RULE_US_PACIFIC = 2, // PST UTC-8 / PDT UTC-7
    DST_RULE_EU_CENTRAL = 3, // Last Sun Mar ~ Last Sun Oct (CET UTC+1 / CEST UTC+2)
    DST_RULE_EU_LONDON  = 4, // GMT UTC+0 / BST UTC+1
    DST_RULE_AU_SYDNEY  = 5  // AEST UTC+10 / AEDT UTC+11
};

struct WorldClockEntry {
    char        cityName[24]       = "Beijing";
    char        regionGroup[16]    = "Asia"; // Asia / Americas / Europe / UTC
    int16_t     baseUtcOffsetMin   = 480;    // Standard offset in minutes (e.g. +480 = UTC+8)
    DstRuleType dstRule            = DST_RULE_NONE;
    bool        useGpsDynamicTz    = false;  // Mode 1: auto compute offset from GPS longitude

    // Computed Live Display Values (updated every second by TimeService)
    int16_t     effectiveOffsetMin = 480;
    bool        isDstActive        = false;
    uint16_t    year               = 2026;
    uint8_t     month              = 1;
    uint8_t     day                = 1;
    uint8_t     weekday            = 4;      // 0=Sun .. 6=Sat
    uint8_t     hour               = 8;
    uint8_t     minute             = 0;
    uint8_t     second             = 0;
    bool        isDaytime          = true;   // 06:00..17:59 -> Sun icon, else Moon icon
};

struct TimeData {
    ClockSyncSource source             = CLOCK_SRC_UNSYNCED;
    bool            isCalibrated       = false;
    bool            rtcVoltageLowVlf   = false; // RX8130CE Flag 0x1D bit1 VLF
    uint32_t        epochUtc           = 0;

    uint16_t        utcYear            = 2026;
    uint8_t         utcMonth           = 1;
    uint8_t         utcDay             = 1;
    uint8_t         utcHour            = 0;
    uint8_t         utcMinute          = 0;
    uint8_t         utcSecond          = 0;

    int16_t         localTzOffsetMin   = 480;   // Default UTC+8 (+480 min)
    bool            autoLocalTzFromGps = true;
    uint16_t        localYear          = 2026;
    uint8_t         localMonth         = 1;
    uint8_t         localDay           = 1;
    uint8_t         localWeekday       = 4;     // 0=Sun .. 6=Sat
    uint8_t         localHour          = 8;
    uint8_t         localMinute        = 0;
    uint8_t         localSecond        = 0;

    // 4 Editable Aviation-Style World Clock Cards
    WorldClockEntry worldClocks[4]     = {};
    uint8_t         editingCardIndex   = 0;     // Selected card (0..3) when entering SubPageEditClock
    bool            cmdSaveClocksNvs   = false; // Triggered when user saves edits in SubPageEditClock
};

// -----------------------------------------------------------------------------
// 10. ESP32-C6 Wi-Fi 6, Captive Portal, Export SoftAP, Dual QR & AGPS Model
// -----------------------------------------------------------------------------
enum WifiOperatingMode : uint8_t {
    WIFI_OP_OFF             = 0,
    WIFI_OP_STA_SCANNING    = 1,
    WIFI_OP_STA_CONNECTING  = 2,
    WIFI_OP_STA_CONNECTED   = 3,
    WIFI_OP_AP_SETUP_PORTAL = 4, // M5Track-Setup (192.168.4.1 + DNS 53 Captive Portal)
    WIFI_OP_AP_TRACK_SHARE  = 5  // M5Track-Share (192.168.4.1 + /tracks Wireless Export)
};

enum AgpsStatus : uint8_t {
    AGPS_STATE_IDLE        = 0,
    AGPS_STATE_DOWNLOADING = 1,
    AGPS_STATE_INJECTED    = 2,
    AGPS_STATE_ERROR       = 3
};

struct SavedWifiCredential {
    bool inUse        = false;
    char ssid[33]     = "";
    char password[65] = "";
};

struct ScannedApEntry {
    char    ssid[33] = "";
    int8_t  rssi     = -100;
    uint8_t authMode = 0;
};

struct NetworkData {
    static constexpr size_t MAX_SCAN_RESULTS = 12;

    WifiOperatingMode   mode                  = WIFI_OP_OFF;
    bool                staConnected          = false;
    char                connectedSsid[33]     = "";
    char                staIp[16]             = "0.0.0.0";
    int8_t              rssiDbm               = -100;

    uint8_t             savedCount            = 0;
    SavedWifiCredential savedList[NetConfig::MAX_SAVED_NETWORKS] = {};

    uint8_t             scanCount             = 0;
    ScannedApEntry      scanResults[MAX_SCAN_RESULTS] = {};

    // SoftAP Captive Portal & Wireless Export Hotspot + Dual QR Payloads
    bool                softApActive          = false;
    bool                isShareHotspot        = false; // false=M5Track-Setup, true=M5Track-Share
    uint8_t             apStationCount        = 0;
    char                apSsid[33]            = "M5Track-Setup";
    char                apIp[16]              = "192.168.4.1";
    char                qrWifiConnect[96]     = "WIFI:T:nopass;S:M5Track-Setup;;";
    char                qrWebPortalUrl[96]    = "http://192.168.4.1/setup";
    bool                portalProvisionedOk   = false; // Triggers 3s auto-close & return to dashboard
    uint32_t            portalOkMillis        = 0;
    char                statusText[64]        = "Disconnected";

    // NTP & u-blox AssistNow Online Status
    bool                ntpSynced             = false;
    AgpsStatus          agpsStatus            = AGPS_STATE_IDLE;
    uint32_t            agpsInjectedBytes     = 0;
    uint32_t            agpsLastSyncUtc       = 0;

    // Cross-Thread Network Control Flags (UI -> NetworkManager)
    bool                cmdStartSetupAp       = false;
    bool                cmdStartShareAp       = false;
    bool                cmdStopAp             = false;
    bool                cmdReconnectSta       = false;
    bool                cmdScanWifi           = false;
    int8_t              cmdDeleteSavedIdx     = -1; // 0..4 deletes slot; -1 = no action
    bool                cmdRunAgpsNow         = false;
};

// -----------------------------------------------------------------------------
// 11. Open-Meteo v1 Weather Forecast Model (24h Split into 2x12h + Multi-Day)
// -----------------------------------------------------------------------------
struct HourlyWeatherItem {
    uint8_t hour24        = 0;     // Local hour 0..23
    float   tempC         = 0.0f;  // temperature_2m
    uint8_t precipProbPct = 0;     // precipitation_probability (0..100%)
    uint8_t wmoCode       = 0;     // WMO weather_code (0..99)
    bool    isDay         = true;
};

struct DailyWeatherItem {
    uint8_t month            = 1;
    uint8_t day              = 1;
    uint8_t wmoCode          = 0;
    float   tempMaxC         = 0.0f;
    float   tempMinC         = 0.0f;
    uint8_t precipProbMaxPct = 0;
};

struct WeatherData {
    static constexpr size_t HOURLY_SLOTS = 24; // [0..11] = Front 12h, [12..23] = Back 12h
    static constexpr size_t DAILY_SLOTS  = 3;

    bool              hasValidData       = false;
    bool              isFromOfflineCache = false;
    bool              isFetching         = false;
    uint32_t          lastFetchUtc       = 0;
    double            fetchedLatitude    = 0.0;
    double            fetchedLongitude   = 0.0;

    // Current Real-Time Weather
    float             currentTempC       = 24.0f;
    float             apparentTempC      = 24.0f;
    uint8_t           humidityPct        = 50;
    float             windSpeedKmh       = 0.0f;
    uint16_t          windDirectionDeg   = 0;
    uint8_t           currentWmoCode     = 0;
    bool              isDay              = true;

    // 24-Hour Forecast (93px/col 12h front/back instant toggle on Page6Weather)
    HourlyWeatherItem hourly[HOURLY_SLOTS] = {};
    DailyWeatherItem  daily[DAILY_SLOTS]   = {};

    // Forward-Looking Driving Weather Hazard Alert
    bool              hasHazardWarning   = false;
    char              warningMessage[96] = "Road conditions normal";

    bool              cmdRefreshWeather  = false;
};

// -----------------------------------------------------------------------------
// 12. Microsoft OneDrive OAuth 2.0 Device Code & Graph Sync Model
// -----------------------------------------------------------------------------
enum OneDriveBindState : uint8_t {
    ONEDRIVE_UNBOUND          = 0,
    ONEDRIVE_REQ_DEVICE_CODE  = 1,
    ONEDRIVE_WAIT_USER_VERIFY = 2, // Displaying userCode + QR(verificationUri)
    ONEDRIVE_BOUND_ACTIVE     = 3,
    ONEDRIVE_AUTH_FAILED      = 4
};

enum OneDriveTransferState : uint8_t {
    ONEDRIVE_IDLE             = 0,
    ONEDRIVE_SYNCING          = 1,
    ONEDRIVE_PRE_SLEEP_SYNC   = 2, // Blocking 25s pre-standby upload
    ONEDRIVE_SYNC_OK          = 3,
    ONEDRIVE_SYNC_ERR         = 4
};

struct OneDriveData {
    OneDriveBindState     bindState             = ONEDRIVE_UNBOUND;
    OneDriveTransferState transferState         = ONEDRIVE_IDLE;
    bool                  isBound               = false;
    bool                  autoSyncEnabled       = true;
    char                  accountEmail[64]      = "";

    // RFC 8628 Device Code Flow Display Fields
    char                  userCode[20]          = ""; // e.g. "B8K2-9X4M"
    char                  verificationUri[64]   = "https://microsoft.com/devicelogin";
    uint16_t              deviceCodeExpiresSec  = 0;

    // Incremental Sync Progress (/tracks/.synced_ledger)
    uint16_t              pendingFiles          = 0;
    uint16_t              syncedTotalFiles      = 0;
    uint8_t               currentFilePercent    = 0;  // 0 ~ 100%
    char                  currentFileName[48]   = "";
    uint32_t              lastSyncUtc           = 0;
    char                  lastError[80]         = "";

    // Cross-Thread Control Triggers (ToolOneDrive / ToolSettings / Web -> OneDriveService)
    bool                  cmdStartDeviceCode    = false;
    bool                  cmdCancelDeviceCode   = false;
    bool                  cmdUnbind             = false;
    bool                  cmdSyncNow            = false;
    bool                  cmdSetAutoSync        = false;
    bool                  requestedAutoSyncVal  = true;
};

// -----------------------------------------------------------------------------
// 13. User Preferences & Cloud Token Configuration
// -----------------------------------------------------------------------------
struct UserSettings {
    uint8_t screenBrightness      = 180;  // 10 ~ 255
    bool    autoDayNightBacklight = true;
    bool    speakerBeepEnabled    = true;
    uint8_t speakerVolume         = HapticConfig::SPEAKER_DEFAULT_VOL;
    bool    autoRecordOnMove      = true;
    bool    oneDriveAutoSync      = true;
    char    assistNowToken[64]    = "";
    char    oneDriveClientId[64]  = "";
    char    oneDriveTenant[24]    = "common";

    // Cross-Thread Persistence Trigger
    bool    cmdSaveSettingsToNvs  = false;
};

// -----------------------------------------------------------------------------
// 14. Complete Atomic Snapshot Structure (Passed by const& to UI BasePage::render)
// -----------------------------------------------------------------------------
struct SystemStateSnapshot {
    BootCheckState   boot          = {};
    GnssData         gnss          = {};
    ImuData          imu           = {};
    BaroData         baro          = {};
    TunnelDbStatus   tunnelDb      = {};
    TripStats        trips[TRIP_CATEGORY_COUNT]     = {};
    TrailPreviewMeta trailMeta[TRIP_CATEGORY_COUNT] = {};
    TripControlData  tripControl   = {};
    PowerData        power         = {};
    StorageData      storage       = {};
    TimeData         time          = {};
    NetworkData      network       = {};
    WeatherData      weather       = {};
    OneDriveData     oneDrive      = {};
    UserSettings     settings      = {};
};

// -----------------------------------------------------------------------------
// 15. RAII Mutex Guard (`StateGuard` / `StateLock`) & Singleton `SystemState`
// -----------------------------------------------------------------------------
class SystemState;

class StateGuard {
public:
    explicit StateGuard(SemaphoreHandle_t mutex, TickType_t timeoutTicks = portMAX_DELAY)
        : _mutex(mutex), _locked(false) {
        if (_mutex != nullptr) {
            _locked = (xSemaphoreTake(_mutex, timeoutTicks) == pdTRUE);
        }
    }

    inline explicit StateGuard(SystemState& state, TickType_t timeoutTicks = portMAX_DELAY);

    ~StateGuard() {
        if (_locked && _mutex != nullptr) {
            xSemaphoreGive(_mutex);
        }
    }

    StateGuard(const StateGuard&)            = delete;
    StateGuard& operator=(const StateGuard&) = delete;

    bool locked() const { return _locked; }
    explicit operator bool() const { return _locked; }

private:
    SemaphoreHandle_t _mutex;
    bool              _locked;
};

using StateLock = StateGuard;

class SystemState {
public:
    static SystemState& instance() {
        static SystemState s_instance;
        return s_instance;
    }

    // Initialize mutex, default world clocks, and PSRAM trail preview buffers
    bool init() {
        ensureMutex();
        StateGuard guard(_mutex);
        if (_initialized) {
            return true;
        }
        initDefaultWorldClocksUnlocked();
        allocatePsramTrailsUnlocked();
        _initialized = true;
        return true;
    }

    SemaphoreHandle_t mutex() {
        ensureMutex();
        return _mutex;
    }

    // Direct Mutable Access (MUST hold StateGuard / StateLock while reading or writing!)
    SystemStateSnapshot& data() { return _data; }
    const SystemStateSnapshot& data() const { return _data; }

    // Zero-allocation snapshot copy into caller-owned persistent buffer (recommended for UiManager)
    bool getSnapshot(SystemStateSnapshot& outSnapshot, TickType_t timeoutTicks = portMAX_DELAY) {
        ensureMutex();
        StateGuard guard(_mutex, timeoutTicks);
        if (!guard.locked()) {
            return false;
        }
        outSnapshot = _data;
        return true;
    }

    // Convenience value-returning snapshot (use on tasks with >=12KB stack)
    SystemStateSnapshot snapshot() {
        SystemStateSnapshot copy;
        getSnapshot(copy, portMAX_DELAY);
        return copy;
    }

    // Request Full Trip Sync Reset from UI / Standby Wakeup (non-blocking)
    void requestFullTripReset() {
        ensureMutex();
        StateGuard guard(_mutex);
        _data.tripControl.cmdFullTripReset     = true;
        _data.tripControl.lastResetBannerMillis = millis();
    }

    // Execute Full Trip Sync Reset across Single Trip, Peak G, History Charts & Trail Origin
    void executeFullTripReset(const FullTripResetPayload& payload) {
        ensureMutex();
        StateGuard guard(_mutex);
        const bool validAnchor = (fabs(payload.newStartLatitude) > 1e-6 || fabs(payload.newStartLongitude) > 1e-6);
        _data.trips[TRIP_SINGLE].resetWithAnchor(
            payload.timestampUtc,
            payload.newStartLatitude,
            payload.newStartLongitude,
            validAnchor,
            payload.currentAltitudeM,
            payload.currentSpeedKmh
        );
        clearTrailUnlocked(TRIP_SINGLE);
        if (validAnchor) {
            appendTrailPointUnlocked(
                TRIP_SINGLE,
                static_cast<int32_t>(llround(payload.newStartLatitude * 1e7)),
                static_cast<int32_t>(llround(payload.newStartLongitude * 1e7))
            );
        }
        _data.tripControl.lastResetPayload      = payload;
        _data.tripControl.tripResetVersion     += 1U;
        _data.tripControl.cmdFullTripReset      = false;
        _data.tripControl.cmdGpxBreakSegment    = true;
        _data.tripControl.lastResetBannerMillis = millis();
    }

    // Thread-safe PSRAM Trail Point Append (with automatic 2:1 even-index decimation when full)
    void appendTrailPoint(TripCategory category, int32_t latE7, int32_t lonE7,
                          float straightDistKm = 0.0f, float returnBearingDeg = 0.0f) {
        if (category >= TRIP_CATEGORY_COUNT) {
            return;
        }
        ensureMutex();
        StateGuard guard(_mutex);
        appendTrailPointUnlocked(category, latE7, lonE7);
        _data.trailMeta[category].straightDistToStartKm = straightDistKm;
        _data.trailMeta[category].returnBearingDeg      = returnBearingDeg;
    }

    // Reset a specific category's trail preview buffer (e.g., midnight daily reset or manual trip start)
    void resetTrail(TripCategory category) {
        if (category >= TRIP_CATEGORY_COUNT) {
            return;
        }
        ensureMutex();
        StateGuard guard(_mutex);
        clearTrailUnlocked(category);
    }

    // Copy PSRAM trail points out safely for Page4Trail 60fps rendering
    size_t copyTrailPoints(TripCategory category, TrailPoint* dstBuffer, size_t maxDstPoints,
                           TrailPreviewMeta* outMeta = nullptr) {
        if (category >= TRIP_CATEGORY_COUNT || dstBuffer == nullptr || maxDstPoints == 0) {
            return 0;
        }
        ensureMutex();
        StateGuard guard(_mutex);
        const TrailPreviewMeta& meta = _data.trailMeta[category];
        if (outMeta != nullptr) {
            *outMeta = meta;
        }
        const TrailPoint* src = _trailBuffers[category];
        if (src == nullptr || meta.pointCount == 0) {
            return 0;
        }
        const size_t n = (meta.pointCount < maxDstPoints) ? meta.pointCount : maxDstPoints;
        memcpy(dstBuffer, src, n * sizeof(TrailPoint));
        return n;
    }

private:
    SystemState() : _mutex(nullptr), _initialized(false) {
        for (size_t i = 0; i < TRIP_CATEGORY_COUNT; ++i) {
            _trailBuffers[i] = nullptr;
        }
        ensureMutex();
        initDefaultWorldClocksUnlocked();
    }

    ~SystemState() = default;
    SystemState(const SystemState&)            = delete;
    SystemState& operator=(const SystemState&) = delete;

    void ensureMutex() {
        if (_mutex == nullptr) {
            _mutex = xSemaphoreCreateMutex();
        }
    }

    void initDefaultWorldClocksUnlocked() {
        // Card 0: Beijing / Local GPS Time (Neon Green)
        strncpy(_data.time.worldClocks[0].cityName, "Beijing", sizeof(_data.time.worldClocks[0].cityName) - 1);
        strncpy(_data.time.worldClocks[0].regionGroup, "Asia", sizeof(_data.time.worldClocks[0].regionGroup) - 1);
        _data.time.worldClocks[0].baseUtcOffsetMin   = 480;
        _data.time.worldClocks[0].effectiveOffsetMin = 480;
        _data.time.worldClocks[0].dstRule            = DST_RULE_NONE;
        _data.time.worldClocks[0].useGpsDynamicTz    = true;

        // Card 1: Coordinated Universal Time UTC (Ice Cyan)
        strncpy(_data.time.worldClocks[1].cityName, "UTC", sizeof(_data.time.worldClocks[1].cityName) - 1);
        strncpy(_data.time.worldClocks[1].regionGroup, "UTC", sizeof(_data.time.worldClocks[1].regionGroup) - 1);
        _data.time.worldClocks[1].baseUtcOffsetMin   = 0;
        _data.time.worldClocks[1].effectiveOffsetMin = 0;
        _data.time.worldClocks[1].dstRule            = DST_RULE_NONE;
        _data.time.worldClocks[1].useGpsDynamicTz    = false;

        // Card 2: New York (US Eastern Auto-DST, Amber)
        strncpy(_data.time.worldClocks[2].cityName, "New York", sizeof(_data.time.worldClocks[2].cityName) - 1);
        strncpy(_data.time.worldClocks[2].regionGroup, "Americas", sizeof(_data.time.worldClocks[2].regionGroup) - 1);
        _data.time.worldClocks[2].baseUtcOffsetMin   = -300;
        _data.time.worldClocks[2].effectiveOffsetMin = -300;
        _data.time.worldClocks[2].dstRule            = DST_RULE_US_EASTERN;
        _data.time.worldClocks[2].useGpsDynamicTz    = false;

        // Card 3: Tokyo (UTC+9, Pure White)
        strncpy(_data.time.worldClocks[3].cityName, "Tokyo", sizeof(_data.time.worldClocks[3].cityName) - 1);
        strncpy(_data.time.worldClocks[3].regionGroup, "Asia", sizeof(_data.time.worldClocks[3].regionGroup) - 1);
        _data.time.worldClocks[3].baseUtcOffsetMin   = 540;
        _data.time.worldClocks[3].effectiveOffsetMin = 540;
        _data.time.worldClocks[3].dstRule            = DST_RULE_NONE;
        _data.time.worldClocks[3].useGpsDynamicTz    = false;
    }

    void allocatePsramTrailsUnlocked() {
        for (size_t i = 0; i < TRIP_CATEGORY_COUNT; ++i) {
            if (_trailBuffers[i] == nullptr) {
                const size_t bytes = TrailPreviewMeta::MAX_POINTS * sizeof(TrailPoint);
                void* mem = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (mem == nullptr) {
                    mem = malloc(bytes);
                }
                if (mem != nullptr) {
                    memset(mem, 0, bytes);
                    _trailBuffers[i] = static_cast<TrailPoint*>(mem);
                }
            }
        }
    }

    void clearTrailUnlocked(TripCategory category) {
        TrailPreviewMeta& meta = _data.trailMeta[category];
        const uint32_t nextVer = meta.version + 1U;
        meta                   = TrailPreviewMeta{};
        meta.version           = nextVer;
    }

    void appendTrailPointUnlocked(TripCategory category, int32_t latE7, int32_t lonE7) {
        allocatePsramTrailsUnlocked();
        TrailPoint* buf = _trailBuffers[category];
        if (buf == nullptr) {
            return;
        }
        TrailPreviewMeta& meta = _data.trailMeta[category];
        if (!meta.hasOrigin) {
            meta.hasOrigin   = true;
            meta.originLatE7 = latE7;
            meta.originLonE7 = lonE7;
            meta.minLatE7    = latE7;
            meta.maxLatE7    = latE7;
            meta.minLonE7    = lonE7;
            meta.maxLonE7    = lonE7;
        } else {
            if (latE7 < meta.minLatE7) meta.minLatE7 = latE7;
            if (latE7 > meta.maxLatE7) meta.maxLatE7 = latE7;
            if (lonE7 < meta.minLonE7) meta.minLonE7 = lonE7;
            if (lonE7 > meta.maxLonE7) meta.maxLonE7 = lonE7;
        }

        // Compact 2:1 when reaching capacity while preserving index 0 (Origin Green Flag)
        if (meta.pointCount >= TrailPreviewMeta::MAX_POINTS) {
            uint16_t writeIdx = 1;
            for (uint16_t readIdx = 2; readIdx < meta.pointCount; readIdx += 2) {
                buf[writeIdx++] = buf[readIdx];
            }
            meta.pointCount = writeIdx;
            if (meta.decimationStride < 32768U) {
                meta.decimationStride = static_cast<uint16_t>(meta.decimationStride * 2U);
            }
        }

        buf[meta.pointCount].latE7 = latE7;
        buf[meta.pointCount].lonE7 = lonE7;
        meta.pointCount++;
        meta.version++;
    }

    SemaphoreHandle_t   _mutex;
    bool                _initialized;
    SystemStateSnapshot _data;
    TrailPoint*         _trailBuffers[TRIP_CATEGORY_COUNT];
};

inline StateGuard::StateGuard(SystemState& state, TickType_t timeoutTicks)
    : StateGuard(state.mutex(), timeoutTicks) {}
