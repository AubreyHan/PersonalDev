#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include "model/SystemState.h"

namespace CloudSyncConfig {
    constexpr const char* DEFAULT_CLIENT_ID    = "d3590ed6-52b3-4102-aeff-aad2292ab01c";
    constexpr const char* GRAPH_ROOT_FOLDER    = "VehicleTracks";
    constexpr uint32_t    AUTO_SYNC_INTERVAL_S = 300; // 5 min periodic check
}

class OneDriveSyncService {
public:
    static OneDriveSyncService& instance() {
        static OneDriveSyncService inst;
        return inst;
    }

    bool init();
    void startTask();

    // Authentication Actions (Device Code Flow)
    bool startDeviceLogin();
    void cancelDeviceLogin();
    void unbindAccount();

    // Sync Actions
    void triggerManualSync() { _manualSyncRequested = true; }
    void setAutoSync(bool enable);

    // Helpers
    uint32_t scanPendingFilesCount();
    bool isBound() const { return _isBound; }
    bool isSyncing() const { return _isSyncing; }

private:
    OneDriveSyncService();

    void taskLoop();
    static void taskTrampoline(void* arg);

    bool refreshAccessToken();
    bool fetchUserProfile();
    bool pollDeviceCodeApproval();

    bool syncTripsCsv();
    bool uploadSingleTrack(const char* relPath);
    bool isFileInSyncedLedger(const char* relPath);
    void markFileInSyncedLedger(const char* relPath);

    bool uploadSmallFile(const char* cloudPath, const char* localPath, const char* contentType);
    bool uploadLargeFileChunked(const char* cloudPath, const char* localPath);

    TaskHandle_t      _taskHandle           = nullptr;
    SemaphoreHandle_t _mutex                = nullptr;

    bool              _isBound              = false;
    bool              _isSyncing            = false;
    bool              _authInProgress       = false;
    bool              _autoSyncEnabled      = true;
    bool              _manualSyncRequested  = false;

    char              _clientId[64]         = {0};
    char              _refreshToken[1024]   = {0};
    char              _accessToken[2048]    = {0};
    uint32_t          _accessTokenExpireMs  = 0;

    // Device code auth state
    char              _deviceCode[128]      = {0};
    char              _userCode[16]         = {0};
    char              _verifyUrl[64]        = {0};
    uint32_t          _pollIntervalMs       = 5000;
    uint32_t          _pollExpireEpoch      = 0;
    uint32_t          _lastPollMs           = 0;

    char              _accountEmail[64]     = {0};
    uint32_t          _lastSyncEpoch        = 0;
    uint32_t          _lastAutoCheckMs      = 0;
};
