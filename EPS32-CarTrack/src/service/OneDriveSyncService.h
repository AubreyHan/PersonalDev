#pragma once

#include <Arduino.h>
#ifdef FILE_READ
#undef FILE_READ
#endif
#ifdef FILE_WRITE
#undef FILE_WRITE
#endif
#include <FS.h>
#include <SdFat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <functional>
#include <vector>
#include "../config.h"
#include "../model/SystemState.h"

// =============================================================================
// OneDriveSyncService - Microsoft Graph OneDrive Incremental Cloud Sync Service
// Strictly compliant with Official Microsoft Identity & Graph Spec (#3 §3.2):
//   1. OAuth 2.0 Device Code Flow (RFC 8628): non-blocking /token polling,
//      authorization_pending, slow_down (+5s), refresh_token rotation & NVS save.
//   2. Incremental Ledger (/tracks/.synced_ledger): scans closed GPX files in
//      /tracks/{single,daily,sub} + trips.csv, skipping active streams & unclosed GPX.
//   3. Dual-Mode Graph Upload Engine:
//      - < 4 MiB: PUT .../content (with Authorization: Bearer)
//      - >= 4 MiB: POST .../createUploadSession, then PSRAM 327,680 B (320 KiB)
//        chunked PUT to pre-signed uploadUrl WITHOUT Authorization header and
//        strictly formatted Content-Range: bytes start-end/total (202 -> 200/201).
//   4. Pre-Standby Handshake: answers PowerData::preSleepCloudSyncReq within 25s
//      and asserts PowerData::preSleepCloudSyncAck.
// =============================================================================
class OneDriveSyncService {
public:
    using SdLockCallback   = std::function<bool(uint32_t timeoutMs)>;
    using SdUnlockCallback = std::function<void()>;

    static constexpr size_t   DIRECT_PUT_LIMIT_BYTES = CloudConfig::ONEDRIVE_DIRECT_PUT_MAX_BYTES; // 4 MiB
    static constexpr size_t   SESSION_CHUNK_BYTES    = CloudConfig::ONEDRIVE_UPLOAD_CHUNK_BYTES;   // 327,680 B (320 KiB)
    static_assert((SESSION_CHUNK_BYTES % CloudConfig::ONEDRIVE_CHUNK_UNIT_BYTES) == 0,
                  "OneDrive upload chunk size MUST be an exact multiple of 327,680 bytes (320 KiB)!");

    static OneDriveSyncService& instance();

    OneDriveSyncService();
    ~OneDriveSyncService();

    OneDriveSyncService(const OneDriveSyncService&)            = delete;
    OneDriveSyncService& operator=(const OneDriveSyncService&) = delete;

    // Initialize NVS credentials & restore bind state
    bool begin(SdFs* sd = nullptr, SemaphoreHandle_t sdMutex = nullptr, bool isRecursiveMutex = false);

    // Attach TF card SdFs instance and thread-safety lock (supports mutex or custom callbacks)
    void attachSdStorage(SdFs* sd, SemaphoreHandle_t sdMutex = nullptr, bool isRecursiveMutex = false);
    void setSdLockCallbacks(SdLockCallback lockCb, SdUnlockCallback unlockCb);

    // Non-blocking periodic tick (call from netTask on Core 0)
    void update();

    // OAuth 2.0 Device Code Flow & Account Management
    bool startDeviceCodeFlow();
    void cancelDeviceCodeFlow();
    bool refreshAccessToken();
    void unbindAccount();
    bool isBound() const { return _isBound; }

    // Manual & Pre-Sleep Sync Entry Points
    bool syncPendingFiles(uint32_t maxDurationMs = 0);
    bool syncBeforeSleep(uint32_t timeoutMs = CloudConfig::ONEDRIVE_PRE_SLEEP_TIMEOUT_MS);

    // Refresh pending/synced file counters from TF card & ledger
    bool refreshLedgerStats();

private:
    struct LedgerEntry {
        String   relPath;
        uint64_t syncedSize = 0;
    };

    struct PendingUploadItem {
        String   relPath;  // e.g. "trips.csv", "single/W-20261111-080000.gpx"
        String   fullPath; // e.g. "/tracks/trips.csv", "/tracks/single/W-20261111-080000.gpx"
        uint64_t fileSize = 0;
    };

    bool lockSd(uint32_t timeoutMs = 2000) const;
    void unlockSd() const;

    void loadConfigAndTokensFromNvs();
    void resolveClientConfig(String& outClientId, String& outTenant) const;
    void pollDeviceCodeTokenOnce();
    bool ensureValidAccessToken();
    void fetchUserProfileEmail();

    bool loadLedger(std::vector<LedgerEntry>& outEntries) const;
    bool appendLedgerRecord(const String& relPath, uint64_t fileSize);
    bool collectPendingUploads(std::vector<PendingUploadItem>& outPending, uint16_t& outSyncedCount) const;
    bool isGpxFileProperlyClosedLocked(const char* fullPath, uint64_t fileSize) const;
    bool readFileSliceSafe(const char* fullPath, uint64_t offset, uint8_t* dstBuf, size_t len) const;

    bool uploadSingleFile(const PendingUploadItem& item, uint32_t deadlineMs, bool hasDeadline);
    bool uploadSmallFileDirectPut(const PendingUploadItem& item);
    bool uploadLargeFileSession(const PendingUploadItem& item, uint32_t deadlineMs, bool hasDeadline);
    bool createGraphUploadSession(const String& relPath, String& outUploadUrl);

    void setLastErrorState(OneDriveBindState bindSt, const char* errMsg);
    void setTransferError(const char* errMsg);

    static String urlEncodeFormComponent(const String& str);
    static String urlEncodeGraphPath(const String& relPath);

    SdFs*             _sd;
    SemaphoreHandle_t _sdMutex;
    bool              _sdMutexRecursive;
    SdLockCallback    _sdLockCb;
    SdUnlockCallback  _sdUnlockCb;

    bool              _initialized;
    bool              _isBound;
    bool              _autoSyncEnabled;

    String            _accessToken;
    String            _refreshToken;
    String            _accountEmail;
    uint32_t          _accessTokenExpiryMs;

    // Device Code Flow state
    String            _deviceCode;
    uint32_t          _deviceCodeStartMs;
    uint32_t          _deviceCodeExpiresMs;
    uint32_t          _pollIntervalMs;
    uint32_t          _lastPollMs;
    uint32_t          _lastCountdownTickMs;

    // Background sync scheduling
    uint32_t          _lastBgSyncCheckMs;
    uint8_t*          _psramChunkBuf;
};
