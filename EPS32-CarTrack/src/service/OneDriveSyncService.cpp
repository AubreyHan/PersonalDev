#include "OneDriveSyncService.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace {
constexpr uint16_t AUTH_HTTP_TIMEOUT_MS   = 12000U;
constexpr uint16_t UPLOAD_HTTP_TIMEOUT_MS = 20000U;

bool endsWithIgnoreCase(const char* str, const char* suffix) {
    if (str == nullptr || suffix == nullptr) {
        return false;
    }
    const size_t sLen   = strlen(str);
    const size_t sufLen = strlen(suffix);
    if (sLen < sufLen) {
        return false;
    }
    return strcasecmp(str + (sLen - sufLen), suffix) == 0;
}

uint32_t currentEpochUtcOrZero() {
    uint32_t utc = 0;
    {
        StateGuard lock(SystemState::instance());
        utc = SystemState::instance().data().time.epochUtc;
    }
    if (utc < 1700000000UL) {
        const time_t sysNow = time(nullptr);
        if (sysNow > 1700000000) {
            utc = static_cast<uint32_t>(sysNow);
        }
    }
    return utc;
}
} // namespace

OneDriveSyncService& OneDriveSyncService::instance() {
    static OneDriveSyncService s_instance;
    return s_instance;
}

OneDriveSyncService::OneDriveSyncService()
    : _sd(nullptr),
      _sdMutex(nullptr),
      _sdMutexRecursive(false),
      _sdLockCb(nullptr),
      _sdUnlockCb(nullptr),
      _initialized(false),
      _isBound(false),
      _autoSyncEnabled(true),
      _accessTokenExpiryMs(0),
      _deviceCodeStartMs(0),
      _deviceCodeExpiresMs(0),
      _pollIntervalMs(5000UL),
      _lastPollMs(0),
      _lastCountdownTickMs(0),
      _lastBgSyncCheckMs(0),
      _psramChunkBuf(nullptr) {}

OneDriveSyncService::~OneDriveSyncService() {
    if (_psramChunkBuf != nullptr) {
        free(_psramChunkBuf);
        _psramChunkBuf = nullptr;
    }
}

