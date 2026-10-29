#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <IPAddress.h>

// =============================================================================
// M5Track-M9N Core Data Models & Global State Contract
// =============================================================================

enum GnssFixType : uint8_t {
    FIX_NONE      = 0, // No Fix
    FIX_DEAD_REC  = 1, // Dead Reckoning only
    FIX_2D        = 2, // 2D Plane Fix
    FIX_3D        = 3, // 3D Spherical Fix
    FIX_GNSS_DR   = 4, // GNSS + DR Fusion
    FIX_TIME_ONLY = 5  // Time-only lock
};

struct SatInfo {
    uint8_t gnssId;    // 0: GPS, 1: SBAS, 2: Galileo, 3: BeiDou, 5: QZSS, 6: GLONASS
    uint8_t svId;      // Satellite SV ID
    uint8_t cno;       // Carrier to Noise Ratio (dBHz, 0~63)
    int8_t  elev;      // Elevation (deg, -90~90)
    int16_t azim;      // Azimuth (deg, 0~360)
    bool    used;      // Used in navigation solution
};

struct GnssData {
    bool        isValid;        // Position validity
    GnssFixType fixType;        // >= FIX_3D is considered reliable
    int32_t     latE7;          // Lat * 1e7
    int32_t     lonE7;          // Lon * 1e7
    double      latitude;       // Degrees
    double      longitude;      // Degrees
    float       altitudeMeters; // Altitude from MSL (m)
    float       speedKmh;       // Ground speed (km/h)
    float       headingDeg;     // True track heading (0.0~359.9 deg)
    float       hdop;           // Horizontal Dilution of Precision
    float       pdop;           // Position Dilution of Precision
    float       vdop;           // Vertical Dilution of Precision
    uint8_t     numSats;        // Total satellites used in solution
    uint8_t     numBdsSats;     // BeiDou
    uint8_t     numGpsSats;     // GPS
    uint8_t     numGloSats;     // GLONASS
    uint8_t     numGalSats;     // Galileo
    
    // Dead Reckoning & Tunnel Constraint
    bool        isDeadReckoning; // Currently in IMU Dead Reckoning mode
    uint32_t    drDurationSec;   // Seconds elapsed in DR mode
    bool        isInTunnel;      // Inside a long tunnel axis-constrained mode
    char        tunnelName[32];  // e.g. "Tianshan Victory Tunnel"
    float       tunnelProgressKm;// Distance driven inside tunnel (km)
    float       tunnelLengthKm;  // Total tunnel design length (km)
    
    // RF Diagnosis & Jamming (UBX-MON-RF)
    uint8_t     jammingState;    // 0: Normal, 1: Warning, 2: Critical Jamming
    uint8_t     antennaStatus;   // 1: Normal, 2: Open-Circuit, 3: Short-Circuit
    uint16_t    rfNoiseLevel;    // noisePerMS
    
    // Accurate UTC Time
    uint16_t    year;
    uint8_t     month;
    uint8_t     day;
    uint8_t     hour;
    uint8_t     minute;
    uint8_t     second;
    uint16_t    millisecond;
    
    // Active satellite constellation list
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
    static constexpr size_t CAPACITY = 120; // 30s interval = 60 mins history
    float    samples[CAPACITY];
    size_t   headIdx;
    size_t   count;
    uint32_t lastSampleEpoch;

    void clear() {
        headIdx = 0;
        count = 0;
        lastSampleEpoch = 0;
        for (size_t i = 0; i < CAPACITY; i++) samples[i] = 0.0f;
    }

    void addSample(float val, uint32_t nowEpoch) {
        if (nowEpoch - lastSampleEpoch >= 30 || count == 0) {
            lastSampleEpoch = nowEpoch;
            samples[headIdx] = val;
            headIdx = (headIdx + 1) % CAPACITY;
            if (count < CAPACITY) count++;
        }
    }

    float getSample(size_t ageIndex) const {
        // ageIndex: 0 = oldest, count-1 = newest
        if (count == 0 || ageIndex >= count) return 0.0f;
        size_t startIdx = (count < CAPACITY) ? 0 : headIdx;
        size_t realIdx = (startIdx + ageIndex) % CAPACITY;
        return samples[realIdx];
    }
};

