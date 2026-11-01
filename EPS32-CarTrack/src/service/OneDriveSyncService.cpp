#include "service/OneDriveSyncService.h"
#include "service/TrackLogger.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

OneDriveSyncService::OneDriveSyncService() {
    _mutex = xSemaphoreCreateRecursiveMutex();
    strncpy(_clientId, CloudSyncConfig::DEFAULT_CLIENT_ID, sizeof(_clientId) - 1);
}

bool OneDriveSyncService::init() {
    Preferences prefs;
    if (prefs.begin("m5track", false)) {
        _isBound         = prefs.getBool("od_bound", false);
        _autoSyncEnabled = prefs.getBool("od_auto", true);
        _lastSyncEpoch   = prefs.getUInt("od_last_sync", 0);

        String rt = prefs.getString("od_refresh", "");
        if (rt.length() > 0) {
            strncpy(_refreshToken, rt.c_str(), sizeof(_refreshToken) - 1);
        }

        String em = prefs.getString("od_email", "");
        if (em.length() > 0) {
            strncpy(_accountEmail, em.c_str(), sizeof(_accountEmail) - 1);
        }

        String cid = prefs.getString("od_client_id", "");
        if (cid.length() > 0) {
            strncpy(_clientId, cid.c_str(), sizeof(_clientId) - 1);
        }

        prefs.end();
    }

    SystemState& state = SystemState::instance();
    state.lock();
    auto& od = state.getContextUnsafe().onedrive;
    od.isBound          = _isBound;
    od.autoSyncEnabled  = _autoSyncEnabled;
    od.lastSyncEpoch    = _lastSyncEpoch;
    strncpy(od.accountEmail, _accountEmail, sizeof(od.accountEmail) - 1);
    state.unlock();

    Serial.printf("[ONEDRIVE] Init: bound=%d, email=%s, autoSync=%d\n", _isBound, _accountEmail, _autoSyncEnabled);
    return true;
}

void OneDriveSyncService::startTask() {
    if (_taskHandle == nullptr) {
        xTaskCreatePinnedToCore(
            taskTrampoline,
            "oneDriveSync",
            10240,
            this,
            1, // Priority 1 (Background network I/O)
            &_taskHandle,
            0  // Pinned to Core 0
        );
    }
}

void OneDriveSyncService::taskTrampoline(void* arg) {
    OneDriveSyncService* self = static_cast<OneDriveSyncService*>(arg);
    self->taskLoop();
}

void OneDriveSyncService::setAutoSync(bool enable) {
    _autoSyncEnabled = enable;
    Preferences prefs;
    if (prefs.begin("m5track", false)) {
        prefs.putBool("od_auto", _autoSyncEnabled);
        prefs.end();
    }
    SystemState& state = SystemState::instance();
    state.lock();
    state.getContextUnsafe().onedrive.autoSyncEnabled = _autoSyncEnabled;
    state.unlock();
}

void OneDriveSyncService::unbindAccount() {
    if (_mutex) xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
    _isBound = false;
    _refreshToken[0] = '\0';
    _accessToken[0]  = '\0';
    _accountEmail[0] = '\0';
    _authInProgress  = false;
    _userCode[0]     = '\0';
    _deviceCode[0]   = '\0';

    Preferences prefs;
    if (prefs.begin("m5track", false)) {
        prefs.remove("od_bound");
        prefs.remove("od_refresh");
        prefs.remove("od_email");
        prefs.end();
    }

    SystemState& state = SystemState::instance();
    state.lock();
    auto& od = state.getContextUnsafe().onedrive;
    od.isBound        = false;
    od.authInProgress = false;
    od.accountEmail[0]= '\0';
    od.userCode[0]    = '\0';
    od.verifyUrl[0]   = '\0';
    state.unlock();

    if (_mutex) xSemaphoreGiveRecursive(_mutex);
    Serial.println("[ONEDRIVE] Account unbound successfully.");
}

void OneDriveSyncService::cancelDeviceLogin() {
    if (_mutex) xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
    _authInProgress = false;
    _deviceCode[0]  = '\0';
    _userCode[0]    = '\0';

    SystemState& state = SystemState::instance();
    state.lock();
    auto& od = state.getContextUnsafe().onedrive;
    od.authInProgress = false;
    od.userCode[0]    = '\0';
    od.verifyUrl[0]   = '\0';
    state.unlock();
    if (_mutex) xSemaphoreGiveRecursive(_mutex);
}