bool OneDriveSyncService::begin(SdFs* sd, SemaphoreHandle_t sdMutex, bool isRecursiveMutex) {
    if (sd != nullptr) {
        attachSdStorage(sd, sdMutex, isRecursiveMutex);
    }
    if (_psramChunkBuf == nullptr) {
        void* mem = heap_caps_malloc(SESSION_CHUNK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (mem != nullptr) {
            _psramChunkBuf = static_cast<uint8_t*>(mem);
        }
    }
    loadConfigAndTokensFromNvs();
    if (_sd != nullptr) {
        refreshLedgerStats();
    }
    _initialized = true;
    return true;
}

void OneDriveSyncService::attachSdStorage(SdFs* sd, SemaphoreHandle_t sdMutex, bool isRecursiveMutex) {
    _sd               = sd;
    _sdMutex          = sdMutex;
    _sdMutexRecursive = isRecursiveMutex;
}

void OneDriveSyncService::setSdLockCallbacks(SdLockCallback lockCb, SdUnlockCallback unlockCb) {
    _sdLockCb   = lockCb;
    _sdUnlockCb = unlockCb;
}

bool OneDriveSyncService::lockSd(uint32_t timeoutMs) const {
    if (_sdLockCb) {
        return _sdLockCb(timeoutMs);
    }
    if (_sdMutex != nullptr) {
        const TickType_t ticks = pdMS_TO_TICKS(timeoutMs);
        if (_sdMutexRecursive) {
            return (xSemaphoreTakeRecursive(_sdMutex, ticks) == pdTRUE);
        }
        return (xSemaphoreTake(_sdMutex, ticks) == pdTRUE);
    }
    return (_sd != nullptr);
}

void OneDriveSyncService::unlockSd() const {
    if (_sdUnlockCb) {
        _sdUnlockCb();
        return;
    }
    if (_sdMutex != nullptr) {
        if (_sdMutexRecursive) {
            xSemaphoreGiveRecursive(_sdMutex);
        } else {
            xSemaphoreGive(_sdMutex);
        }
    }
}

void OneDriveSyncService::loadConfigAndTokensFromNvs() {
    Preferences prefs;
    char clientId[64] = {};
    char tenant[24]   = {};
    char email[64]    = {};

    if (prefs.begin(NvsConfig::NAMESPACE, true)) {
        _refreshToken    = prefs.getString(NvsConfig::KEY_OD_REFRESH_TOK, "");
        _autoSyncEnabled = prefs.getBool(NvsConfig::KEY_OD_AUTO_SYNC, true);
        prefs.getString(NvsConfig::KEY_OD_CLIENT_ID, clientId, sizeof(clientId));
        prefs.getString(NvsConfig::KEY_OD_TENANT, tenant, sizeof(tenant));
        prefs.getString(NvsConfig::KEY_OD_USER_EMAIL, email, sizeof(email));
        prefs.end();
    }

    _accountEmail = email;
    _isBound      = (_refreshToken.length() >= 16);

    StateGuard lock(SystemState::instance());
    auto& st = SystemState::instance().data();
    if (clientId[0] != '\0' && st.settings.oneDriveClientId[0] == '\0') {
        strncpy(st.settings.oneDriveClientId, clientId, sizeof(st.settings.oneDriveClientId) - 1);
    }
    if (tenant[0] != '\0') {
        strncpy(st.settings.oneDriveTenant, tenant, sizeof(st.settings.oneDriveTenant) - 1);
    } else if (st.settings.oneDriveTenant[0] == '\0') {
        strncpy(st.settings.oneDriveTenant, CloudConfig::ONEDRIVE_DEFAULT_TENANT,
                sizeof(st.settings.oneDriveTenant) - 1);
    }
    st.settings.oneDriveAutoSync = _autoSyncEnabled;

    st.oneDrive.isBound         = _isBound;
    st.oneDrive.autoSyncEnabled = _autoSyncEnabled;
    st.oneDrive.bindState       = _isBound ? ONEDRIVE_BOUND_ACTIVE : ONEDRIVE_UNBOUND;
    st.oneDrive.transferState   = ONEDRIVE_IDLE;
    strncpy(st.oneDrive.accountEmail, _accountEmail.c_str(), sizeof(st.oneDrive.accountEmail) - 1);
    st.oneDrive.accountEmail[sizeof(st.oneDrive.accountEmail) - 1] = '\0';
}

void OneDriveSyncService::resolveClientConfig(String& outClientId, String& outTenant) const {
    char cid[64] = {};
    char ten[24] = {};
    {
        StateGuard lock(SystemState::instance());
        const auto& st = SystemState::instance().data();
        strncpy(cid, st.settings.oneDriveClientId, sizeof(cid) - 1);
        strncpy(ten, st.settings.oneDriveTenant, sizeof(ten) - 1);
    }
    if (cid[0] == '\0') {
        Preferences prefs;
        if (prefs.begin(NvsConfig::NAMESPACE, true)) {
            prefs.getString(NvsConfig::KEY_OD_CLIENT_ID, cid, sizeof(cid));
            if (ten[0] == '\0') {
                prefs.getString(NvsConfig::KEY_OD_TENANT, ten, sizeof(ten));
            }
            prefs.end();
        }
    }
    outClientId = (cid[0] != '\0') ? String(cid) : String(CloudConfig::ONEDRIVE_DEFAULT_CLIENT_ID);
    outClientId.trim();
    outTenant = (ten[0] != '\0') ? String(ten) : String(CloudConfig::ONEDRIVE_DEFAULT_TENANT);
    outTenant.trim();
    if (outTenant.isEmpty()) {
        outTenant = CloudConfig::ONEDRIVE_DEFAULT_TENANT;
    }
}

void OneDriveSyncService::update() {
    if (!_initialized) {
        begin();
    }

    // 1. Check highest-priority Pre-Sleep 20-min Standstill Handshake first!
    bool preSleepReq   = false;
    bool cmdStartCode  = false;
    bool cmdCancelCode = false;
    bool cmdUnbind     = false;
    bool cmdSyncNow    = false;
    bool cmdSetAuto    = false;
    bool reqAutoVal    = true;
    OneDriveBindState curBindState = ONEDRIVE_UNBOUND;

    {
        StateGuard lock(SystemState::instance());
        auto& st = SystemState::instance().data();

        if (st.power.preSleepCloudSyncReq && !st.power.preSleepCloudSyncAck) {
            preSleepReq = true;
        }
        if (st.oneDrive.cmdStartDeviceCode) {
            cmdStartCode = true;
            st.oneDrive.cmdStartDeviceCode = false;
        }
        if (st.oneDrive.cmdCancelDeviceCode) {
            cmdCancelCode = true;
            st.oneDrive.cmdCancelDeviceCode = false;
        }
        if (st.oneDrive.cmdUnbind) {
            cmdUnbind = true;
            st.oneDrive.cmdUnbind = false;
        }
        if (st.oneDrive.cmdSyncNow) {
            cmdSyncNow = true;
            st.oneDrive.cmdSyncNow = false;
        }
        if (st.oneDrive.cmdSetAutoSync) {
            cmdSetAuto = true;
            reqAutoVal = st.oneDrive.requestedAutoSyncVal;
            st.oneDrive.cmdSetAutoSync = false;
        }
        curBindState = st.oneDrive.bindState;
    }

    if (preSleepReq) {
        syncBeforeSleep(CloudConfig::ONEDRIVE_PRE_SLEEP_TIMEOUT_MS);
        // Always acknowledge PowerMonitorService so vehicle sleep is never blocked indefinitely
        StateGuard lock(SystemState::instance());
        SystemState::instance().data().power.preSleepCloudSyncAck = true;
        return;
    }

    if (cmdSetAuto) {
        _autoSyncEnabled = reqAutoVal;
        Preferences prefs;
        if (prefs.begin(NvsConfig::NAMESPACE, false)) {
            prefs.putBool(NvsConfig::KEY_OD_AUTO_SYNC, _autoSyncEnabled);
            prefs.end();
        }
        StateGuard lock(SystemState::instance());
        auto& st = SystemState::instance().data();
        st.oneDrive.autoSyncEnabled  = _autoSyncEnabled;
        st.settings.oneDriveAutoSync = _autoSyncEnabled;
    }

    if (cmdUnbind) {
        unbindAccount();
        return;
    }

    if (cmdCancelCode) {
        cancelDeviceCodeFlow();
        return;
    }

    if (cmdStartCode) {
        startDeviceCodeFlow();
        return;
    }

    // 2. Non-blocking Device Code Polling State Machine
    if (curBindState == ONEDRIVE_WAIT_USER_VERIFY && !_deviceCode.isEmpty()) {
        const uint32_t nowMs = millis();

        // Update countdown every 1s for ToolOneDrive UI
        if ((nowMs - _lastCountdownTickMs) >= 1000UL) {
            _lastCountdownTickMs = nowMs;
            const uint32_t elapsedMs = nowMs - _deviceCodeStartMs;
            const uint32_t totalWindowMs = _deviceCodeExpiresMs - _deviceCodeStartMs;
            if (elapsedMs >= totalWindowMs) {
                _deviceCode = "";
                setLastErrorState(ONEDRIVE_AUTH_FAILED, "授权码已过期，请重新获取");
                return;
            }
            const uint16_t remSec = static_cast<uint16_t>((totalWindowMs - elapsedMs) / 1000UL);
            StateGuard lock(SystemState::instance());
            SystemState::instance().data().oneDrive.deviceCodeExpiresSec = remSec;
        }

        if ((nowMs - _lastPollMs) >= _pollIntervalMs) {
            _lastPollMs = nowMs;
            pollDeviceCodeTokenOnce();
        }
        return;
    }

    // 3. Manual or Background Incremental Sync
    if (cmdSyncNow) {
        syncPendingFiles(0);
        return;
    }

    if (_isBound && _autoSyncEnabled && WiFi.status() == WL_CONNECTED) {
        const uint32_t nowMs = millis();
        if (_lastBgSyncCheckMs == 0 ||
            (nowMs - _lastBgSyncCheckMs) >= CloudConfig::ONEDRIVE_BG_SYNC_INTERVAL_MS) {
            _lastBgSyncCheckMs = nowMs;
            syncPendingFiles(15000UL); // Keep background slice bounded so netTask stays responsive
        }
    }
}

bool OneDriveSyncService::startDeviceCodeFlow() {
    if (WiFi.status() != WL_CONNECTED) {
        setLastErrorState(ONEDRIVE_AUTH_FAILED, "请先连接 Wi-Fi 网络后再绑定");
        return false;
    }

    String clientId, tenant;
    resolveClientConfig(clientId, tenant);
    if (clientId.isEmpty()) {
        setLastErrorState(ONEDRIVE_AUTH_FAILED, "未配置 OneDrive Client ID (请在配网页填写)");
        return false;
    }

    {
        StateGuard lock(SystemState::instance());
        auto& st = SystemState::instance().data();
        st.oneDrive.bindState = ONEDRIVE_REQ_DEVICE_CODE;
        st.oneDrive.lastError[0] = '\0';
    }

    String url = "https://";
    url += CloudConfig::ONEDRIVE_AUTH_HOST;
    url += "/";
    url += tenant;
    url += "/oauth2/v2.0/devicecode";

    String body = "client_id=";
    body += urlEncodeFormComponent(clientId);
    body += "&scope=";
    body += urlEncodeFormComponent(String(CloudConfig::ONEDRIVE_SCOPE));

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(AUTH_HTTP_TIMEOUT_MS / 1000U);

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(AUTH_HTTP_TIMEOUT_MS);
    http.setAcceptEncoding("identity");

    if (!http.begin(client, url)) {
        setLastErrorState(ONEDRIVE_AUTH_FAILED, "无法连接微软认证服务器");
        return false;
    }
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    const int code = http.POST(body);
    const String resp = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, resp)) {
        setLastErrorState(ONEDRIVE_AUTH_FAILED, "解析授权响应失败");
        return false;
    }

    if (code != HTTP_CODE_OK) {
        const char* errDesc = doc["error_description"] | (doc["error"] | "DeviceCode 请求失败");
        setLastErrorState(ONEDRIVE_AUTH_FAILED, errDesc);
        return false;
    }

    const char* devCode  = doc["device_code"] | "";
    const char* userCode = doc["user_code"] | "";
    const char* verUri   = doc["verification_uri"] | "https://microsoft.com/devicelogin";
    const uint32_t expIn = doc["expires_in"] | 900U;
    const uint32_t intv  = doc["interval"] | 5U;

    if (devCode[0] == '\0' || userCode[0] == '\0') {
        setLastErrorState(ONEDRIVE_AUTH_FAILED, "设备码字段缺失");
        return false;
    }

    _deviceCode          = devCode;
    _deviceCodeStartMs   = millis();
    _deviceCodeExpiresMs = _deviceCodeStartMs + expIn * 1000UL;
    _pollIntervalMs      = ((intv < 5U) ? 5U : intv) * 1000UL;
    _lastPollMs          = _deviceCodeStartMs;
    _lastCountdownTickMs = _deviceCodeStartMs;

    {
        StateGuard lock(SystemState::instance());
        auto& od = SystemState::instance().data().oneDrive;
        od.bindState = ONEDRIVE_WAIT_USER_VERIFY;
        strncpy(od.userCode, userCode, sizeof(od.userCode) - 1);
        od.userCode[sizeof(od.userCode) - 1] = '\0';
        strncpy(od.verificationUri, verUri, sizeof(od.verificationUri) - 1);
        od.verificationUri[sizeof(od.verificationUri) - 1] = '\0';
        od.deviceCodeExpiresSec = static_cast<uint16_t>((expIn > 65535U) ? 65535U : expIn);
        od.lastError[0] = '\0';
    }

    Serial.printf("[ONEDRIVE] Device code issued: %s -> %s\n", userCode, verUri);
    return true;
}

