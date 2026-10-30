#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

enum GnssFixType : uint8_t {
    FIX_NONE      = 0,
    FIX_DEAD_REC  = 1,
    FIX_2D        = 2,
    FIX_3D        = 3,
    FIX_GNSS_DR   = 4,
    FIX_TIME_ONLY = 5
};

struct SatInfo {
    uint8_t gnssId; // 0: GPS, 1: SBAS, 2: Galileo, 3: BeiDou, 5: QZSS, 6: GLONASS
    uint8_t svId;
    uint8_t cno;
    int8_t  elev;
    int16_t azim;
    bool    used;
};

struct GnssData {
    bool        isValid;
    bool        moduleOnline;
    GnssFixType fixType;
    int32_t     latE7;
    int32_t     lonE7;
    double      latitude;
    double      longitude;
    float       altitudeMeters;
    float       speedKmh;
    float       headingDeg;
    float       hdop;
    float       pdop;
    float       vdop;
    uint8_t     numSats;
    uint8_t     numBdsSats;
    uint8_t     numGpsSats;
    uint8_t     numGloSats;
    uint8_t     numGalSats;

    // Dead Reckoning & Tunnel Constraint
    bool        isDeadReckoning;
    uint32_t    drDurationSec;
    bool        isInTunnel;
    char        tunnelName[24];
    float       tunnelProgressKm;
    float       tunnelLengthKm;

    // RF & Hardware Diagnostics (UBX-MON-RF)
    uint8_t     jammingState;  // 0: OK, 1: Warning, 2: Critical
    uint8_t     antennaStatus; // 1: OK, 2: Open, 3: Short
    uint16_t    rfNoiseLevel;

    // UTC Time
    uint16_t    year;
    uint8_t     month;
    uint8_t     day;
    uint8_t     hour;
    uint8_t     minute;
    uint8_t     second;
    uint16_t    millisecond;

    // Active Satellite Signal List
    uint8_t     activeSatCount;
    SatInfo     sats[32];
};

struct TunnelWaypoint {
    int32_t latE7;
    int32_t lonE7;
};

struct TunnelInfo {
    const char*           name;
    int32_t               entryLatE7;
    int32_t               entryLonE7;
    int32_t               exitLatE7;
    int32_t               exitLonE7;
    float                 entryBearingDeg;
    float                 lengthMeters;
    uint8_t               waypointCount;
    const TunnelWaypoint* waypoints;
};

struct HistoryBuffer {
    static constexpr size_t CAPACITY = 120; // 30s per sample = 60 min (1 hour)
    float    samples[CAPACITY];
    uint16_t head;
    uint16_t count;

    void push(float val) {
        samples[head] = val;
        head = (head + 1) % CAPACITY;
        if (count < CAPACITY) count++;
    }

    float getOldestFirst(uint16_t idx) const {
        if (count == 0 || idx >= count) return 0.0f;
        if (count < CAPACITY) {
            return samples[idx];
        }
        uint16_t pos = (head + idx) % CAPACITY;
        return samples[pos];
    }

    void reset() {
        head = 0;
        count = 0;
        memset(samples, 0, sizeof(samples));
    }
};

struct TripStats {
    double        tripDistanceKm;
    uint32_t      movingDurationSec;
    uint32_t      idleDurationSec;
    uint32_t      highwaySec;   // > 80 km/h
    uint32_t      urbanSec;     // 40 - 80 km/h
    uint32_t      slowSec;      // 1.8 - 40 km/h
    float         maxSpeedKmh;
    float         avgSpeedKmh;
    float         currentAltitudeM;
    float         maxAltitudeM;
    float         minAltitudeM;
    double        cumulativeClimbM;
    float         verticalSpeedMps;
    float         roadSlopePercent;

    float         maxLateralG;
    float         maxLongitudinalG;

    double        startLatitude;
    double        startLongitude;
    bool          hasStartAnchor;

    HistoryBuffer altitudeHistory;
    HistoryBuffer speedHistory;
};

struct FullTripResetPayload {
    uint32_t timestampUtc;
    double   newStartLatitude;
    double   newStartLongitude;
    float    currentAltitudeM;
    float    currentSpeedKmh;
};