bool OneDriveSyncService::startDeviceLogin() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[ONEDRIVE] Login failed: Wi-Fi not connected");
        return false;
    }

    if (_mutex) xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10000);

    HTTPClient http;
    http.begin(client, "https://login.microsoftonline.com/common/oauth2/v2.0/devicecode");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    char postData[256];
    snprintf(postData, sizeof(postData), "client_id=%s&scope=offline_access%%20Files.ReadWrite%%20User.Read", _clientId);

    int httpCode = http.POST(postData);
    if (httpCode != 200) {
        Serial.printf("[ONEDRIVE] DeviceCode request failed with code %d\n", httpCode);
        http.end();
        if (_mutex) xSemaphoreGiveRecursive(_mutex);
        return false;
    }

    String resp = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, resp);
    if (err) {
        Serial.printf("[ONEDRIVE] JSON parse failed: %s\n", err.c_str());
        if (_mutex) xSemaphoreGiveRecursive(_mutex);
        return false;
    }

    const char* dCode = doc["device_code"] | "";
    const char* uCode = doc["user_code"] | "";
    const char* vUri  = doc["verification_uri"] | "https://microsoft.com/devicelogin";
    uint32_t interval = doc["interval"] | 5;
    uint32_t expSec   = doc["expires_in"] | 900;

    strncpy(_deviceCode, dCode, sizeof(_deviceCode) - 1);
    strncpy(_userCode, uCode, sizeof(_userCode) - 1);
    strncpy(_verifyUrl, vUri, sizeof(_verifyUrl) - 1);
    _pollIntervalMs  = (interval > 0 ? interval : 5) * 1000;
    _pollExpireEpoch = millis() + (expSec * 1000);
    _lastPollMs      = millis();
    _authInProgress  = true;

    SystemState& state = SystemState::instance();
    state.lock();
    auto& od = state.getContextUnsafe().onedrive;
    od.authInProgress = true;
    strncpy(od.userCode, _userCode, sizeof(od.userCode) - 1);
    strncpy(od.verifyUrl, _verifyUrl, sizeof(od.verifyUrl) - 1);
    od.lastSyncError[0] = '\0';
    state.unlock();

    Serial.printf("[ONEDRIVE] Device login started! Code: %s, URL: %s\n", _userCode, _verifyUrl);
    if (_mutex) xSemaphoreGiveRecursive(_mutex);
    return true;
}

bool OneDriveSyncService::pollDeviceCodeApproval() {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10000);

    HTTPClient http;
    http.begin(client, "https://login.microsoftonline.com/common/oauth2/v2.0/token");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    char postData[384];
    snprintf(postData, sizeof(postData),
             "grant_type=urn:ietf:params:oauth:grant-type:device_code&client_id=%s&device_code=%s",
             _clientId, _deviceCode);

    int httpCode = http.POST(postData);
    String resp = http.getString();
    http.end();

    if (httpCode == 200) {
        JsonDocument doc;
        deserializeJson(doc, resp);
        const char* acc = doc["access_token"] | "";
        const char* ref = doc["refresh_token"] | "";
        uint32_t expIn  = doc["expires_in"] | 3600;

        if (acc[0] != '\0' && ref[0] != '\0') {
            strncpy(_accessToken, acc, sizeof(_accessToken) - 1);
            strncpy(_refreshToken, ref, sizeof(_refreshToken) - 1);
            _accessTokenExpireMs = millis() + ((expIn > 120 ? expIn - 120 : expIn) * 1000);
            _isBound        = true;
            _authInProgress = false;

            Preferences prefs;
            if (prefs.begin("m5track", false)) {
                prefs.putBool("od_bound", true);
                prefs.putString("od_refresh", _refreshToken);
                prefs.end();
            }

            fetchUserProfile();

            SystemState& state = SystemState::instance();
            state.lock();
            auto& od = state.getContextUnsafe().onedrive;
            od.isBound        = true;
            od.authInProgress = false;
            strncpy(od.accountEmail, _accountEmail, sizeof(od.accountEmail) - 1);
            od.userCode[0]    = '\0';
            state.unlock();

            Serial.printf("[ONEDRIVE] Approval successful! Account: %s\n", _accountEmail);
            return true;
        }
    } else if (httpCode == 400) {
        if (resp.indexOf("authorization_pending") >= 0) {
            // User hasn't finished typing code on phone/PC yet, continue waiting
            return false;
        }
        if (resp.indexOf("expired_token") >= 0 || resp.indexOf("code_expired") >= 0) {
            Serial.println("[ONEDRIVE] Device code expired.");
            cancelDeviceLogin();
            return false;
        }
    }

    return false;
}