void OneDriveSyncService::cancelDeviceCodeFlow() {
    _deviceCode = "";
    StateGuard lock(SystemState::instance());
    auto& od = SystemState::instance().data().oneDrive;
    od.bindState            = _isBound ? ONEDRIVE_BOUND_ACTIVE : ONEDRIVE_UNBOUND;
    od.userCode[0]          = '\0';
    od.deviceCodeExpiresSec = 0;
}

void OneDriveSyncService::pollDeviceCodeTokenOnce() {
    if (WiFi.status() != WL_CONNECTED || _deviceCode.isEmpty()) {
        return;
    }

    String clientId, tenant;
    resolveClientConfig(clientId, tenant);

    String url = "https://";
    url += CloudConfig::ONEDRIVE_AUTH_HOST;
    url += "/";
    url += tenant;
    url += "/oauth2/v2.0/token";

    String body = "grant_type=";
    body += urlEncodeFormComponent("urn:ietf:params:oauth:grant-type:device_code");
    body += "&client_id=";
    body += urlEncodeFormComponent(clientId);
    body += "&device_code=";
    body += urlEncodeFormComponent(_deviceCode);

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(AUTH_HTTP_TIMEOUT_MS / 1000U);

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(AUTH_HTTP_TIMEOUT_MS);
    http.setAcceptEncoding("identity");

    if (!http.begin(client, url)) {
        return; // Transient network hiccup; retry next interval
    }
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    const int code = http.POST(body);
    const String resp = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, resp)) {
        return;
    }

    if (code == HTTP_CODE_OK) {
        const char* accTok = doc["access_token"] | "";
        const char* refTok = doc["refresh_token"] | "";
        const uint32_t expIn = doc["expires_in"] | 3600U;

        if (accTok[0] == '\0' || refTok[0] == '\0') {
            setLastErrorState(ONEDRIVE_AUTH_FAILED, "令牌缺失 offline_access");
            _deviceCode = "";
            return;
        }

        _accessToken  = accTok;
        _refreshToken = refTok;
        _deviceCode   = "";
        _isBound      = true;
        const uint32_t safeSec = (expIn > 120U) ? (expIn - 90U) : expIn;
        _accessTokenExpiryMs = millis() + safeSec * 1000UL;

        Preferences prefs;
        if (prefs.begin(NvsConfig::NAMESPACE, false)) {
            prefs.putString(NvsConfig::KEY_OD_REFRESH_TOK, _refreshToken);
            const uint32_t nowUtc = currentEpochUtcOrZero();
            if (nowUtc > 0) {
                prefs.putUInt(NvsConfig::KEY_OD_EXPIRES_UTC, nowUtc + expIn);
            }
            prefs.end();
        }

        fetchUserProfileEmail();

        {
            StateGuard lock(SystemState::instance());
            auto& od = SystemState::instance().data().oneDrive;
            od.isBound              = true;
            od.bindState            = ONEDRIVE_BOUND_ACTIVE;
            od.userCode[0]          = '\0';
            od.deviceCodeExpiresSec = 0;
            od.lastError[0]         = '\0';
            strncpy(od.accountEmail, _accountEmail.c_str(), sizeof(od.accountEmail) - 1);
            od.accountEmail[sizeof(od.accountEmail) - 1] = '\0';
        }

        refreshLedgerStats();
        Serial.printf("[ONEDRIVE] Bound successfully: %s\n", _accountEmail.c_str());
        return;
    }

    // Handle RFC 8628 error codes on HTTP 400
    const char* errCode = doc["error"] | "";
    if (strcmp(errCode, "authorization_pending") == 0) {
        // Normal waiting state - keep silent polling at _pollIntervalMs
        return;
    }
    if (strcmp(errCode, "slow_down") == 0) {
        // RFC 8628 & #3 §3.2 mandatory rule: increase polling interval by +5 seconds
        _pollIntervalMs += 5000UL;
        return;
    }
    if (strcmp(errCode, "expired_token") == 0) {
        _deviceCode = "";
        setLastErrorState(ONEDRIVE_AUTH_FAILED, "验证码已超时过期，请重新扫码");
        return;
    }
    if (strcmp(errCode, "authorization_declined") == 0) {
        _deviceCode = "";
        setLastErrorState(ONEDRIVE_AUTH_FAILED, "用户已拒绝授权");
        return;
    }
    if (strcmp(errCode, "bad_verification_code") == 0) {
        _deviceCode = "";
        setLastErrorState(ONEDRIVE_AUTH_FAILED, "设备验证码无效");
        return;
    }
}