enum DockTouchAction : uint8_t {
    DOCK_ACTION_NONE    = 0,
    DOCK_ACTION_HOME    = 1,
    DOCK_ACTION_TIME    = 2,
    DOCK_ACTION_GNSS    = 3,
    DOCK_ACTION_WIFI    = 4,
    DOCK_ACTION_BATTERY = 5,
    DOCK_ACTION_WEATHER = 6,
    DOCK_ACTION_TOOLS   = 7
};

enum BatteryPowerSource : uint8_t {
    POWER_SOURCE_BATTERY_NPF550 = 0,
    POWER_SOURCE_EXTERNAL_USB   = 1,
    POWER_SOURCE_EXT_NO_BATTERY = 2
};

struct PowerMonitorData {
    bool               isAvailable;
    bool               hasExternalPower;
    bool               isCharging;
    BatteryPowerSource powerSource;
    uint16_t           uiDisplayColor;
    float              busVoltageV;
    float              shuntVoltageMv;
    float              currentMa;
    float              avgCurrentMa;
    float              powerMw;
    float              remainingMah;
    float              batterySocPercent;
    uint16_t           remainingMinutes;
};

struct DualImuStatus {
    bool  masterOnline;      // 0x68
    bool  slaveOnline;       // 0x69
    bool  isDualFused;
    bool  failoverActive;
    bool  crossCheckWarning;
    float fusedAccelX;       // m/s^2
    float fusedAccelY;       // m/s^2
    float fusedAccelZ;       // m/s^2
    float fusedGyroZ;        // rad/s
    float lateralG;          // Lateral G-Force
    float longitudinalG;     // Longitudinal G-Force
    float totalG;            // Combined horizontal G
    float pitchDeg;          // Vehicle pitch (deg)
    float rollDeg;           // Vehicle roll (deg)
    float fusedHeadingDeg;   // MagSafe-immune fused heading (0~360)
};

struct BaroData {
    bool  isAvailable;
    float pressureHpa;
    float temperatureC;
    float rawBaroAltitudeM;
    float fusedAltitudeM;
    float seaLevelPressureHpa;
};

struct HourlyWeatherPoint {
    uint8_t hour;        // 0 ~ 23
    float   tempC;
    uint8_t precipProb;  // 0 ~ 100%
    uint8_t weatherCode; // WMO code
};

struct WeatherData {
    bool               isValid;
    bool               isOfflineCache;
    float              currentTempC;
    float              apparentTempC;
    uint8_t            humidityPercent;
    float              windSpeedKmh;
    uint16_t           windDirDeg;
    uint8_t            weatherCode;
    uint32_t           lastUpdateEpoch;
    HourlyWeatherPoint hourly[24];
    char               warningText[96];
};

struct WorldClockCardConfig {
    char    cityName[24];
    int16_t tzOffsetMinutes; // e.g., +480 for UTC+8, -240 for EDT
    bool    autoDst;         // US/EU Daylight Saving Time auto engine
    bool    useGpsLongitude; // Dynamic longitude offset
};

struct StorageStatus {
    bool     isMounted;
    bool     needsFormat;
    bool     isRecording;
    uint64_t totalBytes;
    uint64_t usedBytes;
    uint64_t tracksDirBytes;
    uint32_t trackFilePairsCount;
    uint32_t currentSegmentPoints;
    char     currentWgsFile[40];
    char     currentGcjFile[40];
    bool     requestNewSegment;
};

struct WifiStatus {
    bool    isConnected;
    bool    apShareActive;
    bool    apSetupActive;
    char    ssid[36];
    char    ipAddr[20];
    int8_t  rssi;
    uint8_t savedNetworkCount;
    bool    agpsInjected;
};

struct TrailPoint {
    double lat;
    double lon;
    float  speedKmh;
};

enum AppPageId : uint8_t {
    PAGE_INIT          = 0,
    PAGE_WIFI_CONFIG   = 1,
    PAGE_1_DASHBOARD   = 2,
    PAGE_2_ALTITUDE    = 3,
    PAGE_3_SPEED       = 4,
    PAGE_4_TRAIL       = 5,
    PAGE_5_DYNAMICS    = 6,
    PAGE_6_WEATHER     = 7,
    PAGE_7_WORLD_CLOCK = 8,
    PAGE_8_TOOLS       = 9,
    SUBPAGE_EDIT_CLOCK = 10,
    SUBPAGE_TOOL_TF    = 11,
    SUBPAGE_TOOL_WIFI  = 12,
    SUBPAGE_TOOL_GNSS  = 13,
    SUBPAGE_TOOL_SYS   = 14
};