bool OneDriveSyncService::refreshAccessToken() {
    if (_accessToken[0] != '\0' && millis() < _accessTokenExpireMs) {
        return true;
    }
    if (_refreshToken[0] == '\0') {
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10000);

    HTTPClient http;
    http.begin(client, "https://login.microsoftonline.com/common/oauth2/v2.0/token");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String postData = String("grant_type=refresh_token&client_id=") + _clientId +
                      "&refresh_token=" + _refreshToken +
                      "&scope=offline_access%20Files.ReadWrite%20User.Read";

    int httpCode = http.POST(postData);
    if (httpCode != 200) {
        Serial.printf("[ONEDRIVE] Refresh token failed: code %d\n", httpCode);
        http.end();
        return false;
    }

    String resp = http.getString();
    http.end();

    JsonDocument doc;
    deserializeJson(doc, resp);
    const char* acc = doc["access_token"] | "";
    const char* ref = doc["refresh_token"] | "";
    uint32_t expIn  = doc["expires_in"] | 3600;

    if (acc[0] != '\0') {
        strncpy(_accessToken, acc, sizeof(_accessToken) - 1);
        _accessTokenExpireMs = millis() + ((expIn > 120 ? expIn - 120 : expIn) * 1000);

        if (ref[0] != '\0') {
            strncpy(_refreshToken, ref, sizeof(_refreshToken) - 1);
            Preferences prefs;
            if (prefs.begin("m5track", false)) {
                prefs.putString("od_refresh", _refreshToken);
                prefs.end();
            }
        }
        return true;
    }
    return false;
}

bool OneDriveSyncService::fetchUserProfile() {
    if (_accessToken[0] == '\0') return false;

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10000);

    HTTPClient http;
    http.begin(client, "https://graph.microsoft.com/v1.0/me");
    http.addHeader("Authorization", String("Bearer ") + _accessToken);

    int code = http.GET();
    if (code == 200) {
        String resp = http.getString();
        JsonDocument doc;
        deserializeJson(doc, resp);
        const char* mail = doc["mail"] | "";
        if (mail[0] == '\0') mail = doc["userPrincipalName"] | "";
        if (mail[0] == '\0') mail = doc["displayName"] | "";

        strncpy(_accountEmail, mail, sizeof(_accountEmail) - 1);
        Preferences prefs;
        if (prefs.begin("m5track", false)) {
            prefs.putString("od_email", _accountEmail);
            prefs.end();
        }
        http.end();
        return true;
    }
    http.end();
    return false;
}

bool OneDriveSyncService::isFileInSyncedLedger(const char* relPath) {
    TrackLogger::instance().lockSd();
    FsFile ledger;
    if (!TrackLogger::instance().openFileForRead(StorageConfig::SYNC_LEDGER_PATH, ledger)) {
        TrackLogger::instance().unlockSd();
        return false;
    }

    char line[128];
    bool found = false;
    while (ledger.available()) {
        int n = ledger.readBytesUntil('\n', line, sizeof(line) - 1);
        if (n > 0) {
            line[n] = '\0';
            if (line[n - 1] == '\r') line[n - 1] = '\0';
            if (strcmp(line, relPath) == 0) {
                found = true;
                break;
            }
        }
    }
    ledger.close();
    TrackLogger::instance().unlockSd();
    return found;
}

void OneDriveSyncService::markFileInSyncedLedger(const char* relPath) {
    TrackLogger::instance().lockSd();
    FsFile ledger;
    if (ledger.open(StorageConfig::SYNC_LEDGER_PATH, O_RDWR | O_CREAT | O_APPEND)) {
        ledger.write((const uint8_t*)relPath, strlen(relPath));
        ledger.write((const uint8_t*)"\n", 1);
        ledger.sync();
        ledger.close();
    }
    TrackLogger::instance().unlockSd();
}

