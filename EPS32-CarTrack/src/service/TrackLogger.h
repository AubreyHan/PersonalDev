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
    void requestFormat() { _pendingFormat = true; } // Thread-safe async request, executed in trackLoggerTask
    void closeActiveStreams();
    void closeSingleStreams();
    void closeDailyStreams();
    void closeSubStreams();
    void writeNewSegmentTag();
    size_t listTrackFiles(TrackFileEntry* outList, size_t maxEntries);
    bool openFileForRead(const char* filename, FsFile& outFile);
    bool logPowerEvent(const char* eventType, const char* details);
    void lockSd() { if (_sdMutex) xSemaphoreTakeRecursive(_sdMutex, portMAX_DELAY); }
    void unlockSd() { if (_sdMutex) xSemaphoreGiveRecursive(_sdMutex); }

    static void wgs84ToGcj02(double wgLat, double wgLon, double& mgLat, double& mgLon);

    static constexpr uint64_t SUB_TRIP_MAX_BYTES = 10 * 1024 * 1024; // 10MB auto-split per volume

private:
    TrackLogger() {
        _sdMutex = xSemaphoreCreateRecursiveMutex();
    }

    bool openOrAppendGpx(FsFile& wgsFile, FsFile& gcjFile, const char* wName, const char* cName,
                         uint16_t y, uint8_t m, uint8_t d, uint8_t hh, uint8_t mm, uint8_t ss,
                         bool isContinuation);
    void appendTrackPoint(FsFile& file, double lat, double lon, const SystemContext& snap,
                          uint16_t y, uint8_t m, uint8_t d, uint8_t hh, uint8_t mm, uint8_t ss);
    void healUnclosedGpxFiles();
    void enforceStorageQuota50Percent();
    void refreshStorageStats();
    void prunePowerLogFifo(size_t incomingLen);
    void getFormattedGpsTimezoneTimestamp(char* outBuf, size_t maxLen);

    SemaphoreHandle_t _sdMutex              = nullptr;
    volatile bool     _pendingFormat        = false;
    SystemContext     _snapBuf              = {};
    SdFs              _sd;
    FsFile            _fileWgsSingle;
    FsFile            _fileGcjSingle;
    FsFile            _fileWgsDaily;
    FsFile            _fileGcjDaily;
    FsFile            _fileWgsSub;
    FsFile            _fileGcjSub;
    bool              _mounted              = false;
    bool              _singleRecording      = false;
    bool              _dailyRecording       = false;
    bool              _subRecording         = false;
    uint32_t          _singlePoints         = 0;
    uint32_t          _pointsSinceSync      = 0;
    uint32_t          _lastLogSec           = 0;
    uint32_t          _lastRecordedEpochSec = 0;
    uint32_t          _dailyDateInt         = 0;
    uint16_t          _subIndex             = 0;
    uint16_t          _subSplitIndex        = 1;
};
