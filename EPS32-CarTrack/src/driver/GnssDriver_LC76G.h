#pragma once
#include <Arduino.h>
#include <TinyGPSPlus.h>
#include "config_s3_amoled.h"
#include "model/SystemState.h"

class GnssDriver_LC76G {
public:
    static GnssDriver_LC76G& instance() {
        static GnssDriver_LC76G inst;
        return inst;
    }

    bool init();
    void update();
    void configureAutomotiveModel();
    void sendPairCommand(const char* cmd);

    // Convergence & Stability Management
    bool isStable() const { return _isStable; }
    bool isStaticLocked() const { return _isStaticLocked; }
    float getFilteredAltitude() const { return _filteredAltitude; }
    float getVdop() const { return _vdop; }
    float getHdop() const { return _hdop; }
    uint8_t getActiveSats() const { return _activeSats; }

    // Hot Start & NVS Ephemeris / Snapshot Assist
    void saveNvsSnapshot();
    bool loadAndInjectHotStart();

private:
    GnssDriver_LC76G() = default;

    void parseNmeaStream();
    void updateStabilityFilter();
    void updateAltitudeFilter(float rawMslAlt);

    TinyGPSPlus _gps;
    bool _isOnline         = false;
    bool _isStable         = false;
    bool _isStaticLocked   = false;

    // Convergence filter tracking
    uint32_t _stableStartTimeMs = 0;
    uint32_t _lastNvsSaveMs     = 0;
    float    _hdop              = 99.9f;
    float    _vdop              = 99.9f;
    uint8_t  _activeSats        = 0;

    // Altitude 1D Kalman Filter state
    float _filteredAltitude     = 0.0f;
    float _altVarianceEstimate  = 4.0f; // Initial variance P
    float _lastRawAltitude      = 0.0f;
    uint32_t _lastAltUpdateMs   = 0;
    bool  _altFilterInitialized = false;

    // NMEA line buffer
    char _nmeaBuffer[128];
    uint8_t _nmeaIndex = 0;
};