struct TripStats {
    uint32_t startTimeSec;
    uint32_t durationSec;
    float    distanceKm;
    float    currentSpeedKmh;
    float    maxSpeedKmh;
    float    avgSpeedKmh;
    float    startAltitude;
    float    maxAltitude;
    float    minAltitude;
    float    cumulativeClimb;
    
    HistoryBuffer altHistory;
    HistoryBuffer speedHistory;

    void reset(float currentAlt, float currentSpd, uint32_t nowSec) {
        startTimeSec = nowSec;
        durationSec = 0;
        distanceKm = 0.0f;
        currentSpeedKmh = currentSpd;
        maxSpeedKmh = currentSpd;
        avgSpeedKmh = currentSpd;
        startAltitude = currentAlt;
        maxAltitude = currentAlt;
        minAltitude = currentAlt;
        cumulativeClimb = 0.0f;
        altHistory.clear();
        speedHistory.clear();
        altHistory.addSample(currentAlt, nowSec);
        speedHistory.addSample(currentSpd, nowSec);
    }
};

struct VehicleDynamics {
    float lateralG;        // + Right centrifugal, - Left
    float longG;           // + Push-back accel, - Braking
    float totalG;          // sqrt(Gx^2 + Gy^2)
    float maxLateralG;     // Peak lateral G
    float maxLongG;        // Peak longitudinal G
    float pitchDeg;        // -45° to +45°
    float rollDeg;         // -45° to +45°
    float roadSlopePercent;// e.g. +7.5%
};

enum TrailViewMode : uint8_t {
    TRAIL_VIEW_FIT_ENTIRE = 0, // Full path fit with bounding box
    TRAIL_VIEW_FOLLOW_5KM = 1  // Local cruise follow (5km radius)
};

struct TrailPoint {
    int32_t latE7;
    int32_t lonE7;
    int16_t altM;
};

struct TrailState {
    static constexpr size_t MAX_POINTS = 800;
    TrailPoint points[MAX_POINTS];
    size_t     count;
    TrailViewMode viewMode;
    int32_t    minLatE7;
    int32_t    maxLatE7;
    int32_t    minLonE7;
    int32_t    maxLonE7;
    float      distanceToStartKm;
    float      bearingToStartDeg;

    void reset(int32_t latE7, int32_t lonE7, int16_t altM) {
        count = 0;
        viewMode = TRAIL_VIEW_FIT_ENTIRE;
        minLatE7 = maxLatE7 = latE7;
        minLonE7 = maxLonE7 = lonE7;
        distanceToStartKm = 0.0f;
        bearingToStartDeg = 0.0f;
        addPoint(latE7, lonE7, altM);
    }

    void addPoint(int32_t latE7, int32_t lonE7, int16_t altM) {
        if (count < MAX_POINTS) {
            points[count++] = {latE7, lonE7, altM};
        } else {
            // Drop every other point for simple spatial thinning when full
            size_t newCount = 0;
            for (size_t i = 0; i < MAX_POINTS; i += 2) {
                points[newCount++] = points[i];
            }
            count = newCount;
            points[count++] = {latE7, lonE7, altM};
        }
        if (latE7 < minLatE7) minLatE7 = latE7;
        if (latE7 > maxLatE7) maxLatE7 = latE7;
        if (lonE7 < minLonE7) minLonE7 = lonE7;
        if (lonE7 > maxLonE7) maxLonE7 = lonE7;
    }
};

enum AppState : uint8_t {
    STATE_BOOT_INIT,      // Boot check & satellite search
    STATE_WIFI_CONFIG,    // SoftAP & WebPortal portal mode
    STATE_MAIN_APP,       // Main 8-page dashboard system
    STATE_PWR_OFF_WARN,   // 10s power-off countdown dialog
    STATE_SILENT_ASSESS,  // 3s motion anti-noise assessment window
    STATE_DEEP_SLEEP      // Deep sleep mode (<35uA)
};

struct HourlyForecastItem {
    uint8_t hour;             // 0~23
    int8_t  tempC;            // -40~50 °C
    uint8_t rainProbPercent;  // 0~100 %
    uint8_t weatherCode;      // WMO code: 0:clear, 1-3:cloudy, 51-67:rain, 71-77:snow, 95:thunder
};

