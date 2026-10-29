#pragma once
#include <Arduino.h>
#include <SdFat.h>
#include "model/SystemState.h"
#include "config.h"

struct LatLonGcj {
    double lat;
    double lon;
};

class TrackLogger {
public:
    static TrackLogger& instance() {
        static TrackLogger inst;
        return inst;
    }

    bool init();
    void update();
    bool formatSd();
    bool isMounted() const { return _isMounted; }
    bool isLogging() const { return _isLogging; }
    void stopLogging();

    static LatLonGcj wgs84ToGcj02(double wgsLat, double wgsLon);

private:
    TrackLogger() : _isMounted(false), _isLogging(false), _recordedPoints(0), _lastLogMs(0), _standstillSeconds(0), _closeTagsLen(24) {}

    SdFs          _sd;
    FsFile        _wFile;
    FsFile        _cFile;
    bool          _isMounted;
    bool          _isLogging;
    uint32_t      _recordedPoints;
    unsigned long _lastLogMs;
    uint32_t      _standstillSeconds;
    size_t        _closeTagsLen;
    char          _currentWName[64];
    char          _currentCName[64];

    bool startNewTrackFiles();
    void appendTrackPoint(const GnssData& gnss, float altM, float tempC, float pressHpa, const VehicleDynamics& dyn, uint8_t batPct);
    void writeGpxHeader(FsFile& f, const char* name, const char* desc, const char* isoTime);
    void checkStorageWatermark();
};