bool OneDriveSyncService::refreshAccessToken() {
    if (_refreshToken.isEmpty() || WiFi.status() != WL_CONNECTED) {
        return false;
    }

    String clientId, tenant;
    resolveClientConfig(clientId, tenant);
    if (clientId.isEmpty()) {
        return false;
    }

    String url = "https://";
    url += CloudConfig::ONEDRIVE_AUTH_HOST;
    url += "/";
    url += tenant;
    url += "/oauth2/v2.0/token";

    String body = "client_id=";
    body += urlEncodeFormComponent(clientId);
    body += "&grant_type=refresh_token&refresh_token=";
    body += urlEncodeFormComponent(_refreshToken);
    body += "&scope=";
    body += urlEncodeFormComponent(String(CloudConfig::ONEDRIVE_SCOPE));

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(AUTH_HTTP_TIMEOUT_MS / 1000U);

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(AUTH_HTTP_TIMEOUT_MS);
    http.setAcceptEncoding("identity");

    if (!http.begin(client, url)) {
        return false;
    }
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    const int code = http.POST(body);
    const String resp = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, resp)) {
        return false;
    }

    if (code != HTTP_CODE_OK) {
        const char* errCode = doc["error"] | "";
        if (strcmp(errCode, "invalid_grant") == 0) {
            _isBound      = false;
            _accessToken  = "";
            _refreshToken = "";
            setLastErrorState(ONEDRIVE_AUTH_FAILED, "授权令牌已失效，请重新绑定");
        }
        return false;
    }

    const char* accTok = doc["access_token"] | "";
    const char* newRef = doc["refresh_token"] | "";
    const uint32_t expIn = doc["expires_in"] | 3600U;
    if (accTok[0] == '\0') {
        return false;
    }

    _accessToken = accTok;
    const uint32_t safeSec = (expIn > 120U) ? (expIn - 90U) : expIn;
    _accessTokenExpiryMs = millis() + safeSec * 1000UL;

    // Persist rolling rotated refresh_token to NVS immediately (#3 §3.2)
    if (newRef[0] != '\0' && _refreshToken != newRef) {
        _refreshToken = newRef;
        Preferences prefs;
        if (prefs.begin(NvsConfig::NAMESPACE, false)) {
            prefs.putString(NvsConfig::KEY_OD_REFRESH_TOK, _refreshToken);
            prefs.end();
        }
    }

    if (_accountEmail.isEmpty()) {
        fetchUserProfileEmail();
    }
    return true;
}

bool OneDriveSyncService::ensureValidAccessToken() {
    if (!_isBound || _refreshToken.isEmpty()) {
        return false;
    }
    if (!_accessToken.isEmpty() && _accessTokenExpiryMs != 0 &&
        static_cast<int32_t>(_accessTokenExpiryMs - millis()) > 0) {
        return true;
    }
    return refreshAccessToken();
}

void OneDriveSyncService::fetchUserProfileEmail() {
    if (_accessToken.isEmpty() || WiFi.status() != WL_CONNECTED) {
        return;
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(8);

    HTTPClient http;
    http.setConnectTimeout(6000);
    http.setTimeout(8000);
    http.setAcceptEncoding("identity");

    String url = "https://";
    url += CloudConfig::ONEDRIVE_GRAPH_HOST;
    url += "/v1.0/me?$select=mail,userPrincipalName";

    if (!http.begin(client, url)) {
        return;
    }
    http.addHeader("Authorization", String("Bearer ") + _accessToken);
    const int code = http.GET();
    const String resp = http.getString();
    http.end();

    if (code != HTTP_CODE_OK) {
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, resp)) {
        return;
    }
    const char* mail = doc["mail"] | "";
    const char* upn  = doc["userPrincipalName"] | "";
    const char* chosen = (mail[0] != '\0') ? mail : upn;
    if (chosen[0] != '\0') {
        _accountEmail = chosen;
        Preferences prefs;
        if (prefs.begin(NvsConfig::NAMESPACE, false)) {
            prefs.putString(NvsConfig::KEY_OD_USER_EMAIL, _accountEmail);
            prefs.end();
        }
        StateGuard lock(SystemState::instance());
        auto& od = SystemState::instance().data().oneDrive;
        strncpy(od.accountEmail, _accountEmail.c_str(), sizeof(od.accountEmail) - 1);
        od.accountEmail[sizeof(od.accountEmail) - 1] = '\0';
    }
}