struct SystemContext {
    AppPageId            currentPage;
    uint8_t              editingClockSlot; // 0..3
    uint8_t              screenBrightness; // 0..255
    bool                 rtcOnline;
    uint32_t             lastStandstillChangeMs;
    bool                 isVehicleMoving;
    bool                 tripResetBannerActive;
    uint32_t             tripResetBannerExpireMs;

    GnssData             gnss;
    DualImuStatus        imu;
    BaroData             baro;
    PowerMonitorData     power;
    TripStats            trip;
    WeatherData          weather;
    StorageStatus        storage;
    WifiStatus           wifi;
    WorldClockCardConfig clocks[4];
};

class SystemState {
public:
    static SystemState& instance() {
        static SystemState inst;
        return inst;
    }

    void init() {
        if (!_mutex) {
            _mutex = xSemaphoreCreateRecursiveMutex();
        }
        lock();
        memset(&_ctx, 0, sizeof(_ctx));
        _ctx.currentPage = PAGE_INIT;
        _ctx.screenBrightness = 200;
        _ctx.baro.seaLevelPressureHpa = 1013.25f;
        _ctx.power.batterySocPercent = 95.0f;
        _ctx.power.remainingMah = 1900.0f;
        _ctx.power.busVoltageV = 8.15f;
        _ctx.power.hasExternalPower = true;
        _ctx.power.uiDisplayColor = PowerColorConfig::COLOR_EXTERNAL_POWER_GREEN;

        // Default 4 World Clock cards per PRD 3.3 Page 7
        strncpy(_ctx.clocks[0].cityName, "Beijing", sizeof(_ctx.clocks[0].cityName) - 1);
        _ctx.clocks[0].tzOffsetMinutes = 480; // UTC+8
        _ctx.clocks[0].autoDst = false;
        _ctx.clocks[0].useGpsLongitude = true;

        strncpy(_ctx.clocks[1].cityName, "UTC", sizeof(_ctx.clocks[1].cityName) - 1);
        _ctx.clocks[1].tzOffsetMinutes = 0;   // UTC+0
        _ctx.clocks[1].autoDst = false;
        _ctx.clocks[1].useGpsLongitude = false;

        strncpy(_ctx.clocks[2].cityName, "New York", sizeof(_ctx.clocks[2].cityName) - 1);
        _ctx.clocks[2].tzOffsetMinutes = -240; // EDT-4 / EST-5
        _ctx.clocks[2].autoDst = true;
        _ctx.clocks[2].useGpsLongitude = false;

        strncpy(_ctx.clocks[3].cityName, "Tokyo", sizeof(_ctx.clocks[3].cityName) - 1);
        _ctx.clocks[3].tzOffsetMinutes = 540; // UTC+9
        _ctx.clocks[3].autoDst = false;
        _ctx.clocks[3].useGpsLongitude = false;

        // Default initial weather preview
        _ctx.weather.currentTempC = 24.5f;
        _ctx.weather.apparentTempC = 23.0f;
        _ctx.weather.humidityPercent = 52;
        _ctx.weather.windSpeedKmh = 14.0f;
        _ctx.weather.weatherCode = 1;
        strncpy(_ctx.weather.warningText, "Weather Normal - Clear road conditions ahead", sizeof(_ctx.weather.warningText) - 1);
        for (int i = 0; i < 24; i++) {
            _ctx.weather.hourly[i].hour = (15 + i) % 24;
            _ctx.weather.hourly[i].tempC = 25.0f - 4.5f * sinf(i * 0.25f);
            _ctx.weather.hourly[i].precipProb = (i >= 2 && i <= 4) ? 75 : 5;
            _ctx.weather.hourly[i].weatherCode = (i >= 2 && i <= 4) ? 61 : 1;
        }
        unlock();
    }

    bool lock(TickType_t timeout = portMAX_DELAY) {
        if (!_mutex) return false;
        return xSemaphoreTakeRecursive(_mutex, timeout) == pdTRUE;
    }

    void unlock() {
        if (_mutex) {
            xSemaphoreGiveRecursive(_mutex);
        }
    }

    SystemContext& getContextUnsafe() {
        return _ctx;
    }