uint32_t OneDriveSyncService::scanPendingFilesCount() {
    TrackFileEntry entries[64];
    size_t total = TrackLogger::instance().listTrackFiles(entries, 64);

    SystemState& state = SystemState::instance();
    state.lock();
    const auto& st = state.getContextUnsafe().storage;
    char curW[64], curC[64], curD[64], curS[64];
    strncpy(curW, st.currentWgsFile, sizeof(curW) - 1);
    strncpy(curC, st.currentGcjFile, sizeof(curC) - 1);
    strncpy(curD, st.currentDailyFile, sizeof(curD) - 1);
    strncpy(curS, st.currentSubFile, sizeof(curS) - 1);
    state.unlock();

    uint32_t pending = 0;
    for (size_t i = 0; i < total; i++) {
        const char* name = entries[i].name;
        // Skip files actively being recorded
        if (strcmp(name, curW) == 0 || strcmp(name, curC) == 0 ||
            strcmp(name, curD) == 0 || strcmp(name, curS) == 0) {
            continue;
        }
        if (!isFileInSyncedLedger(name)) {
            pending++;
        }
    }
    return pending;
}

bool OneDriveSyncService::uploadSmallFile(const char* cloudPath, const char* localPath, const char* contentType) {
    TrackLogger::instance().lockSd();
    FsFile file;
    if (!TrackLogger::instance().openFileForRead(localPath, file)) {
        TrackLogger::instance().unlockSd();
        Serial.printf("[ONEDRIVE] Cannot open local file %s for read\n", localPath);
        return false;
    }
    uint64_t fileSize = file.fileSize();

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15000);

    if (!client.connect("graph.microsoft.com", 443)) {
        file.close();
        TrackLogger::instance().unlockSd();
        Serial.println("[ONEDRIVE] Failed to connect to graph.microsoft.com");
        return false;
    }

    // Build URL: /v1.0/me/drive/root:/VehicleTracks/{cloudPath}:/content
    String uri = String("/v1.0/me/drive/root:/") + CloudSyncConfig::GRAPH_ROOT_FOLDER + "/" + cloudPath + ":/content";

    client.printf("PUT %s HTTP/1.1\r\n", uri.c_str());
    client.print("Host: graph.microsoft.com\r\n");
    client.printf("Authorization: Bearer %s\r\n", _accessToken);
    client.printf("Content-Type: %s\r\n", contentType);
    client.printf("Content-Length: %llu\r\n", fileSize);
    client.print("Connection: close\r\n\r\n");

    uint8_t buf[1024];
    uint64_t sent = 0;
    while (file.available()) {
        int n = file.read(buf, sizeof(buf));
        if (n > 0) {
            client.write(buf, n);
            sent += n;
            if (fileSize > 0) {
                uint8_t pct = (uint8_t)((sent * 100) / fileSize);
                SystemState::instance().lock();
                SystemState::instance().getContextUnsafe().onedrive.uploadProgressPercent = pct;
                SystemState::instance().unlock();
            }
        }
    }
    file.close();
    TrackLogger::instance().unlockSd();

    // Read HTTP status response line
    String statusLine = client.readStringUntil('\n');
    bool ok = (statusLine.indexOf("200") >= 0 || statusLine.indexOf("201") >= 0);
    client.stop();

    if (!ok) {
        Serial.printf("[ONEDRIVE] Upload %s failed: %s\n", cloudPath, statusLine.c_str());
    } else {
        Serial.printf("[ONEDRIVE] Upload %s succeeded (%llu bytes)\n", cloudPath, fileSize);
    }
    return ok;
}