void OneDriveSyncService::unbindAccount() {
    _isBound             = false;
    _accessToken         = "";
    _refreshToken        = "";
    _deviceCode          = "";
    _accountEmail        = "";
    _accessTokenExpiryMs = 0;

    Preferences prefs;
    if (prefs.begin(NvsConfig::NAMESPACE, false)) {
        prefs.remove(NvsConfig::KEY_OD_REFRESH_TOK);
        prefs.remove(NvsConfig::KEY_OD_ACCESS_TOK);
        prefs.remove(NvsConfig::KEY_OD_EXPIRES_UTC);
        prefs.remove(NvsConfig::KEY_OD_USER_EMAIL);
        prefs.end();
    }

    StateGuard lock(SystemState::instance());
    auto& od = SystemState::instance().data().oneDrive;
    od.isBound              = false;
    od.bindState            = ONEDRIVE_UNBOUND;
    od.transferState        = ONEDRIVE_IDLE;
    od.accountEmail[0]      = '\0';
    od.userCode[0]          = '\0';
    od.deviceCodeExpiresSec = 0;
    od.currentFilePercent   = 0;
    od.currentFileName[0]   = '\0';
    od.lastError[0]         = '\0';
}

bool OneDriveSyncService::loadLedger(std::vector<LedgerEntry>& outEntries) const {
    outEntries.clear();
    if (_sd == nullptr || !lockSd(2000)) {
        return false;
    }

    FsFile ledger;
    if (ledger.open(_sd, SdCardConfig::FILE_SYNCED_LEDGER, O_RDONLY)) {
        char lineBuf[128] = {};
        while (ledger.fgets(lineBuf, sizeof(lineBuf)) > 0) {
            size_t len = strlen(lineBuf);
            while (len > 0 && (lineBuf[len - 1] == '\n' || lineBuf[len - 1] == '\r' || lineBuf[len - 1] == ' ')) {
                lineBuf[--len] = '\0';
            }
            if (len == 0 || lineBuf[0] == '#') {
                continue;
            }

            char* sep = strchr(lineBuf, '|');
            String   relPath;
            uint64_t syncedBytes = UINT64_MAX;
            if (sep != nullptr) {
                *sep        = '\0';
                relPath     = lineBuf;
                syncedBytes = strtoull(sep + 1, nullptr, 10);
            } else {
                relPath = lineBuf;
            }
            if (relPath.isEmpty()) {
                continue;
            }

            bool updatedExisting = false;
            for (auto& existing : outEntries) {
                if (existing.relPath == relPath) {
                    existing.syncedSize = syncedBytes;
                    updatedExisting     = true;
                    break;
                }
            }
            if (!updatedExisting) {
                LedgerEntry entry;
                entry.relPath    = relPath;
                entry.syncedSize = syncedBytes;
                outEntries.push_back(entry);
            }
        }
        ledger.close();
    }

    unlockSd();
    return true;
}

bool OneDriveSyncService::appendLedgerRecord(const String& relPath, uint64_t fileSize) {
    if (_sd == nullptr || !lockSd(2000)) {
        return false;
    }

    bool ok = false;
    FsFile ledger;
    if (ledger.open(_sd, SdCardConfig::FILE_SYNCED_LEDGER, O_WRONLY | O_CREAT | O_APPEND)) {
        char line[144] = {};
        const int n = snprintf(line, sizeof(line), "%s|%llu\n",
                               relPath.c_str(), static_cast<unsigned long long>(fileSize));
        if (n > 0) {
            ok = (ledger.write(line, static_cast<size_t>(n)) == static_cast<size_t>(n));
        }
        ledger.sync();
        ledger.close();
    }

    unlockSd();
    return ok;
}

bool OneDriveSyncService::isGpxFileProperlyClosedLocked(const char* fullPath, uint64_t fileSize) const {
    if (_sd == nullptr || fullPath == nullptr || fileSize < 32) {
        return false;
    }
    FsFile f;
    if (!f.open(_sd, fullPath, O_RDONLY)) {
        return false;
    }
    const uint64_t tailBytes = (fileSize > 96ULL) ? 96ULL : fileSize;
    char tailBuf[104] = {};
    bool closedOk = false;
    if (f.seekSet(fileSize - tailBytes)) {
        const int r = f.read(tailBuf, static_cast<size_t>(tailBytes));
        if (r > 0) {
            tailBuf[r] = '\0';
            closedOk   = (strstr(tailBuf, "</gpx>") != nullptr);
        }
    }
    f.close();
    return closedOk;
}