struct WeatherInfo {
    bool     hasData;
    float    temperatureC;
    uint8_t  relativeHumidity;
    uint8_t  currentWeatherCode;
    float    windSpeedKmh;
    uint32_t lastUpdateEpoch;
    
    HourlyForecastItem hourly[24];
    uint8_t            currentSlotIdx; // 0: 0~8h, 1: 8~16h, 2: 16~24h
};

enum ClockSlotMode : uint8_t {
    CLOCK_MODE_GPS_AUTO    = 0, // Auto longitude timezone
    CLOCK_MODE_MANUAL_CITY = 1  // Preset city list
};

enum CityRegion : uint8_t {
    REGION_ASIA_PACIFIC = 0,
    REGION_AMERICAS     = 1,
    REGION_EUROPE       = 2,
    REGION_UTC_STANDARD = 3
};

struct WorldCityDef {
    const char* cityNameCn;
    const char* cityNameEn;
    int8_t      standardUtcOffsetHours;
    bool        hasDst;
    uint8_t     region;
};

struct WorldClockSlotConfig {
    ClockSlotMode mode;
    uint8_t       cityIndex;
    int8_t        utcOffsetHours;
    char          displayName[20];
    bool          isDstActive;
};

struct WorldClockState {
    WorldClockSlotConfig slots[4];
    uint8_t              activeEditSlot;
    uint8_t              activeEditRegion;
};

enum BatteryHardwareMode : uint8_t {
    BATTERY_MODE_NO_BASE_EMPTY = 0, // Phase 1: No base (USB only, empty icon)
    BATTERY_MODE_BASE_ATTACHED = 1  // Phase 2: Base Dual 16340 detected
};

struct PowerMonitorData {
    bool     isAvailable;        // INA226 online
    bool     isCharging;         // Charging via external power
    float    busVoltageV;        // 3.0 ~ 4.2V
    float    shuntVoltageMv;     // Shunt voltage drop
    float    currentMa;          // Transient current (+ charge, - discharge)
    float    avgCurrentMa;       // Rolling average current
    float    powerMw;            // Instant power
    float    remainingMah;       // Coulomb counter capacity (0~1400mAh)
    float    batterySocPercent;  // 0~100 %
    uint16_t remainingMinutes;   // Estimated runtime
};

struct FullTripResetPayload {
    uint32_t resetEpoch;
    int32_t  currentLatE7;
    int32_t  currentLonE7;
    float    currentAltM;
    float    currentSpeedKmh;
};

namespace Viewport {
    constexpr int16_t SCREEN_W   = 320;
    constexpr int16_t SCREEN_H   = 240;
    
    // Top Status Bar (24px)
    constexpr int16_t TOP_BAR_X          = 0;
    constexpr int16_t TOP_BAR_Y          = 0;
    constexpr int16_t TOP_BAR_W          = 320;
    constexpr int16_t TOP_BAR_H          = 24;
    constexpr int16_t TOP_BAR_HIT_H      = 30;
    constexpr int16_t HITBOX_TIME_W      = 95;  // X: 0~95 -> Jump to Page 7
    constexpr int16_t STATUS_BADGE_X     = 96;  // X: 96~225 Status & Battery
    constexpr int16_t STATUS_BADGE_W     = 130;
    constexpr int16_t HITBOX_WEATHER_X   = 226; // X: 226~319 -> Jump to Page 6
    constexpr int16_t HITBOX_WEATHER_W   = 94;
    
    // Full-Width Safe Area
    constexpr int16_t SAFE_X     = 0;
    constexpr int16_t SAFE_Y     = 24;
    constexpr int16_t SAFE_W     = 320;
    constexpr int16_t SAFE_H     = 216;
    
    // Floating Indicator Overlay
    constexpr int16_t  INDICATOR_OSD_Y        = 228;
    constexpr int16_t  INDICATOR_OSD_H        = 8;
    constexpr uint32_t INDICATOR_AUTO_HIDE_MS = 3000;
}

namespace GestureConfig {
    constexpr int16_t SWIPE_HORIZ_THRESHOLD_PX = 24;
    constexpr int16_t SWIPE_DRAG_TRIGGER_PX    = 34;
    constexpr int16_t SWIPE_UP_THRESHOLD_PX    = -24;
    constexpr int16_t SWIPE_DOWN_THRESHOLD_PX  = 24;
    constexpr float   DIRECTION_RATIO          = 1.15f;
}