bool OneDriveSyncService::uploadLargeFileChunked(const char* cloudPath, const char* localPath) {
    TrackLogger::instance().lockSd();
    FsFile file;
    if (!TrackLogger::instance().openFileForRead(localPath, file)) {
        TrackLogger::instance().unlockSd();
        return false;
    }
    uint64_t fileSize = file.fileSize();
    TrackLogger::instance().unlockSd();

    if (fileSize < 4 * 1024 * 1024) {
        return uploadSmallFile(cloudPath, localPath, "application/gpx+xml");
    }

    // Step 1: Create Upload Session via HTTP POST
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15000);

    HTTPClient http;
    String sessUrl = String("https://graph.microsoft.com/v1.0/me/drive/root:/") +
                     CloudSyncConfig::GRAPH_ROOT_FOLDER + "/" + cloudPath + ":/createUploadSession";
    http.begin(client, sessUrl);
    http.addHeader("Authorization", String("Bearer ") + _accessToken);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST("{\"item\":{\"@microsoft.graph.conflictBehavior\":\"replace\"}}");
    if (code != 200) {
        Serial.printf("[ONEDRIVE] createUploadSession failed: %d\n", code);
        http.end();
        return false;
    }

    String resp = http.getString();
    http.end();

    JsonDocument doc;
    deserializeJson(doc, resp);
    String uploadUrl = doc["uploadUrl"] | "";
    if (uploadUrl.length() == 0) return false;

    // Step 2: Upload in 320 KB aligned chunks per Microsoft Graph API requirements
    constexpr uint32_t CHUNK_SIZE = 320 * 1024;
    uint8_t* chunkBuf = (uint8_t*)malloc(CHUNK_SIZE);
    if (!chunkBuf) {
        // Fallback to single PUT if PSRAM/RAM allocation fails
        return uploadSmallFile(cloudPath, localPath, "application/gpx+xml");
    }

    TrackLogger::instance().lockSd();
    if (!TrackLogger::instance().openFileForRead(localPath, file)) {
        free(chunkBuf);
        TrackLogger::instance().unlockSd();
        return false;
    }

    uint64_t offset = 0;
    bool success = true;

    while (offset < fileSize) {
        uint32_t toRead = (uint32_t)min((uint64_t)CHUNK_SIZE, fileSize - offset);
        int bytesRead = file.read(chunkBuf, toRead);
        if (bytesRead <= 0) break;

        HTTPClient chunkHttp;
        WiFiClientSecure chunkClient;
        chunkClient.setInsecure();
        chunkHttp.begin(chunkClient, uploadUrl);

        char rangeHdr[64];
        snprintf(rangeHdr, sizeof(rangeHdr), "bytes %llu-%llu/%llu", offset, offset + bytesRead - 1, fileSize);
        chunkHttp.addHeader("Content-Range", rangeHdr);
        chunkHttp.addHeader("Content-Length", String(bytesRead));

        int putCode = chunkHttp.sendRequest("PUT", chunkBuf, bytesRead);
        chunkHttp.end();

        if (putCode != 200 && putCode != 201 && putCode != 202) {
            Serial.printf("[ONEDRIVE] Chunk upload failed at offset %llu: %d\n", offset, putCode);
            success = false;
            break;
        }

        offset += bytesRead;
        uint8_t pct = (uint8_t)((offset * 100) / fileSize);
        SystemState::instance().lock();
        SystemState::instance().getContextUnsafe().onedrive.uploadProgressPercent = pct;
        SystemState::instance().unlock();
    }

    file.close();
    TrackLogger::instance().unlockSd();
    free(chunkBuf);
    return success;
}

bool OneDriveSyncService::syncTripsCsv() {
    SystemState::instance().lock();
    strncpy(SystemState::instance().getContextUnsafe().onedrive.currentUploadingFile, "trips.csv", 47);
    SystemState::instance().getContextUnsafe().onedrive.uploadProgressPercent = 0;
    SystemState::instance().unlock();

    return uploadSmallFile("trips.csv", StorageConfig::TRIPS_CSV_PATH, "text/csv");
}

bool OneDriveSyncService::uploadSingleTrack(const char* relPath) {
    SystemState::instance().lock();
    strncpy(SystemState::instance().getContextUnsafe().onedrive.currentUploadingFile, relPath, 47);
    SystemState::instance().getContextUnsafe().onedrive.uploadProgressPercent = 0;
    SystemState::instance().unlock();

    bool ok = uploadLargeFileChunked(relPath, relPath);
    if (ok) {
        markFileInSyncedLedger(relPath);
    }
    return ok;
}

