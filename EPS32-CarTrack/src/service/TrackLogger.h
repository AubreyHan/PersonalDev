#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>
#include <FatLib/FatFormatter.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "../config.h"
#include "../model/SystemState.h"

// =============================================================================
// TrackLogger - Dedicated SPI SdFat Dual-Stream (WGS-84 / GCJ-02) GPX Engine
// - Mounts Tab5 MicroSD on dedicated FSPI (SCK=43, MISO=39, MOSI=44, CS=42,
//   GPIO40/41 INPUT_PULLUP) avoiding SDMMC contention with ESP32-C6 Wi-Fi 6.
// - Supports 1-tap in-device FAT32 formatting via FatFormatter + directory init.
// - Parallel W-*.gpx (WGS-84) & C-*.gpx (GCJ-02) 1Hz logging across:
//     1) /tracks/single/ (auto-splits every 3600 pts / 1h)
//     2) /tracks/daily/  (auto-stitches <trkseg> same day, rolls over at 00:00)
//     3) /tracks/sub/    (strictly splits at <= 10 MiB [10,485,760 B] volumes)
// - 4-Stage Power-Loss Pipeline: atomic pre-closed XML footer, 10s sync(),
//   boot unclosed-GPX self-healer, and monotonic UTC timestamp guard.
// - Enforces <= 50% TF card quota (evicts oldest W/C pairs down to <= 40%).
// - Maintains /tracks/trips.csv ledger & 100KB FIFO /tracks/power.log.
// =============================================================================

struct TrackFileEntry {
    char     relPath[64] = "";    // Relative to /tracks/, e.g. "single/W-20261111-083000.gpx"
    uint64_t sizeBytes   = 0;
    uint16_t fatDate     = 0;
    uint16_t fatTime     = 0;
    bool     isActive    = false; // True if currently open for active GPX recording
};

class TrackLogger {
public:
    // Recursive RAII lock guard for thread-safe SD access across Storage/Web/Cloud tasks
    class SdGuard {
    public:
        explicit SdGuard(SemaphoreHandle_t mutex, TickType_t timeout = portMAX_DELAY)
            : _mutex(mutex), _locked(false) {
            if (_mutex != nullptr) {
                _locked = (xSemaphoreTakeRecursive(_mutex, timeout) == pdTRUE);
            }
        }
        ~SdGuard() {
            if (_locked && _mutex != nullptr) {
                xSemaphoreGiveRecursive(_mutex);
            }
        }
        SdGuard(const SdGuard&)            = delete;
        SdGuard& operator=(const SdGuard&) = delete;
        bool locked() const { return _locked; }
        explicit operator bool() const { return _locked; }

    private:
        SemaphoreHandle_t _mutex;
        bool              _locked;
    };

    static TrackLogger& instance();

    TrackLogger();

    // Mount dedicated SPI TF card, inspect FAT32 compatibility, heal unclosed GPX
    // tails, enforce 50% quota, restore NVS trip persistence, and log BOOT event.
    bool begin();

    // Periodic service tick (call at >= 2~5Hz from Core 1 StorageTask; internally
    // paces GPX point writes to 1Hz while responding immediately to commands/settle).
    void update();

    // One-tap in-device FAT32 formatter via SdFat FatFormatter + directory tree setup
    bool formatToFat32();

    // Gracefully settle & close active single/daily/sub GPX streams (used before sleep/cutoff)
    void flushAndCloseAllStreams(bool writeSingleTripCsv = true);

    // Append a timestamped line to /tracks/power.log (100KB cap, auto-trims oldest 20KB)
    bool appendPowerLog(const char* eventName, const char* details = "");

    // WGS-84 -> GCJ-02 Krasovsky 1940 polynomial encryption (identity outside China)
    static bool isInsideChina(double lat, double lon);
    static void wgs84ToGcj02(double wgsLat, double wgsLon, double& outGcjLat, double& outGcjLon);

    // Thread-safe accessors & read/manage helpers for NetworkManager & OneDriveSyncService
    SemaphoreHandle_t sdMutex() const { return _sdMutex; }
    SdFs*             sdFs() { return _mounted ? &_sd : nullptr; }
    bool              isMounted() const { return _mounted; }
    bool              isPathActiveOpen(const char* relPath) const;

