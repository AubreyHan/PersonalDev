#pragma once
#include <Arduino.h>
#include <SdFat.h>
#include "model/SystemState.h"

struct TrackFileEntry {
    char     name[44];
    uint32_t sizeBytes;
    bool     isGcj02;
};

class TrackLogger {
public:
    static TrackLogger& instance() {
        static TrackLogger inst;
        return inst;
    }

    bool init();
    void update();
    bool formatFat32();
    void closeActiveStreams();
    void writeNewSegmentTag();
    size_t listTrackFiles(TrackFileEntry* outList, size_t maxEntries);
    bool openFileForRead(const char* filename, FsFile& outFile);

    static void wgs84ToGcj02(double wgLat, double wgLon, double& mgLat, double& mgLon);

private:
    TrackLogger() = default;

    bool openNewTrackPair(const SystemContext& snap);
    void appendTrackPoint(FsFile& file, double lat, double lon, const SystemContext& snap);
    void healUnclosedGpxFiles();
    void enforceStorageQuota50Percent();
    void refreshStorageStats();

    SdFs     _sd;
    FsFile   _fileWgs;
    FsFile   _fileGcj;
    bool     _mounted            = false;
    bool     _recording          = false;
    uint32_t _pointsInFile       = 0;
    uint32_t _pointsSinceSync    = 0;
    uint32_t _lastLogSec         = 0;
};