bool OneDriveSyncService::performSync(uint32_t maxFiles, uint32_t timeoutMs) {
    if (WiFi.status() != WL_CONNECTED || !_isBound) {
        return false;
    }

    if (_mutex) {
        if (xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
            return false;
        }
    }

    _isSyncing = true;
    uint32_t startMs = millis();

    SystemState& state = SystemState::instance();
    state.lock();
    state.getContextUnsafe().onedrive.isSyncing = true;
    state.getContextUnsafe().onedrive.lastSyncError[0] = '\0';
    state.unlock();

    if (!refreshAccessToken()) {
        Serial.println("[ONEDRIVE] Could not obtain access token for sync.");
        _isSyncing = false;
        state.lock();
        state.getContextUnsafe().onedrive.isSyncing = false;
        strncpy(state.getContextUnsafe().onedrive.lastSyncError, "Token refresh failed", 47);
        state.unlock();
        if (_mutex) xSemaphoreGiveRecursive(_mutex);
        return false;
    }

    // 1. Sync trips.csv first (overwrite cloud copy with latest summaries)
    syncTripsCsv();

    // 2. Sync closed track files
    TrackFileEntry entries[64];
    size_t total = TrackLogger::instance().listTrackFiles(entries, 64);

    state.lock();
    const auto& st = state.getContextUnsafe().storage;
    char curW[64], curC[64], curD[64], curS[64];
    strncpy(curW, st.currentWgsFile, sizeof(curW) - 1);
    strncpy(curC, st.currentGcjFile, sizeof(curC) - 1);
    strncpy(curD, st.currentDailyFile, sizeof(curD) - 1);
    strncpy(curS, st.currentSubFile, sizeof(curS) - 1);
    state.unlock();

    uint32_t uploadedCount = 0;
    for (size_t i = 0; i < total; i++) {
        if (millis() - startMs > timeoutMs) {
            Serial.println("[ONEDRIVE] Sync reached timeout limit, deferring remaining.");
            break;
        }
        if (maxFiles > 0 && uploadedCount >= maxFiles) {
            break;
        }

        const char* name = entries[i].name;
        // Skip files currently open and being actively recorded
        if (curW[0] != '\0' && strcmp(name, curW) == 0) continue;
        if (curC[0] != '\0' && strcmp(name, curC) == 0) continue;
        if (curD[0] != '\0' && strcmp(name, curD) == 0) continue;
        if (curS[0] != '\0' && strcmp(name, curS) == 0) continue;

        if (!isFileInSyncedLedger(name)) {
            Serial.printf("[ONEDRIVE] Syncing %s to OneDrive...\n", name);
            if (uploadSingleTrack(name)) {
                uploadedCount++;
            }
        }
    }

    _isSyncing = false;
    _lastSyncEpoch = (uint32_t)time(nullptr);

    Preferences prefs;
    if (prefs.begin("m5track", false)) {
        prefs.putUInt("od_last_sync", _lastSyncEpoch);
        prefs.end();
    }

    uint32_t rem = scanPendingFilesCount();
    state.lock();
    state.getContextUnsafe().onedrive.isSyncing = false;
    state.getContextUnsafe().onedrive.lastSyncEpoch = _lastSyncEpoch;
    state.getContextUnsafe().onedrive.pendingCount = rem;
    state.getContextUnsafe().onedrive.currentUploadingFile[0] = '\0';
    state.getContextUnsafe().onedrive.uploadProgressPercent = 0;
    state.unlock();

    Serial.printf("[ONEDRIVE] Sync finished. Uploaded: %u, Remaining: %u\n", uploadedCount, rem);
    if (_mutex) xSemaphoreGiveRecursive(_mutex);
    return true;
}

bool OneDriveSyncService::syncBeforeSleep(uint32_t timeoutMs) {
    if (WiFi.status() != WL_CONNECTED || !_isBound) {
        return false;
    }
    Serial.println("[ONEDRIVE] Pre-sleep cloud sync started...");
    return performSync(0, timeoutMs);
}

void OneDriveSyncService::taskLoop() {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // 1. Device Code Authorization Polling
        if (_authInProgress) {
            if (millis() > _pollExpireEpoch) {
                Serial.println("[ONEDRIVE] Device login timed out.");
                cancelDeviceLogin();
            } else if (millis() - _lastPollMs >= _pollIntervalMs) {
                _lastPollMs = millis();
                pollDeviceCodeApproval();
            }
            continue;
        }

        if (!_isBound || WiFi.status() != WL_CONNECTED) {
            continue;
        }

        // 2. Periodic or Manual Sync Evaluation
        bool shouldSync = _manualSyncRequested;
        if (!shouldSync && _autoSyncEnabled) {
            if (millis() - _lastAutoCheckMs >= (CloudSyncConfig::AUTO_SYNC_INTERVAL_S * 1000)) {
                _lastAutoCheckMs = millis();
                if (scanPendingFilesCount() > 0) {
                    shouldSync = true;
                }
            }
        }

        if (shouldSync) {
            _manualSyncRequested = false;
            performSync(0, 60000);
        }
    }
}