bool OneDriveSyncService::collectPendingUploads(std::vector<PendingUploadItem>& outPending,
                                                uint16_t& outSyncedCount) const {
    outPending.clear();
    outSyncedCount = 0;

    bool sdMounted = false;
    bool isRecording = false;
    char activePaths[6][48] = {};
    {
        StateGuard lock(SystemState::instance());
        const auto& st = SystemState::instance().data().storage;
        sdMounted   = st.isMounted;
        isRecording = st.isRecording;
        strncpy(activePaths[0], st.activeSingleWgsPath, sizeof(activePaths[0]) - 1);
        strncpy(activePaths[1], st.activeSingleGcjPath, sizeof(activePaths[1]) - 1);
        strncpy(activePaths[2], st.activeDailyWgsPath,  sizeof(activePaths[2]) - 1);
        strncpy(activePaths[3], st.activeDailyGcjPath,  sizeof(activePaths[3]) - 1);
        strncpy(activePaths[4], st.activeSubWgsPath,    sizeof(activePaths[4]) - 1);
        strncpy(activePaths[5], st.activeSubGcjPath,    sizeof(activePaths[5]) - 1);
    }

    if (_sd == nullptr || !sdMounted) {
        return false;
    }

    std::vector<LedgerEntry> ledger;
    loadLedger(ledger);

    auto findLedgerSize = [&ledger](const String& rel, uint64_t& outSize) -> bool {
        for (const auto& e : ledger) {
            if (e.relPath == rel) {
                outSize = e.syncedSize;
                return true;
            }
        }
        return false;
    };

    auto isActiveStream = [&](const String& rel) -> bool {
        if (!isRecording) {
            return false;
        }
        for (size_t i = 0; i < 6; ++i) {
            if (activePaths[i][0] != '\0' && rel.equalsIgnoreCase(activePaths[i])) {
                return true;
            }
        }
        return false;
    };

    if (!lockSd(2500)) {
        return false;
    }

    // 1. Check /tracks/trips.csv first (highest priority summary report)
    {
        FsFile csvFile;
        if (csvFile.open(_sd, SdCardConfig::FILE_TRIPS_CSV, O_RDONLY)) {
            const uint64_t sz = csvFile.fileSize();
            csvFile.close();
            if (sz > 0) {
                const String rel = "trips.csv";
                uint64_t syncedSz = 0;
                if (findLedgerSize(rel, syncedSz) && (syncedSz == sz || syncedSz == UINT64_MAX)) {
                    outSyncedCount++;
                } else {
                    PendingUploadItem item;
                    item.relPath  = rel;
                    item.fullPath = SdCardConfig::FILE_TRIPS_CSV;
                    item.fileSize = sz;
                    outPending.push_back(item);
                }
            }
        }
    }

    // 2. Scan /tracks/single, /tracks/daily, /tracks/sub for closed GPX files
    struct FolderSpec {
        const char* fullDir;
        const char* relPrefix;
    };
    static constexpr FolderSpec kFolders[3] = {
        { SdCardConfig::DIR_SINGLE, "single/" },
        { SdCardConfig::DIR_DAILY,  "daily/"  },
        { SdCardConfig::DIR_SUB,    "sub/"    }
    };

    for (const auto& folder : kFolders) {
        FsFile dir;
        if (!dir.open(_sd, folder.fullDir, O_RDONLY)) {
            continue;
        }
        FsFile entry;
        char nameBuf[64] = {};
        while (entry.openNext(&dir, O_RDONLY)) {
            const bool isRegFile = entry.isFile() && !entry.isHidden();
            const uint64_t sz    = entry.fileSize();
            nameBuf[0]           = '\0';
            if (isRegFile) {
                entry.getName(nameBuf, sizeof(nameBuf));
            }
            entry.close();

            if (!isRegFile || nameBuf[0] == '\0' || nameBuf[0] == '.' || sz == 0) {
                continue;
            }
            if (!endsWithIgnoreCase(nameBuf, ".gpx")) {
                continue;
            }

            String rel = folder.relPrefix;
            rel += nameBuf;

            // Strictly skip any actively written recording file!
            if (isActiveStream(rel)) {
                continue;
            }

            String full = folder.fullDir;
            full += "/";
            full += nameBuf;

            uint64_t syncedSz = 0;
            if (findLedgerSize(rel, syncedSz) && (syncedSz == sz || syncedSz == UINT64_MAX)) {
                outSyncedCount++;
                continue;
            }

            // Verify GPX tail closure (</gpx>) before queuing upload
            if (!isGpxFileProperlyClosedLocked(full.c_str(), sz)) {
                continue;
            }

            PendingUploadItem item;
            item.relPath  = rel;
            item.fullPath = full;
            item.fileSize = sz;
            outPending.push_back(item);
        }
        dir.close();
    }

    unlockSd();
    return true;
}

bool OneDriveSyncService::refreshLedgerStats() {
    std::vector<PendingUploadItem> pending;
    uint16_t syncedCount = 0;
    if (!collectPendingUploads(pending, syncedCount)) {
        return false;
    }

    StateGuard lock(SystemState::instance());
    auto& od = SystemState::instance().data().oneDrive;
    od.pendingFiles     = static_cast<uint16_t>((pending.size() > 65535U) ? 65535U : pending.size());
    od.syncedTotalFiles = syncedCount;
    return true;
}

bool OneDriveSyncService::syncBeforeSleep(uint32_t timeoutMs) {
    if (!_isBound || WiFi.status() != WL_CONNECTED || _sd == nullptr) {
        return false;
    }

    {
        StateGuard lock(SystemState::instance());
        auto& od = SystemState::instance().data().oneDrive;
        od.transferState = ONEDRIVE_PRE_SLEEP_SYNC;
        od.lastError[0]  = '\0';
    }

    const bool ok = syncPendingFiles(timeoutMs > 0 ? timeoutMs : CloudConfig::ONEDRIVE_PRE_SLEEP_TIMEOUT_MS);
    return ok;
}

bool OneDriveSyncService::syncPendingFiles(uint32_t maxDurationMs) {
    const uint32_t startMs    = millis();
    const bool     hasDeadline = (maxDurationMs > 0);
    const uint32_t deadlineMs = startMs + maxDurationMs;

    if (!_isBound) {
        setTransferError("尚未绑定 OneDrive 账号");
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        setTransferError("Wi-Fi 未连接");
        return false;
    }
    if (_sd == nullptr) {
        setTransferError("TF 卡存储未就绪");
        return false;
    }
    if (!ensureValidAccessToken()) {
        setTransferError("OneDrive 令牌刷新失败");
        return false;
    }

    std::vector<PendingUploadItem> pending;
    uint16_t syncedCount = 0;
    if (!collectPendingUploads(pending, syncedCount)) {
        setTransferError("扫描 TF 卡轨迹失败");
        return false;
    }

    {
        StateGuard lock(SystemState::instance());
        auto& od = SystemState::instance().data().oneDrive;
        od.pendingFiles     = static_cast<uint16_t>(pending.size());
        od.syncedTotalFiles = syncedCount;
        if (pending.empty()) {
            od.transferState       = ONEDRIVE_SYNC_OK;
            od.currentFilePercent  = 100;
            od.currentFileName[0]  = '\0';
            const uint32_t nowUtc  = currentEpochUtcOrZero();
            if (nowUtc > 0) {
                od.lastSyncUtc = nowUtc;
            }
            return true;
        }
        if (od.transferState != ONEDRIVE_PRE_SLEEP_SYNC) {
            od.transferState = ONEDRIVE_SYNCING;
        }
        od.lastError[0] = '\0';
    }

    bool allSucceeded = true;
    for (size_t i = 0; i < pending.size(); ++i) {
        if (hasDeadline && static_cast<int32_t>(deadlineMs - millis()) <= 1500) {
            Serial.println("[ONEDRIVE] Sync slice/pre-sleep deadline reached cleanly");
            break;
        }

        const PendingUploadItem& item = pending[i];
        {
            StateGuard lock(SystemState::instance());
            auto& od = SystemState::instance().data().oneDrive;
            strncpy(od.currentFileName, item.relPath.c_str(), sizeof(od.currentFileName) - 1);
            od.currentFileName[sizeof(od.currentFileName) - 1] = '\0';
            od.currentFilePercent = 0;
        }

        if (uploadSingleFile(item, deadlineMs, hasDeadline)) {
            appendLedgerRecord(item.relPath, item.fileSize);
            syncedCount++;
            StateGuard lock(SystemState::instance());
            auto& od = SystemState::instance().data().oneDrive;
            if (od.pendingFiles > 0) {
                od.pendingFiles--;
            }
            od.syncedTotalFiles   = syncedCount;
            od.currentFilePercent = 100;
        } else {
            allSucceeded = false;
            break;
        }
    }

    {
        StateGuard lock(SystemState::instance());
        auto& od = SystemState::instance().data().oneDrive;
        od.currentFileName[0] = '\0';
        if (allSucceeded) {
            od.transferState = ONEDRIVE_SYNC_OK;
            const uint32_t nowUtc = currentEpochUtcOrZero();
            if (nowUtc > 0) {
                od.lastSyncUtc = nowUtc;
            }
        } else if (od.transferState != ONEDRIVE_SYNC_ERR) {
            od.transferState = ONEDRIVE_SYNC_ERR;
        }
    }
    return allSucceeded;
}

