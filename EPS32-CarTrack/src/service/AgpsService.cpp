#include "AgpsService.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

AgpsService& AgpsService::instance() {
    static AgpsService s_instance;
    return s_instance;
}

AgpsService::AgpsService()
    : _mutex(xSemaphoreCreateMutex()),
      _injectCb(nullptr),
      _psramCache(nullptr),
      _cachedLen(0),
      _pendingReadOffset(0),
      _initialized(false),
      _lastSyncMillis(0),
      _lastAttemptMillis(0),
      _lastSyncUtc(0) {}

AgpsService::~AgpsService() {
    if (_psramCache != nullptr) {
        free(_psramCache);
        _psramCache = nullptr;
    }
    if (_mutex != nullptr) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
}

bool AgpsService::begin(RawUbxInjectCallback injectCallback) {
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (injectCallback) {
            _injectCb = injectCallback;
        }
        ensureCacheAllocatedLocked();
        _initialized = true;
        xSemaphoreGive(_mutex);
    }

    // Restore persisted AssistNow token from NVS if SystemState does not have it yet
    char nvsToken[64] = {};
    Preferences prefs;
    if (prefs.begin(NvsConfig::NAMESPACE, true)) {
        prefs.getString(NvsConfig::KEY_AGPS_TOKEN, nvsToken, sizeof(nvsToken));
        prefs.end();
    }
    if (nvsToken[0] != '\0') {
        StateGuard lock(SystemState::instance());
        auto& st = SystemState::instance().data();
        if (st.settings.assistNowToken[0] == '\0') {
            strncpy(st.settings.assistNowToken, nvsToken, sizeof(st.settings.assistNowToken) - 1);
            st.settings.assistNowToken[sizeof(st.settings.assistNowToken) - 1] = '\0';
        }
    }
    return true;
}

void AgpsService::setInjectCallback(RawUbxInjectCallback injectCallback) {
    if (_mutex == nullptr) {
        return;
    }
    bool flushPending = false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        _injectCb = injectCallback;
        flushPending = (_injectCb != nullptr && _pendingReadOffset < _cachedLen);
        xSemaphoreGive(_mutex);
    }
    if (flushPending) {
        reinjectCached();
    }
}

void AgpsService::update() {
    if (!_initialized) {
        begin();
    }

    bool manualTrigger = false;
    bool staConnected  = (WiFi.status() == WL_CONNECTED);
    {
        StateGuard lock(SystemState::instance());
        auto& st = SystemState::instance().data();
        if (st.network.cmdRunAgpsNow) {
            manualTrigger = true;
            st.network.cmdRunAgpsNow = false;
        }
    }

    if (manualTrigger) {
        syncNow(true);
        return;
    }

    if (!staConnected) {
        return;
    }

    // Check if an AssistNow token is configured; stay quietly IDLE if unconfigured
    char token[64] = {};
    if (!resolveToken(token, sizeof(token))) {
        return;
    }

    const uint32_t nowMs = millis();
    if (_lastSyncMillis != 0 && (nowMs - _lastSyncMillis) < MIN_AUTO_SYNC_INTERVAL_MS) {
        return;
    }
    if (_lastAttemptMillis != 0 && (nowMs - _lastAttemptMillis) < RETRY_BACKOFF_MS) {
        return;
    }

    syncNow(false);
}

String AgpsService::buildAssistNowUrl(const char* host,
                                      const char* token,
                                      bool        includePositionFilter,
                                      double      latDeg,
                                      double      lonDeg,
                                      float       altMeters,
                                      uint32_t    paccMeters) {
    // STRICT #3 §3.1 Rule: Query parameters MUST be delimited by semicolon ';', NEVER '&'!
    String url;
    url.reserve(256);
    url += "https://";
    url += (host != nullptr && host[0] != '\0') ? host : CloudConfig::ASSISTNOW_HOST_PRIMARY;
    url += CloudConfig::ASSISTNOW_PATH;
    url += "?token=";
    if (token != nullptr) {
        url += token;
    }
    url += ";gnss=gps,bds,glo,gal;datatype=eph,alm,aux;format=mga;";

    if (includePositionFilter &&
        isfinite(latDeg) && isfinite(lonDeg) &&
        fabs(latDeg) <= 90.0 && fabs(lonDeg) <= 180.0 &&
        (fabs(latDeg) > 1e-5 || fabs(lonDeg) > 1e-5)) {
        char posBuf[128] = {};
        const float clampedAlt = isfinite(altMeters) ? altMeters : 0.0f;
        const uint32_t safePacc = (paccMeters < 100UL) ? 1000UL : paccMeters;
        snprintf(posBuf, sizeof(posBuf),
                 "lat=%.6f;lon=%.6f;alt=%.1f;pacc=%lu;filteronpos;",
                 latDeg, lonDeg, static_cast<double>(clampedAlt),
                 static_cast<unsigned long>(safePacc));
        url += posBuf;
    }
    return url;
}

