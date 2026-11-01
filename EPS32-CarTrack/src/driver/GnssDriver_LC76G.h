#pragma once
#include <Arduino.h>
#include <TinyGPSPlus.h>
#include "config_s3_amoled.h"
#include "model/SystemState.h"

struct SatelliteInfo {
    uint8_t prn;
    uint8_t elevation;
    uint16_t azimuth;
    uint8_t snr; // C/N0 (dB-Hz)
};

class GnssDriver_LC76G {
public:
    static GnssDriver_LC76G& instance() {
        static GnssDriver_LC76G inst;
        return inst;
    }

    bool init();
    void hardwareReset();
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

    // Antenna & RF Health Diagnostics
    uint32_t getTotalBytes() const { return _totalBytes; }
    uint16_t getSatsInView() const { return _satsInView; }
    uint8_t getMaxSnr() const { return _maxSnr; }
    float getAvgSnr() const { return _avgSnr; }
    const char* getAntennaStatus() const { return _antennaStatus; }
    bool isUsingI2c() const { return _usingI2c; }

    // Hot Start & NVS Ephemeris / Snapshot Assist
    void saveNvsSnapshot();
    bool loadAndInjectHotStart();

private:
    GnssDriver_LC76G() = default;

    void parseNmeaStream();
    void parseNmeaSentence(const char* sentence);
    void parseGsvSentence(const char* sentence);
    void updateStabilityFilter();
    void updateAltitudeFilter(float rawMslAlt);
    void pollI2cStream();
    void logDiagnostics();

    TinyGPSPlus _gps;
    bool _isOnline         = false;
    bool _isStable         = false;
    bool _isStaticLocked   = false;
    bool _usingI2c         = false;

    // Convergence filter tracking
    uint32_t _stableStartTimeMs = 0;
    uint32_t _lastNvsSaveMs     = 0;
    uint32_t _lastDiagMs        = 0;
    uint32_t _totalBytes        = 0;
    float    _hdop              = 99.9f;
    float    _vdop              = 99.9f;
    uint8_t  _activeSats        = 0;

    // Antenna & RF Telemetry
    uint16_t _satsInView        = 0;
    uint8_t  _maxSnr            = 0;
    float    _avgSnr            = 0.0f;
    char     _antennaStatus[24] = "CHECKING"; // OK / OPEN / SHORT / INDOOR_WEAK
    SatelliteInfo _topSats[12];
    uint8_t  _topSatCount       = 0;

    // Altitude 1D Kalman Filter state
    float _filteredAltitude     = 0.0f;
    float _altVarianceEstimate  = 4.0f;
    float _lastRawAltitude      = 0.0f;
    uint32_t _lastAltUpdateMs   = 0;
    bool  _altFilterInitialized = false;

    // NMEA line buffer
    char _nmeaBuffer[160];
    uint8_t _nmeaIndex = 0;
};
