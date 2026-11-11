#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <functional>
#include "../config.h"
#include "../model/SystemState.h"

// =============================================================================
// AgpsService - u-blox AssistNow Online (UBX-MGA-*) Ephemeris Service
// Strictly compliant with Official u-blox AssistNow REST Specification (#3 §3.1):
//   - Primary/Backup HTTPS hosts: online-live1 / online-live2.services.u-blox.com
//   - Mandatory semicolon ';' parameter delimiter (NEVER '&'!)
//   - PSRAM binary UBX-MGA stream cache + 256~512B paced chunked injection
// =============================================================================
class AgpsService {
public:
    using RawUbxInjectCallback = std::function<bool(const uint8_t* data, size_t len)>;

    static constexpr size_t   MAX_MGA_CACHE_BYTES        = 131072UL;    // 128 KiB PSRAM MGA buffer
    static constexpr size_t   INJECT_CHUNK_BYTES         = CloudConfig::ASSISTNOW_CHUNK_SIZE; // 512 B
    static constexpr uint32_t MIN_AUTO_SYNC_INTERVAL_MS  = 7200000UL;   // 2h ephemeris validity window
    static constexpr uint32_t RETRY_BACKOFF_MS           = 60000UL;     // 60s backoff after network failure
    static constexpr uint16_t HTTP_TIMEOUT_MS            = 12000U;      // 12s HTTPS timeout

    static AgpsService& instance();

    AgpsService();
    ~AgpsService();

    AgpsService(const AgpsService&)            = delete;
    AgpsService& operator=(const AgpsService&) = delete;

    // Initialize PSRAM cache mutex & optional GnssDriver raw UBX injection callback
    bool begin(RawUbxInjectCallback injectCallback = nullptr);

    // Register or update thread-safe callback to forward 256~512B UBX-MGA chunks to GnssDriver
    void setInjectCallback(RawUbxInjectCallback injectCallback);

    // Non-blocking periodic service tick (call from netTask on Core 0)
    void update();

    // Immediately download AssistNow Online ephemeris and inject into NEO-M9N
    bool syncNow(bool force = true);

    // Re-inject already cached PSRAM UBX-MGA ephemeris (e.g., after UBX-CFG-RST watchdog)
    bool reinjectCached();

    // Pull-mode consumer API: allows GnssDriver to pull 256~512B chunks directly if preferred
    bool   hasPendingChunks() const;
    size_t readNextPendingChunk(uint8_t* dstBuf, size_t maxLen = INJECT_CHUNK_BYTES);

    // Cached binary stream inspection
    size_t   cachedBytes() const;
    uint32_t lastSyncUtc() const { return _lastSyncUtc; }

    // Pure URL builder strictly using semicolon ';' delimiters per u-blox AssistNow spec
    static String buildAssistNowUrl(const char* host,
                                    const char* token,
                                    bool        includePositionFilter,
                                    double      latDeg     = 0.0,
                                    double      lonDeg     = 0.0,
                                    float       altMeters  = 0.0f,
                                    uint32_t    paccMeters = 10000UL);

private:
    bool ensureCacheAllocatedLocked();
    bool resolveToken(char* outToken, size_t outSize) const;
    bool resolveCoarsePosition(double& outLat, double& outLon, float& outAltM, uint32_t& outPaccM) const;
    bool downloadFromHost(const char* host, const char* token,
                          bool usePosFilter, double lat, double lon, float altM, uint32_t paccM,
                          size_t& outBytesRead);
    bool injectBufferInChunks(const uint8_t* buf, size_t totalLen, size_t& outInjected);
    void updateStateStatus(AgpsStatus status, uint32_t injectedBytes, uint32_t syncUtc);

    mutable SemaphoreHandle_t _mutex;
    RawUbxInjectCallback      _injectCb;
    uint8_t*                  _psramCache;
    size_t                    _cachedLen;
    size_t                    _pendingReadOffset;
    bool                      _initialized;
    uint32_t                  _lastSyncMillis;
    uint32_t                  _lastAttemptMillis;
    uint32_t                  _lastSyncUtc;
};