namespace TripResetConfig {
    constexpr int16_t  HITBOX_TRIP_X      = 160;
    constexpr int16_t  HITBOX_TRIP_Y      = 180;
    constexpr int16_t  HITBOX_TRIP_W      = 152;
    constexpr int16_t  HITBOX_TRIP_H      = 50;
    constexpr uint32_t LONG_PRESS_MS      = 2000;
    constexpr uint32_t VIBRATION_FEEDBACK_MS = 100;
}

struct SystemContext {
    AppState    currentState;
    uint8_t     mainPageIdx;      // 0:Dash, 1:Alt, 2:Speed, 3:Trail, 4:Dynamics, 5:Weather, 6:Clock, 7:Tools
    uint8_t     totalMainPages;   // Default 8
    bool        isWifiConnected;
    int8_t      wifiRssi;
    String      wifiSsid;
    IPAddress   localIp;
    bool        isSdMounted;
    bool        isLogging;
    String      currentGpxName;
    uint32_t    recordedPoints;
    
    GnssData         gnss;
    TripStats        trip;
    VehicleDynamics  dynamics;
    TrailState       trail;
    WeatherInfo      weather;
    WorldClockState  worldClock;
    PowerMonitorData powerMonitor;
    
    bool                vbusConnected;
    BatteryHardwareMode batteryMode;
    uint8_t             batteryPercent;
    bool                isCharging;
    int8_t              pwrCountdownSec;
    bool                sdFormatRequired;
};

class SystemState {
public:
    static SystemState& instance() {
        static SystemState inst;
        return inst;
    }

    void init() {
        _mutex = xSemaphoreCreateMutex();
        SystemContext& ctx = _context;
        ctx.currentState = STATE_BOOT_INIT;
        ctx.mainPageIdx = 0;
        ctx.totalMainPages = 8;
        ctx.isWifiConnected = false;
        ctx.wifiRssi = -100;
        ctx.wifiSsid = "";
        ctx.localIp = IPAddress(0, 0, 0, 0);
        ctx.isSdMounted = false;
        ctx.isLogging = false;
        ctx.currentGpxName = "";
        ctx.recordedPoints = 0;
        ctx.vbusConnected = true;
        ctx.batteryMode = BATTERY_MODE_NO_BASE_EMPTY;
        ctx.batteryPercent = 0;
        ctx.isCharging = false;
        ctx.pwrCountdownSec = 10;
        ctx.sdFormatRequired = false;

        memset(&ctx.gnss, 0, sizeof(ctx.gnss));
        memset(&ctx.dynamics, 0, sizeof(ctx.dynamics));
        ctx.trip.reset(0, 0, 0);
        ctx.trail.reset(0, 0, 0);

        ctx.weather.hasData = false;
        ctx.weather.currentSlotIdx = 0;
        ctx.weather.temperatureC = 0.0f;
        ctx.weather.relativeHumidity = 0;

        // Default 4 world clock slots: Beijing (UTC+8), UTC (UTC+0), New York (EDT-4), Tokyo (UTC+9)
        ctx.worldClock.activeEditSlot = 0;
        ctx.worldClock.activeEditRegion = 0;
        
        ctx.worldClock.slots[0] = {CLOCK_MODE_MANUAL_CITY, 0, 8, "北京 Beijing", false};
        ctx.worldClock.slots[1] = {CLOCK_MODE_MANUAL_CITY, 3, 0, "UTC 伦敦", false};
        ctx.worldClock.slots[2] = {CLOCK_MODE_MANUAL_CITY, 1, -4, "纽约 New York", true};
        ctx.worldClock.slots[3] = {CLOCK_MODE_MANUAL_CITY, 2, 9, "东京 Tokyo", false};

        ctx.powerMonitor.isAvailable = false;
        ctx.powerMonitor.batterySocPercent = 0.0f;
        ctx.powerMonitor.busVoltageV = 0.0f;
        ctx.powerMonitor.currentMa = 0.0f;
    }

    void lock() {
        if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    }

    void unlock() {
        if (_mutex) xSemaphoreGive(_mutex);
    }

    SystemContext& getContextUnsafe() {
        return _context;
    }

private:
    SystemState() : _mutex(nullptr) {}
    SemaphoreHandle_t _mutex;
    SystemContext     _context;
};