bool AgpsService::syncNow(bool force) {
    const uint32_t nowMs = millis();
    if (!force && _lastSyncMillis != 0 && (nowMs - _lastSyncMillis) < MIN_AUTO_SYNC_INTERVAL_MS) {
        return true;
    }
    _lastAttemptMillis = nowMs;

    if (WiFi.status() != WL_CONNECTED) {
        updateStateStatus(AGPS_STATE_ERROR, 0, _lastSyncUtc);
        return false;
    }

    char token[64] = {};
    if (!resolveToken(token, sizeof(token))) {
        // Token not configured; report IDLE on auto check, ERROR only if explicitly forced
        updateStateStatus(force ? AGPS_STATE_ERROR : AGPS_STATE_IDLE, 0, _lastSyncUtc);
        return false;
    }

    updateStateStatus(AGPS_STATE_DOWNLOADING, 0, _lastSyncUtc);

    double   latDeg  = 0.0;
    double   lonDeg  = 0.0;
    float    altM    = 0.0f;
    uint32_t paccM   = 10000UL;
    const bool hasPos = resolveCoarsePosition(latDeg, lonDeg, altM, paccM);

    size_t downloadedBytes = 0;
    bool ok = downloadFromHost(CloudConfig::ASSISTNOW_HOST_PRIMARY, token,
                               hasPos, latDeg, lonDeg, altM, paccM, downloadedBytes);
    if (!ok) {
        Serial.println("[AGPS] Primary AssistNow host failed, trying backup online-live2...");
        ok = downloadFromHost(CloudConfig::ASSISTNOW_HOST_BACKUP, token,
                              hasPos, latDeg, lonDeg, altM, paccM, downloadedBytes);
    }

    if (!ok || downloadedBytes < 8) {
        updateStateStatus(AGPS_STATE_ERROR, 0, _lastSyncUtc);
        return false;
    }

    size_t injectedBytes = 0;
    uint8_t* localCopy = nullptr;
    size_t   localLen  = 0;

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        localLen = _cachedLen;
        if (_injectCb != nullptr && _psramCache != nullptr && localLen > 0) {
            localCopy = static_cast<uint8_t*>(
                heap_caps_malloc(localLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (localCopy == nullptr) {
                localCopy = static_cast<uint8_t*>(malloc(localLen));
            }
            if (localCopy != nullptr) {
                memcpy(localCopy, _psramCache, localLen);
            }
        }
        xSemaphoreGive(_mutex);
    }

    if (localCopy != nullptr) {
        injectBufferInChunks(localCopy, localLen, injectedBytes);
        free(localCopy);
        if (injectedBytes > 0 && xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
            _pendingReadOffset = _cachedLen; // Consumed via push callback
            xSemaphoreGive(_mutex);
        }
    } else {
        // Buffered in PSRAM awaiting pull or callback registration
        injectedBytes = downloadedBytes;
    }

    uint32_t syncUtc = 0;
    {
        StateGuard lock(SystemState::instance());
        syncUtc = SystemState::instance().data().time.epochUtc;
    }
    if (syncUtc < 1700000000UL) {
        const time_t sysNow = time(nullptr);
        if (sysNow > 1700000000) {
            syncUtc = static_cast<uint32_t>(sysNow);
        }
    }

    _lastSyncMillis = millis();
    _lastSyncUtc    = syncUtc;
    updateStateStatus(AGPS_STATE_INJECTED, static_cast<uint32_t>(injectedBytes), _lastSyncUtc);

    Serial.printf("[AGPS] AssistNow Online synced & cached (%u bytes, injected=%u bytes)\n",
                  static_cast<unsigned>(downloadedBytes),
                  static_cast<unsigned>(injectedBytes));
    return true;
}

bool AgpsService::reinjectCached() {
    uint8_t* localCopy = nullptr;
    size_t   localLen  = 0;

    if (_mutex == nullptr || xSemaphoreTake(_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }
    const bool hasCb = (_injectCb != nullptr);
    if (hasCb && _psramCache != nullptr && _cachedLen >= 8) {
        localLen = _cachedLen;
        localCopy = static_cast<uint8_t*>(
            heap_caps_malloc(localLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (localCopy == nullptr) {
            localCopy = static_cast<uint8_t*>(malloc(localLen));
        }
        if (localCopy != nullptr) {
            memcpy(localCopy, _psramCache, localLen);
        }
    }
    xSemaphoreGive(_mutex);

    if (!hasCb || localCopy == nullptr) {
        if (localCopy != nullptr) {
            free(localCopy);
        }
        return false;
    }

    size_t injected = 0;
    const bool ok = injectBufferInChunks(localCopy, localLen, injected);
    free(localCopy);

    if (ok && xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        _pendingReadOffset = _cachedLen;
        xSemaphoreGive(_mutex);
    }
    if (ok) {
        updateStateStatus(AGPS_STATE_INJECTED, static_cast<uint32_t>(injected), _lastSyncUtc);
    }
    return ok;
}

bool AgpsService::hasPendingChunks() const {
    if (_mutex == nullptr || xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return false;
    }
    const bool pending = (_psramCache != nullptr && _pendingReadOffset < _cachedLen);
    xSemaphoreGive(_mutex);
    return pending;
}

size_t AgpsService::readNextPendingChunk(uint8_t* dstBuf, size_t maxLen) {
    if (dstBuf == nullptr || maxLen == 0 || _mutex == nullptr) {
        return 0;
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return 0;
    }
    if (_psramCache == nullptr || _pendingReadOffset >= _cachedLen) {
        xSemaphoreGive(_mutex);
        return 0;
    }

    const size_t remaining = _cachedLen - _pendingReadOffset;
    const size_t cap       = (maxLen < INJECT_CHUNK_BYTES) ? maxLen : INJECT_CHUNK_BYTES;
    size_t toCopy          = (remaining < cap) ? remaining : cap;

    // Align to complete UBX frames (0xB5 0x62 + Class + ID + LenL + LenH + Payload + CKA + CKB) if possible
    size_t cursor = _pendingReadOffset;
    size_t alignedBytes = 0;
    while (cursor + 8 <= _cachedLen &&
           _psramCache[cursor] == 0xB5 &&
           _psramCache[cursor + 1] == 0x62) {
        const uint16_t payloadLen = static_cast<uint16_t>(_psramCache[cursor + 4]) |
                                    (static_cast<uint16_t>(_psramCache[cursor + 5]) << 8);
        const size_t frameLen = static_cast<size_t>(payloadLen) + 8U;
        if (cursor + frameLen > _cachedLen) {
            break;
        }
        if (alignedBytes + frameLen > cap) {
            break;
        }
        alignedBytes += frameLen;
        cursor += frameLen;
    }
    if (alignedBytes > 0) {
        toCopy = alignedBytes;
    }

    memcpy(dstBuf, _psramCache + _pendingReadOffset, toCopy);
    _pendingReadOffset += toCopy;
    xSemaphoreGive(_mutex);
    return toCopy;
}

size_t AgpsService::cachedBytes() const {
    if (_mutex == nullptr || xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return 0;
    }
    const size_t n = _cachedLen;
    xSemaphoreGive(_mutex);
    return n;
}

bool AgpsService::ensureCacheAllocatedLocked() {
    if (_psramCache != nullptr) {
        return true;
    }
    void* mem = heap_caps_malloc(MAX_MGA_CACHE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mem == nullptr) {
        mem = malloc(32768UL);
    }
    _psramCache = static_cast<uint8_t*>(mem);
    return (_psramCache != nullptr);
}

bool AgpsService::resolveToken(char* outToken, size_t outSize) const {
    if (outToken == nullptr || outSize == 0) {
        return false;
    }
    outToken[0] = '\0';

    {
        StateGuard lock(SystemState::instance());
        const auto& st = SystemState::instance().data();
        if (st.settings.assistNowToken[0] != '\0') {
            strncpy(outToken, st.settings.assistNowToken, outSize - 1);
            outToken[outSize - 1] = '\0';
        }
    }

    if (outToken[0] == '\0') {
        Preferences prefs;
        if (prefs.begin(NvsConfig::NAMESPACE, true)) {
            prefs.getString(NvsConfig::KEY_AGPS_TOKEN, outToken, outSize);
            prefs.end();
        }
    }

    // Trim whitespace
    size_t len = strlen(outToken);
    while (len > 0 && (outToken[len - 1] == ' ' || outToken[len - 1] == '\r' || outToken[len - 1] == '\n')) {
        outToken[--len] = '\0';
    }
    return (len >= 8);
}

bool AgpsService::resolveCoarsePosition(double& outLat, double& outLon,
                                        float& outAltM, uint32_t& outPaccM) const {
    uint32_t nowUtc = 0;
    {
        StateGuard lock(SystemState::instance());
        const auto& st = SystemState::instance().data();
        nowUtc = st.time.epochUtc;
        if (st.gnss.isValid &&
            (fabs(st.gnss.latitude) > 1e-5 || fabs(st.gnss.longitude) > 1e-5)) {
            outLat   = st.gnss.latitude;
            outLon   = st.gnss.longitude;
            outAltM  = st.gnss.altitudeMeters;
            outPaccM = 5000UL; // 5 km coarse position filter
            return true;
        }
    }

    if (nowUtc < 1700000000UL) {
        const time_t sysNow = time(nullptr);
        if (sysNow > 1700000000) {
            nowUtc = static_cast<uint32_t>(sysNow);
        }
    }

    Preferences prefs;
    if (!prefs.begin(NvsConfig::NAMESPACE, true)) {
        return false;
    }
    const int32_t  latE7      = prefs.getInt(NvsConfig::KEY_LAST_LAT_E7, 0);
    const int32_t  lonE7      = prefs.getInt(NvsConfig::KEY_LAST_LON_E7, 0);
    const int32_t  altCm      = prefs.getInt(NvsConfig::KEY_LAST_ALT_CM, 0);
    const uint32_t lastPosUtc = prefs.getUInt(NvsConfig::KEY_LAST_POS_UTC, 0);
    prefs.end();

    if (latE7 == 0 && lonE7 == 0) {
        return false;
    }

    const double lat = static_cast<double>(latE7) * 1e-7;
    const double lon = static_cast<double>(lonE7) * 1e-7;
    if (fabs(lat) > 90.0 || fabs(lon) > 180.0) {
        return false;
    }

    if (nowUtc > 1700000000UL && lastPosUtc > 1700000000UL && nowUtc >= lastPosUtc) {
        const uint32_t ageSec = nowUtc - lastPosUtc;
        if (ageSec <= GnssConfig::MGA_POS_FRESH_MAX_AGE_SEC) {
            outPaccM = 10000UL; // 10 km
        } else if (ageSec <= 86400UL) {
            outPaccM = 50000UL; // 50 km
        } else {
            // Older than 24h: fetch global ephemeris without filteronpos
            return false;
        }
    } else {
        outPaccM = 30000UL; // 30 km conservative filter
    }

    outLat  = lat;
    outLon  = lon;
    outAltM = static_cast<float>(altCm) * 0.01f;
    return true;
}

bool AgpsService::downloadFromHost(const char* host, const char* token,
                                   bool usePosFilter, double lat, double lon,
                                   float altM, uint32_t paccM,
                                   size_t& outBytesRead) {
    outBytesRead = 0;
    const String url = buildAssistNowUrl(host, token, usePosFilter, lat, lon, altM, paccM);

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTP_TIMEOUT_MS / 1000U);

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (!http.begin(client, url)) {
        return false;
    }

    const int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[AGPS] HTTP %d from %s\n", httpCode, host);
        http.end();
        return false;
    }

    NetworkClient* stream = http.getStreamPtr();
    if (stream == nullptr) {
        http.end();
        return false;
    }

    // Download into a temporary PSRAM staging buffer first so we don't hold _mutex during TLS I/O
    uint8_t* stageBuf = static_cast<uint8_t*>(
        heap_caps_malloc(MAX_MGA_CACHE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    size_t stageCap = MAX_MGA_CACHE_BYTES;
    if (stageBuf == nullptr) {
        stageCap = 32768UL;
        stageBuf = static_cast<uint8_t*>(malloc(stageCap));
    }
    if (stageBuf == nullptr) {
        http.end();
        return false;
    }

    int remaining = http.getSize(); // -1 if chunked/unknown
    size_t totalRead = 0;
    uint32_t lastDataMs = millis();

    while ((http.connected() || stream->available() > 0) &&
           (remaining > 0 || remaining == -1) &&
           totalRead < stageCap) {
        const int avail = stream->available();
        if (avail > 0) {
            size_t toRead = static_cast<size_t>(avail);
            if (toRead > INJECT_CHUNK_BYTES) {
                toRead = INJECT_CHUNK_BYTES;
            }
            if (totalRead + toRead > stageCap) {
                toRead = stageCap - totalRead;
            }
            const int r = stream->readBytes(stageBuf + totalRead, toRead);
            if (r > 0) {
                totalRead += static_cast<size_t>(r);
                if (remaining > 0) {
                    remaining -= r;
                }
                lastDataMs = millis();
            }
        } else {
            if ((millis() - lastDataMs) > HTTP_TIMEOUT_MS) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    http.end();

    // Verify authentic binary UBX-MGA stream (starts with 0xB5 0x62 sync chars)
    if (totalRead < 8 || stageBuf[0] != 0xB5 || stageBuf[1] != 0x62) {
        Serial.printf("[AGPS] Invalid UBX-MGA header from %s (len=%u)\n",
                      host, static_cast<unsigned>(totalRead));
        free(stageBuf);
        return false;
    }

    bool stored = false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (ensureCacheAllocatedLocked()) {
            memcpy(_psramCache, stageBuf, totalRead);
            _cachedLen         = totalRead;
            _pendingReadOffset = 0;
            stored             = true;
        }
        xSemaphoreGive(_mutex);
    }

    free(stageBuf);
    if (stored) {
        outBytesRead = totalRead;
    }
    return stored;
}

bool AgpsService::injectBufferInChunks(const uint8_t* buf, size_t totalLen, size_t& outInjected) {
    outInjected = 0;
    if (buf == nullptr || totalLen == 0) {
        return false;
    }

    RawUbxInjectCallback cb;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        cb = _injectCb;
        xSemaphoreGive(_mutex);
    }
    if (!cb) {
        return false;
    }

    size_t offset = 0;
    while (offset < totalLen) {
        size_t chunkLen = 0;
        size_t cursor   = offset;

        // Batch complete UBX frames up to 512 bytes (CloudConfig::ASSISTNOW_CHUNK_SIZE)
        while (cursor + 8 <= totalLen &&
               buf[cursor] == 0xB5 &&
               buf[cursor + 1] == 0x62) {
            const uint16_t payloadLen = static_cast<uint16_t>(buf[cursor + 4]) |
                                        (static_cast<uint16_t>(buf[cursor + 5]) << 8);
            const size_t frameLen = static_cast<size_t>(payloadLen) + 8U;
            if (cursor + frameLen > totalLen) {
                break;
            }
            if (chunkLen + frameLen > INJECT_CHUNK_BYTES) {
                break;
            }
            chunkLen += frameLen;
            cursor += frameLen;
        }

        if (chunkLen == 0) {
            const size_t rem = totalLen - offset;
            chunkLen = (rem > INJECT_CHUNK_BYTES) ? INJECT_CHUNK_BYTES : rem;
        }

        if (!cb(buf + offset, chunkLen)) {
            return (outInjected > 0);
        }
        outInjected += chunkLen;
        offset += chunkLen;

        // Pace UART 115200 bps writes (~44ms per 512B on wire) so M9N RX FIFO never overflows
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

void AgpsService::updateStateStatus(AgpsStatus status, uint32_t injectedBytes, uint32_t syncUtc) {
    StateGuard lock(SystemState::instance());
    auto& st = SystemState::instance().data();
    st.network.agpsStatus = status;
    if (injectedBytes > 0) {
        st.network.agpsInjectedBytes = injectedBytes;
    }
    if (syncUtc > 0) {
        st.network.agpsLastSyncUtc = syncUtc;
    }
}