bool OneDriveSyncService::readFileSliceSafe(const char* fullPath, uint64_t offset,
                                            uint8_t* dstBuf, size_t len) const {
    if (_sd == nullptr || fullPath == nullptr || dstBuf == nullptr || len == 0) {
        return false;
    }
    if (!lockSd(2500)) {
        return false;
    }
    bool ok = false;
    FsFile f;
    if (f.open(_sd, fullPath, O_RDONLY)) {
        if (f.seekSet(offset)) {
            const int r = f.read(dstBuf, len);
            ok = (r == static_cast<int>(len));
        }
        f.close();
    }
    unlockSd();
    return ok;
}

bool OneDriveSyncService::uploadSingleFile(const PendingUploadItem& item,
                                           uint32_t deadlineMs, bool hasDeadline) {
    if (item.fileSize == 0) {
        return true;
    }
    if (item.fileSize < DIRECT_PUT_LIMIT_BYTES) {
        return uploadSmallFileDirectPut(item);
    }
    return uploadLargeFileSession(item, deadlineMs, hasDeadline);
}

bool OneDriveSyncService::uploadSmallFileDirectPut(const PendingUploadItem& item) {
    const size_t sz = static_cast<size_t>(item.fileSize);
    uint8_t* buf = static_cast<uint8_t*>(
        heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buf == nullptr) {
        // Fallback to upload session if contiguous buffer couldn't be allocated
        return uploadLargeFileSession(item, 0, false);
    }

    if (!readFileSliceSafe(item.fullPath.c_str(), 0, buf, sz)) {
        free(buf);
        setTransferError("读取本地轨迹文件失败");
        return false;
    }

    String url = "https://";
    url += CloudConfig::ONEDRIVE_GRAPH_HOST;
    url += "/v1.0/me/drive/root:";
    url += CloudConfig::ONEDRIVE_REMOTE_ROOT;
    url += "/";
    url += urlEncodeGraphPath(item.relPath);
    url += ":/content";

    bool uploaded = false;
    for (int attempt = 0; attempt < 2 && !uploaded; ++attempt) {
        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(UPLOAD_HTTP_TIMEOUT_MS / 1000U);

        HTTPClient http;
        http.setConnectTimeout(8000);
        http.setTimeout(UPLOAD_HTTP_TIMEOUT_MS);
        http.setAcceptEncoding("identity");

        if (http.begin(client, url)) {
            http.addHeader("Authorization", String("Bearer ") + _accessToken);
            http.addHeader("Content-Type", "application/octet-stream");
            const int code = http.PUT(buf, sz);
            http.end();

            if (code == HTTP_CODE_OK || code == HTTP_CODE_CREATED) {
                uploaded = true;
                break;
            }
            if (code == HTTP_CODE_UNAUTHORIZED && attempt == 0) {
                if (refreshAccessToken()) {
                    continue;
                }
            }
            Serial.printf("[ONEDRIVE] Small PUT %s failed HTTP %d\n", item.relPath.c_str(), code);
        }
    }

    free(buf);
    if (!uploaded) {
        setTransferError("OneDrive 直传文件失败");
    }
    return uploaded;
}

bool OneDriveSyncService::createGraphUploadSession(const String& relPath, String& outUploadUrl) {
    outUploadUrl = "";

    String url = "https://";
    url += CloudConfig::ONEDRIVE_GRAPH_HOST;
    url += "/v1.0/me/drive/root:";
    url += CloudConfig::ONEDRIVE_REMOTE_ROOT;
    url += "/";
    url += urlEncodeGraphPath(relPath);
    url += ":/createUploadSession";

    static constexpr const char* kSessionBody =
        "{\"item\":{\"@microsoft.graph.conflictBehavior\":\"replace\"}}";

    for (int attempt = 0; attempt < 2; ++attempt) {
        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(AUTH_HTTP_TIMEOUT_MS / 1000U);

        HTTPClient http;
        http.setConnectTimeout(8000);
        http.setTimeout(AUTH_HTTP_TIMEOUT_MS);
        http.setAcceptEncoding("identity");

        if (!http.begin(client, url)) {
            return false;
        }
        http.addHeader("Authorization", String("Bearer ") + _accessToken);
        http.addHeader("Content-Type", "application/json");
        const int code = http.POST(String(kSessionBody));
        const String resp = http.getString();
        http.end();

        if (code == HTTP_CODE_UNAUTHORIZED && attempt == 0) {
            if (refreshAccessToken()) {
                continue;
            }
            return false;
        }

        if (code == HTTP_CODE_OK || code == HTTP_CODE_CREATED) {
            JsonDocument doc;
            if (!deserializeJson(doc, resp)) {
                const char* u = doc["uploadUrl"] | "";
                if (u[0] != '\0') {
                    outUploadUrl = u;
                    return true;
                }
            }
        }
        Serial.printf("[ONEDRIVE] createUploadSession HTTP %d\n", code);
        break;
    }
    return false;
}