    size_t  listTrackFiles(TrackFileEntry* outList, size_t maxEntries, const char* folderFilter = nullptr);
    bool    getFileSize(const char* relOrAbsPath, uint64_t& outSize);
    int32_t readFileChunk(const char* relOrAbsPath, uint64_t offset, uint8_t* dstBuf, size_t maxLen);
    bool    deleteTrackOrPair(const char* relPath);
    bool    deleteAllTracks();
    void    refreshStorageStats();

private:
    struct DualGpxStream {
        bool     isOpen         = false;
        FsFile   wgsFile;
        FsFile   gcjFile;
        char     relWgsPath[48] = ""; // Relative to /tracks/, e.g. "single/W-20261111-083000.gpx"
        char     relGcjPath[48] = "";
        uint64_t wgsInsertPos   = 0;  // Write cursor immediately prior to closing XML tail
        uint64_t gcjInsertPos   = 0;
        uint32_t pointsInFile   = 0;
        uint32_t startEpochUtc  = 0;
    };

    bool mountCardLocked();
    bool ensureDirectoriesAndLedgerLocked();
    uint16_t healUnclosedGpxFilesLocked();
    bool healSingleGpxFileLocked(const char* fullPath);

    void loadNvsState();
    void saveSubTripNvsState(bool active, uint16_t subIdx, uint16_t splitIdx);
    void saveDailyNvsState(uint32_t ymd, float dayDistKm);

    void handlePendingCommands(const SystemStateSnapshot& snap);
    void handleDailyRollover(const SystemStateSnapshot& snap, uint32_t localYmd);

    bool openNewDualStreamLocked(DualGpxStream& stream,
                                 const char* relWgs,
                                 const char* relGcj,
                                 const char* trkName,
                                 uint32_t nowUtc);
    bool openOrAppendDualStreamLocked(DualGpxStream& stream,
                                      const char* relWgs,
                                      const char* relGcj,
                                      const char* trkName,
                                      uint32_t nowUtc);
    bool prepareExistingGpxForAppendLocked(FsFile& file, uint64_t& outInsertPos);
    bool breakSegmentInDualStreamLocked(DualGpxStream& stream);
    void closeDualStreamLocked(DualGpxStream& stream);
    void syncOpenStreamsLocked();

    bool writeTrackPointToStreamLocked(DualGpxStream& stream,
                                       const SystemStateSnapshot& snap,
                                       uint32_t monoEpochUtc,
                                       uint16_t monoMs,
                                       double gcjLat,
                                       double gcjLon);

    bool appendTripCsvRowLocked(const char* categoryLabel,
                                const TripStats& stats,
                                uint32_t endEpochUtc,
                                int16_t tzOffsetMin,
                                const char* gpxRelPath);

    void enforceStorageQuota50PercentLocked();
    uint64_t computeFolderBytesLocked(const char* dirPath,
                                      uint16_t* outWgsCount = nullptr);
    void resolveFullTracksPath(const char* relOrAbsPath, char* outFull, size_t outSize) const;
    void syncActivePathsToState();

    static void fatDateTimeCallback(uint16_t* date, uint16_t* time);

    SemaphoreHandle_t _sdMutex          = nullptr;
    SPIClass          _sdSpi;
    SdFs              _sd;
    bool              _cardPresent      = false;
    bool              _mounted          = false;
    bool              _formatIncompat   = false;
    StorageFsType     _fsType           = STORAGE_FS_NONE;

    DualGpxStream     _singleStream;
    DualGpxStream     _dailyStream;
    DualGpxStream     _subStream;

    uint16_t          _singleSplitSeq   = 1;
    uint32_t          _currentDailyYmd  = 0;
    bool              _manualTripActive = false;
    uint16_t          _subIndex         = 0;
    uint16_t          _subVolumeIndex   = 1;

    uint32_t          _lastRecordTickMs = 0;
    uint32_t          _lastSyncMs       = 0;
    uint32_t          _lastQuotaCheckMs = 0;
    uint32_t          _lastMonotonicUtc = 0;
    uint16_t          _lastMonotonicMs  = 0;
    uint32_t          _seenResetVersion = 1;

    // Pre-reset cached TripStats snapshots so trips.csv always logs accurate totals
    TripStats         _cachedSingleStats = {};
    TripStats         _cachedDailyStats  = {};
    TripStats         _cachedSubStats    = {};
    char              _lastSingleWgsRel[48] = "";
    char              _lastDailyWgsRel[48]  = "";
    char              _lastSubWgsRel[48]    = "";
};
