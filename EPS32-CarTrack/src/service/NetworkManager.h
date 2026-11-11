#pragma once

#include <Arduino.h>
#ifdef FILE_READ
#undef FILE_READ
#endif
#ifdef FILE_WRITE
#undef FILE_WRITE
#endif
#include <FS.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <SdFat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <functional>
#include "../config.h"
#include "../model/SystemState.h"

// =============================================================================
// NetworkManagerService - Tab5 ESP32-C6 SDIO Wi-Fi 6, Captive Portal & Export
// Note: Named NetworkManagerService (with m5track::NetworkManager alias) because
// Arduino-ESP32 v3.x (libraries/Network/src/NetworkManager.h) already defines a
// global ::NetworkManager base class included transitively by WiFi.h/WebServer.h.
// Strictly compliant with Official Tab5 Hardware & Protocol Specs (#3 §1.5, SPEC §4):
//   1. ESP32-C6 SDIO Wi-Fi 6 Init: verifies 0x44 P0 WLAN_PWR_EN + >=50ms settle
//      delay + compile-time WiFi.setPins(12,13,11,10,9,8,15) compatibility check.
//   2. 5-Slot NVS Multi-SSID Roaming: async scan + best-RSSI priority connect +
//      fallback candidate queue + UI slot delete/reconnect/scan commands.
//   3. M5Track-Setup Captive Portal: DNS 53 catch-all + Pure-Dark mobile Web UI
//      (Wi-Fi scan/save, AssistNow Token & OneDrive ClientID config) + Dual QR.
//   4. M5Track-Share Wireless Export: high-throughput SoftAP + mobile Web File
//      Manager (/tracks/{single,daily,sub}, W-/C- GPX, trips.csv, power.log,
//      16 KiB mutex-sliced streaming download, delete & quota telemetry) + Dual QR.
// =============================================================================
class NetworkManagerService {
public:
    using SdLockCallback   = std::function<bool(uint32_t timeoutMs)>;
    using SdUnlockCallback = std::function<void()>;
    using NtpSyncCallback  = std::function<void(uint32_t epochUtc)>;

    static constexpr size_t   STREAM_SLICE_BYTES      = 16384UL;  // 16 KiB per short SD lock slice
    static constexpr uint32_t STA_CONNECT_TIMEOUT_MS  = 10000UL;  // 10s per candidate AP attempt
    static constexpr uint32_t PORTAL_AUTOCLOSE_MS     = 3000UL;   // 3s auto-close after Setup OK

    static NetworkManagerService& instance();

    NetworkManagerService();
    ~NetworkManagerService();

    NetworkManagerService(const NetworkManagerService&)            = delete;
    NetworkManagerService& operator=(const NetworkManagerService&) = delete;

    // Initialize ESP32-C6 SDIO Wi-Fi 6 co-processor, load NVS credentials & start best-RSSI roam
    bool begin(SdFs* sd = nullptr, SemaphoreHandle_t sdMutex = nullptr, bool isRecursiveMutex = false);

    // Attach TF card SdFs and lock for conflict-free M5Track-Share file browsing/downloading
    void attachSdStorage(SdFs* sd, SemaphoreHandle_t sdMutex = nullptr, bool isRecursiveMutex = false);
    void setSdLockCallbacks(SdLockCallback lockCb, SdUnlockCallback unlockCb);
    void setNtpSyncCallback(NtpSyncCallback ntpCb) { _ntpSyncCb = ntpCb; }

    // Non-blocking periodic network & HTTP/DNS service tick (call from netTask on Core 0)
    void update();

    // Explicit Mode Transitions
    bool startSetupCaptivePortal(const char* optionalWpaPassword = nullptr);
    bool startTrackShareHotspot(const char* optionalWpaPassword = nullptr);
    void stopSoftApAndReturnToSta();
    bool triggerAsyncScan();
    bool saveWifiCredential(const char* ssid, const char* password);
    bool deleteWifiCredential(uint8_t index);
    bool deleteWifiCredentialBySsid(const char* ssid);

    bool isStaConnected() const { return WiFi.status() == WL_CONNECTED; }
    bool isSoftApActive() const { return _softApRunning; }

    // Standard ZXing Wi-Fi QR payload formatter ("WIFI:T:WPA;S:...;P:...;;" or "WIFI:T:nopass;S:...;;")
    static void formatWifiQrCode(const char* ssid, const char* password, char* outBuf, size_t outSize);

private:
    struct RoamCandidate {
        uint8_t savedIdx = 0;
        int8_t  rssi     = -127;
    };

    bool ensureEsp32C6PowerAndSdioReady();
    bool lockSd(uint32_t timeoutMs = 1500) const;
    void unlockSd() const;

    void loadSavedNetworksAndTokensFromNvs();
    void persistSavedNetworksToNvs();
    void syncNetworkStateSnapshot(const char* statusOverride = nullptr);

    void pollAsyncScanResults();
    void beginRoamFromScan();
    bool tryNextRoamCandidate();
    void handleStaConnectionLifecycle();
    void checkNtpSyncProgress();

    // SoftAP + DNSServer + WebServer Setup & Route Handlers
    bool startSoftApCommon(const char* ssid, const char* password, bool isShareMode);
    void setupWebRoutes();
    bool redirectIfCaptiveProbe();

    void handleWebRoot();
    void handleApiStatus();
    void handleApiScan();
    void handleApiSaved();
    void handleApiConnect();
    void handleApiDelete();
    void handleApiCloudConfigGet();
    void handleApiCloudConfigPost();
    void handleApiOneDriveStatus();
    void handleApiOneDriveAction();
    void handleApiTracksList();
    void handleApiTrackDownload();
    void handleApiTrackDelete();
    void handleApiSdFormat();
    void handleApiTunnelInfo();
    void handleApiTunnelUploadDone();
    void handleApiTunnelUploadChunk();

    bool resolveSafeTrackPath(const String& reqFile, String& outFullPath, String& outRelPath) const;

    SdFs*             _sd;
    SemaphoreHandle_t _sdMutex;
    bool              _sdMutexRecursive;
    SdLockCallback    _sdLockCb;
    SdUnlockCallback  _sdUnlockCb;
    NtpSyncCallback   _ntpSyncCb;

    DNSServer         _dnsServer;
    WebServer         _webServer;

    bool              _hwInitialized;
    bool              _softApRunning;
    bool              _isShareHotspot;
    bool              _scanInProgress;
    bool              _roamAfterScan;
    bool              _portalTestConnecting;
    bool              _ntpRequested;
    bool              _ntpSynced;

    uint8_t             _savedCount;
    SavedWifiCredential _saved[NetConfig::MAX_SAVED_NETWORKS];

    RoamCandidate     _roamQueue[NetConfig::MAX_SAVED_NETWORKS];
    uint8_t           _roamQueueCount;
    uint8_t           _roamCursor;

    uint32_t          _staConnectStartMs;
    uint32_t          _lastReconnectScanMs;
    uint32_t          _lastTelemetrySyncMs;
    uint32_t          _portalProvisionedMs;

    fs::File          _tunnelUploadFile;
    bool              _tunnelUploadOk;
    size_t            _tunnelUploadBytes;
};

namespace m5track {
using NetworkManager = ::NetworkManagerService;
} // namespace m5track