    void copySnapshotTo(SystemContext& out) {
        lock();
        out = _ctx;
        unlock();
    }

    SystemContext getSnapshot() {
        SystemContext snap;
        copySnapshotTo(snap);
        return snap;
    }

    void triggerFullTripReset();

    // Trail points stored in PSRAM (up to 4096 points for full-bleed map)
    static constexpr size_t MAX_TRAIL_POINTS = 4096;
    void pushTrailPoint(double lat, double lon, float speedKmh) {
        lock();
        if (!_trailPoints) {
            _trailPoints = (TrailPoint*)ps_malloc(sizeof(TrailPoint) * MAX_TRAIL_POINTS);
            _trailCount = 0;
        }
        if (_trailPoints) {
            if (_trailCount < MAX_TRAIL_POINTS) {
                _trailPoints[_trailCount++] = {lat, lon, speedKmh};
            } else {
                // Decimate 2:1 when full so entire trip always fits
                for (size_t i = 0; i < MAX_TRAIL_POINTS / 2; i++) {
                    _trailPoints[i] = _trailPoints[i * 2];
                }
                _trailCount = MAX_TRAIL_POINTS / 2;
                _trailPoints[_trailCount++] = {lat, lon, speedKmh};
            }
        }
        unlock();
    }

    size_t copyTrailPoints(TrailPoint* outBuf, size_t maxCount) {
        lock();
        size_t n = 0;
        if (_trailPoints && outBuf && _trailCount > 0) {
            n = (_trailCount < maxCount) ? _trailCount : maxCount;
            memcpy(outBuf, _trailPoints, n * sizeof(TrailPoint));
        }
        unlock();
        return n;
    }

    size_t getTrailPointCount() {
        lock();
        size_t c = _trailCount;
        unlock();
        return c;
    }

private:
    SystemState() : _mutex(nullptr), _trailPoints(nullptr), _trailCount(0) {}
    SemaphoreHandle_t _mutex;
    SystemContext     _ctx;
    TrailPoint*       _trailPoints;
    size_t            _trailCount;
};

inline void SystemState::triggerFullTripReset() {
    lock();
    float curAlt = _ctx.trip.currentAltitudeM;
    float curSpd = _ctx.gnss.speedKmh;
    double curLat = _ctx.gnss.latitude;
    double curLon = _ctx.gnss.longitude;
    bool hasFix = _ctx.gnss.isValid;

    // 1. Page 1 Dashboard TRIP reset
    _ctx.trip.tripDistanceKm = 0.0f;
    _ctx.trip.movingDurationSec = 0;
    _ctx.trip.idleDurationSec = 0;
    _ctx.trip.highwaySec = 0;
    _ctx.trip.urbanSec = 0;
    _ctx.trip.slowSec = 0;

    // 2. Page 2 Altitude extremes & history reset
    _ctx.trip.maxAltitudeM = curAlt;
    _ctx.trip.minAltitudeM = curAlt;
    _ctx.trip.cumulativeClimbM = 0.0f;
    _ctx.trip.verticalSpeedMps = 0.0f;
    _ctx.trip.altitudeHistory.reset();
    _ctx.trip.altitudeHistory.push(curAlt);

    // 3. Page 3 Speed extremes & history reset
    _ctx.trip.maxSpeedKmh = curSpd;
    _ctx.trip.avgSpeedKmh = curSpd;
    _ctx.trip.speedHistory.reset();
    _ctx.trip.speedHistory.push(curSpd);

    // 4. Page 4 Trail clear & re-anchor start point
    _trailCount = 0;
    if (hasFix) {
        _ctx.trip.startLatitude = curLat;
        _ctx.trip.startLongitude = curLon;
        _ctx.trip.hasStartAnchor = true;
        if (_trailPoints) {
            _trailPoints[_trailCount++] = {curLat, curLon, curSpd};
        }
    } else {
        _ctx.trip.hasStartAnchor = false;
    }

    // 5. Page 5 Dynamics peak G reset
    _ctx.trip.maxLateralG = 0.0f;
    _ctx.trip.maxLongitudinalG = 0.0f;

    // 6. TF Card GPX stream new segment request
    _ctx.storage.requestNewSegment = true;

    // Banner notification
    _ctx.tripResetBannerActive = true;
    _ctx.tripResetBannerExpireMs = millis() + 2500;
    unlock();
}