bool OneDriveSyncService::uploadLargeFileSession(const PendingUploadItem& item,
                                                 uint32_t deadlineMs, bool hasDeadline) {
    String uploadUrl;
    if (!createGraphUploadSession(item.relPath, uploadUrl) || uploadUrl.isEmpty()) {
        setTransferError("创建 OneDrive 分片会话失败");
        return false;
    }

    // Ensure 327,680-byte (320 KiB) PSRAM buffer is allocated (#3 §3.2 Iron Rule #2)
    bool tempAlloc = false;
    uint8_t* chunkBuf = _psramChunkBuf;
    if (chunkBuf == nullptr) {
        chunkBuf = static_cast<uint8_t*>(
            heap_caps_malloc(SESSION_CHUNK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (chunkBuf == nullptr) {
            chunkBuf = static_cast<uint8_t*>(malloc(SESSION_CHUNK_BYTES));
        }
        tempAlloc = true;
    }
    if (chunkBuf == nullptr) {
        setTransferError("PSRAM 分片缓冲区分配失败");
        return false;
    }

    const uint64_t totalSize = item.fileSize;
    uint64_t offset = 0;
    bool ok = true;

    while (offset < totalSize) {
        if (hasDeadline && static_cast<int32_t>(deadlineMs - millis()) <= 1000) {
            ok = false;
            setTransferError("休眠前预同步分片超时");
            break;
        }

        // IRON RULE #2: Every non-final chunk is strictly 327,680 Bytes (1 * 320 KiB)!
        const uint64_t rem = totalSize - offset;
        const size_t chunkLen = (rem > SESSION_CHUNK_BYTES)
                                    ? SESSION_CHUNK_BYTES
                                    : static_cast<size_t>(rem);
        const bool isFinalChunk = ((offset + chunkLen) == totalSize);

        // Read chunk into PSRAM under short SD lock, then release SD lock immediately
        if (!readFileSliceSafe(item.fullPath.c_str(), offset, chunkBuf, chunkLen)) {
            ok = false;
            setTransferError("读取大文件分片失败");
            break;
        }

        // IRON RULE #3: 0-based inclusive Content-Range: bytes start-end/total
        char contentRange[96] = {};
        snprintf(contentRange, sizeof(contentRange), "bytes %llu-%llu/%llu",
                 static_cast<unsigned long long>(offset),
                 static_cast<unsigned long long>(offset + chunkLen - 1ULL),
                 static_cast<unsigned long long>(totalSize));

        bool chunkUploaded = false;
        for (int retry = 0; retry < 3 && !chunkUploaded; ++retry) {
            WiFiClientSecure client;
            client.setInsecure();
            client.setTimeout(UPLOAD_HTTP_TIMEOUT_MS / 1000U);

            HTTPClient http;
            http.setConnectTimeout(8000);
            http.setTimeout(UPLOAD_HTTP_TIMEOUT_MS);
            http.setAcceptEncoding("identity");

            if (http.begin(client, uploadUrl)) {
                // IRON RULE #1: NEVER include Authorization header on pre-signed uploadUrl PUT!
                http.addHeader("Content-Range", contentRange);
                http.addHeader("Content-Type", "application/octet-stream");
                // HTTPClient::PUT(chunkBuf, chunkLen) sets Content-Length: <chunkLen> automatically
                const int code = http.PUT(chunkBuf, chunkLen);
                http.end();

                if (isFinalChunk) {
                    if (code == HTTP_CODE_OK || code == HTTP_CODE_CREATED) {
                        chunkUploaded = true;
                        break;
                    }
                } else {
                    if (code == HTTP_CODE_ACCEPTED || code == HTTP_CODE_OK || code == HTTP_CODE_CREATED) {
                        chunkUploaded = true;
                        break;
                    }
                }
                Serial.printf("[ONEDRIVE] Chunk PUT (%s) HTTP %d (retry %d)\n",
                              contentRange, code, retry);
            }
            vTaskDelay(pdMS_TO_TICKS(250));
        }

        if (!chunkUploaded) {
            ok = false;
            setTransferError("分片上传中断");
            break;
        }

        offset += chunkLen;
        const uint8_t pct = static_cast<uint8_t>((offset * 100ULL) / totalSize);
        {
            StateGuard lock(SystemState::instance());
            SystemState::instance().data().oneDrive.currentFilePercent = pct;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (tempAlloc && chunkBuf != nullptr) {
        free(chunkBuf);
    }
    return ok;
}

void OneDriveSyncService::setLastErrorState(OneDriveBindState bindSt, const char* errMsg) {
    StateGuard lock(SystemState::instance());
    auto& od = SystemState::instance().data().oneDrive;
    od.bindState = bindSt;
    if (errMsg != nullptr) {
        strncpy(od.lastError, errMsg, sizeof(od.lastError) - 1);
        od.lastError[sizeof(od.lastError) - 1] = '\0';
    }
}

void OneDriveSyncService::setTransferError(const char* errMsg) {
    StateGuard lock(SystemState::instance());
    auto& od = SystemState::instance().data().oneDrive;
    od.transferState = ONEDRIVE_SYNC_ERR;
    if (errMsg != nullptr) {
        strncpy(od.lastError, errMsg, sizeof(od.lastError) - 1);
        od.lastError[sizeof(od.lastError) - 1] = '\0';
    }
}

String OneDriveSyncService::urlEncodeFormComponent(const String& str) {
    static constexpr const char* kHex = "0123456789ABCDEF";
    String encoded;
    encoded.reserve(str.length() + 16);
    for (size_t i = 0; i < str.length(); ++i) {
        const uint8_t c = static_cast<uint8_t>(str[i]);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += static_cast<char>(c);
        } else {
            encoded += '%';
            encoded += kHex[(c >> 4) & 0x0F];
            encoded += kHex[c & 0x0F];
        }
    }
    return encoded;
}

String OneDriveSyncService::urlEncodeGraphPath(const String& relPath) {
    static constexpr const char* kHex = "0123456789ABCDEF";
    String encoded;
    encoded.reserve(relPath.length() + 16);
    for (size_t i = 0; i < relPath.length(); ++i) {
        const uint8_t c = static_cast<uint8_t>(relPath[i]);
        // Keep '/' unencoded as Graph item path separator
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            encoded += static_cast<char>(c);
        } else {
            encoded += '%';
            encoded += kHex[(c >> 4) & 0x0F];
            encoded += kHex[c & 0x0F];
        }
    }
    return encoded;
}
