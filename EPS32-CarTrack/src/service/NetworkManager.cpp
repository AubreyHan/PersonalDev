#include "service/NetworkManager.h"
#include "service/TrackLogger.h"
#include "service/TunnelService.h"
#include "service/OneDriveSyncService.h"
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <M5Unified.h>

void AppNetworkManager::init() {
    Serial.println("[WIFI] Initializing ESP32-C6 Wi-Fi co-processor via SDIO...");

    // Hardware reset pulse to ESP32-C6 (SDIO2_RST: GPIO 15)
    pinMode(15, OUTPUT);
    digitalWrite(15, LOW);
    delay(30);
    digitalWrite(15, HIGH);
    pinMode(15, INPUT_PULLUP);
    delay(60);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);

    loadSavedNetworks();
    setupHttpRoutes();
    tryAutoConnectMultiSsid();
}

void AppNetworkManager::loadSavedNetworks() {
    if (_netMutex) xSemaphoreTakeRecursive(_netMutex, portMAX_DELAY);
    Preferences prefs;
    _savedCount = 0;
    memset(_saved, 0, sizeof(_saved));
    if (prefs.begin("m5track", true)) {
        uint8_t cnt = prefs.getUChar("wifi_count", 0);
        if (cnt > 5) cnt = 5;
        for (uint8_t i = 0; i < cnt; i++) {
            char kSsid[12], kPwd[12];
            snprintf(kSsid, sizeof(kSsid), "ssid_%u", i);
            snprintf(kPwd,  sizeof(kPwd),  "pwd_%u",  i);
            String s = prefs.getString(kSsid, "");
            String p = prefs.getString(kPwd,  "");
            if (s.length() > 0) {
                strncpy(_saved[_savedCount].ssid,     s.c_str(), sizeof(_saved[_savedCount].ssid) - 1);
                strncpy(_saved[_savedCount].password, p.c_str(), sizeof(_saved[_savedCount].password) - 1);
                _savedCount++;
            }
        }
        prefs.end();
    }
    uint8_t sc = _savedCount;
    if (_netMutex) xSemaphoreGiveRecursive(_netMutex);

    SystemState& state = SystemState::instance();
    state.lock();
    state.getContextUnsafe().wifi.savedNetworkCount = sc;
    state.unlock();
}

bool AppNetworkManager::saveNetworkCredential(const char* ssid, const char* pwd) {
    if (!ssid || strlen(ssid) == 0) return false;
    if (_netMutex) xSemaphoreTakeRecursive(_netMutex, portMAX_DELAY);
    loadSavedNetworks();

    int existingIdx = -1;
    for (uint8_t i = 0; i < _savedCount; i++) {
        if (strcmp(_saved[i].ssid, ssid) == 0) {
            existingIdx = i;
            break;
        }
    }

    if (existingIdx >= 0) {
        memset(_saved[existingIdx].password, 0, sizeof(_saved[existingIdx].password));
        strncpy(_saved[existingIdx].password, pwd ? pwd : "", sizeof(_saved[existingIdx].password) - 1);
    } else if (_savedCount < 5) {
        memset(&_saved[_savedCount], 0, sizeof(SavedWifiCredential));
        strncpy(_saved[_savedCount].ssid,     ssid,           sizeof(_saved[_savedCount].ssid) - 1);
        strncpy(_saved[_savedCount].password, pwd ? pwd : "", sizeof(_saved[_savedCount].password) - 1);
        _savedCount++;
    } else {
        for (int i = 0; i < 4; i++) _saved[i] = _saved[i + 1];
        memset(&_saved[4], 0, sizeof(SavedWifiCredential));
        strncpy(_saved[4].ssid,     ssid,           sizeof(_saved[4].ssid) - 1);
        strncpy(_saved[4].password, pwd ? pwd : "", sizeof(_saved[4].password) - 1);
    }

    Preferences prefs;
    if (prefs.begin("m5track", false)) {
        prefs.putUChar("wifi_count", _savedCount);
        for (uint8_t i = 0; i < _savedCount; i++) {
            char kSsid[12], kPwd[12];
            snprintf(kSsid, sizeof(kSsid), "ssid_%u", i);
            snprintf(kPwd,  sizeof(kPwd),  "pwd_%u",  i);
            prefs.putString(kSsid, _saved[i].ssid);
            prefs.putString(kPwd,  _saved[i].password);
        }
        prefs.end();
    }
    uint8_t sc = _savedCount;
    if (_netMutex) xSemaphoreGiveRecursive(_netMutex);

    SystemState& state = SystemState::instance();
    state.lock();
    state.getContextUnsafe().wifi.savedNetworkCount = sc;
    state.unlock();

    return true;
}

bool AppNetworkManager::deleteNetworkCredential(const char* ssid) {
    if (!ssid) return false;
    if (_netMutex) xSemaphoreTakeRecursive(_netMutex, portMAX_DELAY);
    loadSavedNetworks();
    uint8_t newCount = 0;
    SavedWifiCredential temp[5];
    memset(temp, 0, sizeof(temp));
    for (uint8_t i = 0; i < _savedCount; i++) {
        if (strcmp(_saved[i].ssid, ssid) != 0) {
            temp[newCount++] = _saved[i];
        }
    }
    _savedCount = newCount;
    memset(_saved, 0, sizeof(_saved));
    for (uint8_t i = 0; i < _savedCount; i++) _saved[i] = temp[i];

    Preferences prefs;
    if (prefs.begin("m5track", false)) {
        prefs.putUChar("wifi_count", _savedCount);
        for (uint8_t i = 0; i < _savedCount; i++) {
            char kSsid[12], kPwd[12];
            snprintf(kSsid, sizeof(kSsid), "ssid_%u", i);
            snprintf(kPwd,  sizeof(kPwd),  "pwd_%u",  i);
            prefs.putString(kSsid, _saved[i].ssid);
            prefs.putString(kPwd,  _saved[i].password);
        }
        prefs.end();
    }
    uint8_t sc = _savedCount;
    if (_netMutex) xSemaphoreGiveRecursive(_netMutex);

    SystemState& state = SystemState::instance();
    state.lock();
    state.getContextUnsafe().wifi.savedNetworkCount = sc;
    state.unlock();
    return true;
}

uint8_t AppNetworkManager::getSavedNetworks(SavedWifiCredential* outArr, uint8_t maxCount) {
    if (_netMutex) xSemaphoreTakeRecursive(_netMutex, portMAX_DELAY);
    uint8_t n = (_savedCount < maxCount) ? _savedCount : maxCount;
    for (uint8_t i = 0; i < n; i++) outArr[i] = _saved[i];
    if (_netMutex) xSemaphoreGiveRecursive(_netMutex);
    return n;
}

void AppNetworkManager::tryAutoConnectMultiSsid() {
    if (_portalRunning) return;
    if (WiFi.status() == WL_CONNECTED) return;

    if (_netMutex) xSemaphoreTakeRecursive(_netMutex, portMAX_DELAY);
    if (_savedCount == 0) {
        if (_netMutex) xSemaphoreGiveRecursive(_netMutex);
        return;
    }
    uint8_t idx = _connectAttemptIdx % _savedCount;
    _connectAttemptIdx++;
    char targetSsid[36], targetPwd[68];
    strncpy(targetSsid, _saved[idx].ssid, sizeof(targetSsid));
    strncpy(targetPwd,  _saved[idx].password, sizeof(targetPwd));
    if (_netMutex) xSemaphoreGiveRecursive(_netMutex);

    Serial.printf("[WIFI] Auto-connecting to saved network [%s]...\n", targetSsid);
    WiFi.begin(targetSsid, targetPwd);
}

void AppNetworkManager::doStartSetupPortal() {
    doStopPortals();
    vTaskDelay(pdMS_TO_TICKS(50));
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    vTaskDelay(pdMS_TO_TICKS(50));
    IPAddress apIP(192, 168, 4, 1);
    IPAddress netmask(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, apIP, netmask);
    vTaskDelay(pdMS_TO_TICKS(50));

    bool apOk = WiFi.softAP("M5Track-Setup", "", 1, 0, 4);
    if (!apOk) {
        apOk = WiFi.softAP("M5Track-Setup");
    }
    Serial.printf("[WIFI] Started Captive Portal SoftAP: M5Track-Setup (ok=%d, IP=%s, ch=%d, mac=%s)\n",
                  (int)apOk, WiFi.softAPIP().toString().c_str(), (int)WiFi.channel(), WiFi.softAPmacAddress().c_str());

    if (apOk) {
        _dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        _dnsServer.start(53, "*", apIP);
        _server.begin();
        _portalRunning = true;
        _isShareMode   = false;
        _portalConnectState = PORTAL_STATE_IDLE;
        _portalStartTimeMs  = millis();
        // Trigger background scan so nearby networks are ready
        WiFi.scanNetworks(true);
    } else {
        Serial.println("[WIFI] Warning: SoftAP start failed, maintaining STA.");
        WiFi.mode(WIFI_STA);
    }

    SystemState& state = SystemState::instance();
    state.lock();
    state.getContextUnsafe().wifi.apSetupActive = apOk;
    state.getContextUnsafe().wifi.apShareActive = false;
    state.unlock();
}

void AppNetworkManager::doStartSharePortal() {
    doStopPortals();
    vTaskDelay(pdMS_TO_TICKS(50));
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    vTaskDelay(pdMS_TO_TICKS(50));
    IPAddress apIP(192, 168, 4, 1);
    IPAddress netmask(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, apIP, netmask);
    vTaskDelay(pdMS_TO_TICKS(50));

    bool apOk = WiFi.softAP("M5Track-Share", "", 1, 0, 4);
    if (!apOk) {
        apOk = WiFi.softAP("M5Track-Share");
    }
    Serial.printf("[WIFI] Started Track Share SoftAP: M5Track-Share (ok=%d, IP=%s, ch=%d)\n",
                  (int)apOk, WiFi.softAPIP().toString().c_str(), (int)WiFi.channel());

    if (apOk) {
        _dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        _dnsServer.start(53, "*", apIP);
        _server.begin();
        _portalRunning = true;
        _isShareMode   = true;
    } else {
        Serial.println("[WIFI] Warning: Track Share SoftAP failed, maintaining STA.");
        WiFi.mode(WIFI_STA);
    }

    SystemState& state = SystemState::instance();
    state.lock();
    state.getContextUnsafe().wifi.apShareActive = apOk;
    state.getContextUnsafe().wifi.apSetupActive = false;
    state.unlock();
}

void AppNetworkManager::doStopPortals() {
    if (_portalRunning) {
        _dnsServer.stop();
        _server.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        _portalRunning = false;
        _isShareMode   = false;
        _portalConnectState = PORTAL_STATE_IDLE;

        SystemState& state = SystemState::instance();
        state.lock();
        state.getContextUnsafe().wifi.apShareActive = false;
        state.getContextUnsafe().wifi.apSetupActive = false;
        state.unlock();
    }
}

void AppNetworkManager::handleCaptiveRedirect() {
    String host = _server.hostHeader();
    if (host != "192.168.4.1" && host != "192.168.4.1:80" && host != "m5track.local") {
        _server.sendHeader("Location", "http://192.168.4.1/setup", true);
        _server.send(302, "text/plain", "");
        return;
    }
    handleRootPage();
}

void AppNetworkManager::handleRootPage() {
    if (_isShareMode) {
        handleShareTracksPage();
    } else {
        handleWifiSetupPage();
    }
}

void AppNetworkManager::handleWifiSetupPage() {
    // Collect scanned networks
    int n = WiFi.scanComplete();
    if (n < 0) {
        // Trigger asynchronous scan
        WiFi.scanNetworks(true);
        n = 0;
    }

    struct ScannedAp {
        String ssid;
        int32_t rssi;
        bool secure;
    };
    std::vector<ScannedAp> aps;
    for (int i = 0; i < n; i++) {
        String s = WiFi.SSID(i);
        if (s.length() == 0) continue;
        int32_t r = WiFi.RSSI(i);
        bool sec = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        bool found = false;
        for (auto& ap : aps) {
            if (ap.ssid == s) {
                if (r > ap.rssi) ap.rssi = r;
                found = true;
                break;
            }
        }
        if (!found && aps.size() < 12) {
            aps.push_back({s, r, sec});
        }
    }

    // Sort by signal strength descending
    std::sort(aps.begin(), aps.end(), [](const ScannedAp& a, const ScannedAp& b) {
        return a.rssi > b.rssi;
    });

    String scanListHtml = "";
    if (aps.empty()) {
        scanListHtml = "<div class='empty-tip'>Scanning surrounding networks... <a href='/setup' style='color:#00ffaa;'>Refresh</a></div>";
    } else {
        for (const auto& ap : aps) {
            int q = 2 * (ap.rssi + 100);
            if (q < 0) q = 0;
            if (q > 100) q = 100;
            int bars = (q >= 75) ? 4 : ((q >= 50) ? 3 : ((q >= 25) ? 2 : 1));

            String barSpan = "<span class='bars'>";
            for (int b = 1; b <= 4; b++) {
                barSpan += "<span class='bar" + String(b <= bars ? " active" : "") + "'></span>";
            }
            barSpan += "</span>";

            scanListHtml += "<div class='ap-row' onclick=\"selSsid('" + ap.ssid + "')\">";
            scanListHtml += "<div class='ap-name'>" + ap.ssid + (ap.secure ? " <span class='lock'>🔒</span>" : "") + "</div>";
            scanListHtml += "<div class='ap-rssi'>" + barSpan + "<span class='rssi-txt'>" + String(ap.rssi) + "dBm</span></div>";
            scanListHtml += "</div>";
        }
    }

    // Render Saved Networks
    String savedHtml = "";
    if (_savedCount > 0) {
        savedHtml += "<div class='saved-box'><div class='sub-title'>Saved Profiles (" + String(_savedCount) + "/5)</div>";
        for (uint8_t i = 0; i < _savedCount; i++) {
            savedHtml += "<div class='saved-row'>";
            savedHtml += "<span onclick=\"selSsid('" + String(_saved[i].ssid) + "')\" style='cursor:pointer;flex:1;'>★ " + String(_saved[i].ssid) + "</span>";
            savedHtml += "<button type='button' class='btn-del' onclick=\"delSsid('" + String(_saved[i].ssid) + "')\">Delete</button>";
            savedHtml += "</div>";
        }
        savedHtml += "</div>";
    }

    String page = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>M5Track-Tab5 Wi-Fi Setup</title>
<style>
body{background:#0d1117;color:#e6edf3;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;margin:0;padding:16px;display:flex;justify-content:center;}
.wrap{max-width:440px;width:100%;}
.header{text-align:center;padding:12px 0 20px;}
.logo{font-size:24px;font-weight:bold;color:#00ffaa;letter-spacing:1px;}
.subtitle{color:#8b949e;font-size:14px;margin-top:4px;}
.card{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:20px;margin-bottom:16px;box-shadow:0 4px 12px rgba(0,0,0,0.4);}
.sub-title{font-size:15px;font-weight:600;color:#00ffaa;margin-bottom:12px;display:flex;justify-content:space-between;align-items:center;}
.ap-row{display:flex;justify-content:space-between;align-items:center;padding:12px;border-bottom:1px solid #21262d;cursor:pointer;border-radius:8px;transition:background 0.15s;}
.ap-row:active{background:#21262d;}
.ap-name{font-size:15px;font-weight:500;color:#fff;}
.lock{font-size:12px;}
.ap-rssi{display:flex;align-items:center;gap:6px;}
.rssi-txt{font-size:12px;color:#8b949e;}
.bars{display:inline-flex;align-items:flex-end;gap:2px;height:14px;}
.bar{width:3px;background:#30363d;border-radius:1px;}
.bar:nth-child(1){height:4px;}
.bar:nth-child(2){height:7px;}
.bar:nth-child(3){height:10px;}
.bar:nth-child(4){height:14px;}
.bar.active{background:#00ffaa;}
.form-group{margin-bottom:16px;}
label{display:block;font-size:13px;font-weight:600;color:#8b949e;margin-bottom:6px;}
input[type="text"],input[type="password"]{width:100%;height:44px;background:#0d1117;border:1px solid #30363d;border-radius:8px;padding:0 12px;color:#fff;font-size:16px;box-sizing:border-box;outline:none;}
input:focus{border-color:#00ffaa;}
.pwd-wrap{position:relative;}
.btn-eye{position:absolute;right:8px;top:8px;height:28px;background:none;border:none;cursor:pointer;font-size:16px;padding:0 8px;}
.btn-primary{width:100%;height:48px;background:#00ffaa;color:#000;font-size:16px;font-weight:bold;border:none;border-radius:8px;cursor:pointer;transition:background 0.2s;display:flex;align-items:center;justify-content:center;}
.btn-primary:active{background:#00cc88;}
.btn-rescan{background:#21262d;color:#00ffaa;border:1px solid #30363d;padding:4px 10px;border-radius:6px;font-size:12px;text-decoration:none;font-weight:600;}
.empty-tip{padding:16px;text-align:center;color:#8b949e;font-size:14px;}
.saved-box{margin-top:16px;border-top:1px solid #21262d;padding-top:12px;}
.saved-row{display:flex;justify-content:space-between;align-items:center;padding:8px 0;color:#c9d1d9;font-size:14px;}
.btn-del{background:#21262d;color:#f85149;border:1px solid #30363d;padding:4px 8px;border-radius:4px;font-size:12px;cursor:pointer;}
</style></head><body>
<div class="wrap">
  <div class="header">
    <div class="logo">🚗 M5Track-Tab5</div>
    <div class="subtitle">Vehicle GNSS Tracker & HUD Provisioning</div>
  </div>

  <div class="card">
    <div class="sub-title">
      <span>Nearby Networks</span>
      <a class="btn-rescan" href="/setup">↻ Rescan</a>
    </div>
    )rawliteral" + scanListHtml + R"rawliteral(
  </div>

  <div class="card">
    <div class="sub-title">Connect Wi-Fi</div>
    <form action="/wifisave" method="POST">
      <div class="form-group">
        <label>Network Name (SSID)</label>
        <input type="text" id="ssid" name="ssid" placeholder="Tap network above or type SSID" required>
      </div>
      <div class="form-group">
        <label>Password</label>
        <div class="pwd-wrap">
          <input type="password" id="pwd" name="pwd" placeholder="Leave blank if open Wi-Fi">
          <button type="button" class="btn-eye" onclick="togglePwd()">👁️</button>
        </div>
      </div>
      <button type="submit" class="btn-primary">Connect & Save</button>
    </form>
    )rawliteral" + savedHtml + R"rawliteral(
  </div>
</div>
<script>
function selSsid(s){
  document.getElementById('ssid').value=s;
  var p=document.getElementById('pwd');
  p.focus();
}
function togglePwd(){
  var p=document.getElementById('pwd');
  p.type = (p.type==='password') ? 'text' : 'password';
}
function delSsid(s){
  if(confirm('Delete saved Wi-Fi: '+s+'?')){
    fetch('/api/delete',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:s})})
    .then(function(){location.reload();});
  }
}
</script>
</body></html>
)rawliteral";

    _server.send(200, "text/html", page);
}

void AppNetworkManager::handleWifiSave() {
    String ssid = _server.hasArg("ssid") ? _server.arg("ssid") : _server.arg("s");
    String pwd  = _server.hasArg("pwd")  ? _server.arg("pwd")  : _server.arg("p");
    ssid.trim();
    pwd.trim();

    if (ssid.length() == 0) {
        _server.send(400, "text/html", "<html><body><h3>SSID cannot be empty</h3><a href='/setup'>Back</a></body></html>");
        return;
    }

    saveNetworkCredential(ssid.c_str(), pwd.c_str());

    _portalConnectState   = PORTAL_STATE_CONNECTING;
    _portalConnectStartMs = millis();
    strncpy(_portalTargetSsid, ssid.c_str(), sizeof(_portalTargetSsid) - 1);
    memset(_portalErrMsg, 0, sizeof(_portalErrMsg));

    Serial.printf("[WIFI] Connecting to requested SSID: [%s]...\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pwd.c_str());

    String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="2; url=/status">
<title>Connecting...</title>
<style>
body{background:#0d1117;color:#e6edf3;font-family:-apple-system,BlinkMacSystemFont,sans-serif;margin:0;padding:24px;display:flex;justify-content:center;}
.card{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:24px;max-width:400px;width:100%;text-align:center;}
.spinner{width:48px;height:48px;border:4px solid #30363d;border-top-color:#00ffaa;border-radius:50%;animation:spin 1s linear infinite;margin:24px auto;}
@keyframes spin{to{transform:rotate(360deg);}}
h2{color:#00ffaa;margin:0 0 12px 0;}
p{color:#8b949e;line-height:1.5;}
</style></head><body>
<div class="card">
  <h2>Connecting to Network</h2>
  <div class="spinner"></div>
  <p>Connecting to <b>)rawliteral" + ssid + R"rawliteral(</b>...</p>
  <p style="font-size:14px;">Please wait while the tracker connects. You will be redirected automatically.</p>
</div>
</body></html>
)rawliteral";
    _server.send(200, "text/html", html);
}

void AppNetworkManager::handleStatusPage() {
    String html;
    if (_portalConnectState == PORTAL_STATE_SUCCESS) {
        html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Connected</title>
<style>
body{background:#0d1117;color:#e6edf3;font-family:-apple-system,BlinkMacSystemFont,sans-serif;margin:0;padding:24px;display:flex;justify-content:center;}
.card{background:#161b22;border:1px solid #238636;border-radius:12px;padding:24px;max-width:400px;width:100%;text-align:center;}
.icon{font-size:48px;margin-bottom:12px;}
h2{color:#00ffaa;margin:0 0 12px 0;}
.badge{background:#238636;color:#fff;padding:6px 12px;border-radius:6px;font-weight:bold;display:inline-block;margin:8px 0;}
</style></head><body>
<div class="card">
  <div class="icon">🎉</div>
  <h2>Connection Successful!</h2>
  <div class="badge">IP: )rawliteral" + WiFi.localIP().toString() + R"rawliteral(</div>
  <p style="color:#8b949e;">Connected to <b>)rawliteral" + String(_portalTargetSsid) + R"rawliteral(</b></p>
  <p style="color:#8b949e;font-size:14px;">Setup complete. The configuration hotspot will close automatically.</p>
</div>
</body></html>
)rawliteral";
    } else if (_portalConnectState == PORTAL_STATE_FAILED) {
        html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Connection Failed</title>
<style>
body{background:#0d1117;color:#e6edf3;font-family:-apple-system,BlinkMacSystemFont,sans-serif;margin:0;padding:24px;display:flex;justify-content:center;}
.card{background:#161b22;border:1px solid #da3633;border-radius:12px;padding:24px;max-width:400px;width:100%;text-align:center;}
.icon{font-size:48px;margin-bottom:12px;}
h2{color:#f85149;margin:0 0 12px 0;}
.btn{display:inline-block;background:#238636;color:#fff;font-weight:bold;padding:12px 24px;border-radius:8px;text-decoration:none;margin-top:16px;}
</style></head><body>
<div class="card">
  <div class="icon">⚠️</div>
  <h2>Connection Failed</h2>
  <p style="color:#8b949e;">)rawliteral" + String(_portalErrMsg[0] ? _portalErrMsg : "Could not connect to network. Verify password.") + R"rawliteral(</p>
  <a class="btn" href="/setup">Try Again</a>
</div>
</body></html>
)rawliteral";
    } else {
        uint32_t elapsed = (millis() - _portalConnectStartMs) / 1000;
        html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="2; url=/status">
<title>Connecting...</title>
<style>
body{background:#0d1117;color:#e6edf3;font-family:-apple-system,BlinkMacSystemFont,sans-serif;margin:0;padding:24px;display:flex;justify-content:center;}
.card{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:24px;max-width:400px;width:100%;text-align:center;}
.spinner{width:48px;height:48px;border:4px solid #30363d;border-top-color:#00ffaa;border-radius:50%;animation:spin 1s linear infinite;margin:24px auto;}
@keyframes spin{to{transform:rotate(360deg);}}
h2{color:#00ffaa;margin:0 0 12px 0;}
p{color:#8b949e;}
</style></head><body>
<div class="card">
  <h2>Connecting...</h2>
  <div class="spinner"></div>
  <p>Negotiating Wi-Fi handshake ()rawliteral" + String(elapsed) + R"rawliteral(s)...</p>
</div>
</body></html>
)rawliteral";
    }
    _server.send(200, "text/html", html);
}

void AppNetworkManager::handleShareTracksPage() {
    const char* html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>M5Track-Tab5 GPX Track Share</title>
<style>
body{background:#0d1117;color:#00FFAA;font-family:sans-serif;margin:16px;}
.card{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:16px;margin-bottom:12px;}
a.btn{display:inline-block;padding:10px 16px;background:#00FFAA;color:#000;font-weight:bold;text-decoration:none;border-radius:6px;margin-top:8px;}
.badge{padding:3px 8px;border-radius:4px;font-size:12px;background:#283038;color:#FFD700;}
</style></head><body>
<h2>M5Track-Tab5 High-Speed File Download</h2>
<div class='card' style='border-color:#38AEFF;'>
  <div><b>power.log</b> <span class='badge' style='background:#38AEFF;color:#000'>System Power Log</span></div>
  <div style='color:#aaa;margin-top:4px;'>Accurate Boot, Shutdown & Standby Event FIFO Log (&le; 100 KB)</div>
  <a class='btn' style='background:#38AEFF;' href='/api/download?file=power.log' download='power.log'>Download System Log</a>
</div>
<div class='card' style='border-color:#FFD700;'>
  <div><b>trips.csv</b> <span class='badge' style='background:#FFD700;color:#000'>Trip Journal CSV</span></div>
  <div style='color:#aaa;margin-top:4px;'>Historical Trip Summaries (Single, Daily, Sub-Trip Stats)</div>
  <a class='btn' style='background:#FFD700;' href='/api/download?file=trips.csv' download='trips.csv'>Download Trips Journal (CSV)</a>
</div>
<div id="list">Loading GPX tracks...</div>
<script>
fetch('/api/tracks').then(r=>r.json()).then(d=>{
  let h='';
  (d.files||[]).forEach(f=>{
    h+=`<div class='card'><div><b>${f.name}</b> <span class='badge'>${f.badge}</span></div>
    <div style='color:#aaa;margin-top:4px;'>Size: ${(f.size/1024).toFixed(1)} KB</div>
    <a class='btn' href='/api/download?file=${f.name}' download='${f.name}'>Download GPX</a></div>`;
  });
  document.getElementById('list').innerHTML=h||'<div class="card">No GPX tracks on TF Card yet.</div>';
});
</script></body></html>
)rawliteral";
    _server.send(200, "text/html", html);
}

void AppNetworkManager::setupHttpRoutes() {
    _server.on("/", HTTP_GET, [this]() { handleRootPage(); });
    _server.on("/setup", HTTP_GET, [this]() { _isShareMode = false; handleWifiSetupPage(); });
    _server.on("/wifi", HTTP_GET, [this]() { _isShareMode = false; handleWifiSetupPage(); });
    _server.on("/wifisave", HTTP_POST, [this]() { handleWifiSave(); });
    _server.on("/status", HTTP_GET, [this]() { handleStatusPage(); });
    _server.on("/tracks", HTTP_GET, [this]() { handleShareTracksPage(); });

    // OS Probe endpoints: return 302 redirect so OS triggers captive portal
    auto redirectCb = [this]() {
        _server.sendHeader("Location", "http://192.168.4.1/setup", true);
        _server.send(302, "text/plain", "");
    };
    _server.on("/hotspot-detect.html", HTTP_GET, redirectCb);
    _server.on("/canonical.html", HTTP_GET, redirectCb);
    _server.on("/generate_204", HTTP_GET, redirectCb);
    _server.on("/gen_204", HTTP_GET, redirectCb);
    _server.on("/ncsi.txt", HTTP_GET, redirectCb);
    _server.on("/connecttest.txt", HTTP_GET, redirectCb);
    _server.on("/success.txt", HTTP_GET, redirectCb);
    // Enhanced Apple / Android / Xiaomi / Huawei CNA probe endpoints
    _server.on("/library/test/success.html", HTTP_GET, redirectCb);
    _server.on("/mobile/status.go", HTTP_GET, redirectCb);
    _server.on("/check_network_status.txt", HTTP_GET, redirectCb);
    _server.on("/kindle-wifi/wifiredirect.html", HTTP_GET, redirectCb);
    _server.on("/wpad.dat", HTTP_GET, redirectCb);
    _server.onNotFound([this]() { handleCaptiveRedirect(); });

    _server.on("/api/scan", HTTP_GET, [this]() {
        int n = WiFi.scanComplete();
        if (n < 0) {
            WiFi.scanNetworks(true);
            n = 0;
        }
        JsonDocument doc;
        JsonArray arr = doc["aps"].to<JsonArray>();
        for (int i = 0; i < n && i < 15; i++) {
            JsonObject obj = arr.add<JsonObject>();
            obj["ssid"] = WiFi.SSID(i);
            obj["rssi"] = WiFi.RSSI(i);
            obj["auth"] = (int)WiFi.encryptionType(i);
        }
        String out;
        serializeJson(doc, out);
        _server.send(200, "application/json", out);
    });

    _server.on("/api/saved", HTTP_GET, [this]() {
        JsonDocument doc;
        JsonArray arr = doc["saved"].to<JsonArray>();
        for (uint8_t i = 0; i < _savedCount; i++) {
            JsonObject obj = arr.add<JsonObject>();
            obj["ssid"] = _saved[i].ssid;
        }
        String out;
        serializeJson(doc, out);
        _server.send(200, "application/json", out);
    });

    _server.on("/api/connect", HTTP_POST, [this]() {
        JsonDocument doc;
        deserializeJson(doc, _server.arg("plain"));
        const char* ssid = doc["ssid"] | "";
        const char* pwd  = doc["pwd"]  | "";
        bool ok = saveNetworkCredential(ssid, pwd);
        if (ok) {
            _portalConnectState   = PORTAL_STATE_CONNECTING;
            _portalConnectStartMs = millis();
            strncpy(_portalTargetSsid, ssid, sizeof(_portalTargetSsid) - 1);
            WiFi.begin(ssid, pwd);
        }
        _server.send(200, "application/json", ok ? "{\"success\":true,\"message\":\"Connecting...\"}"
                                                 : "{\"success\":false,\"message\":\"Invalid SSID\"}");
    });

    _server.on("/api/delete", HTTP_POST, [this]() {
        JsonDocument doc;
        deserializeJson(doc, _server.arg("plain"));
        const char* ssid = doc["ssid"] | "";
        deleteNetworkCredential(ssid);
        _server.send(200, "application/json", "{\"success\":true}");
    });

    _server.on("/api/tracks", HTTP_GET, [this]() {
        TrackFileEntry entries[32];
        size_t cnt = TrackLogger::instance().listTrackFiles(entries, 32);
        JsonDocument doc;
        JsonArray arr = doc["files"].to<JsonArray>();
        for (size_t i = 0; i < cnt; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["name"]  = entries[i].name;
            o["size"]  = entries[i].sizeBytes;
            o["type"]  = entries[i].isGcj02 ? "gcj02" : "wgs84";

            const char* tripDesc = "Single";
            if (strstr(entries[i].name, "-Day-") || strstr(entries[i].name, "daily/")) {
                tripDesc = "Daily";
            } else if (strstr(entries[i].name, "-sub") || strstr(entries[i].name, "sub/") || strstr(entries[i].name, "-Manual-")) {
                tripDesc = "Sub-Trip";
            }

            char badgeStr[64];
            snprintf(badgeStr, sizeof(badgeStr), "%s [%s]",
                     entries[i].isGcj02 ? "GCJ-02" : "WGS-84", tripDesc);
            o["badge"]    = badgeStr;
            o["tripMode"] = tripDesc;
        }
        String out;
        serializeJson(doc, out);
        _server.send(200, "application/json", out);
    });

    _server.on("/api/download", HTTP_GET, [this]() {
        String fname = _server.arg("file");
        TrackLogger::instance().lockSd();
        FsFile f;
        if (!TrackLogger::instance().openFileForRead(fname.c_str(), f)) {
            TrackLogger::instance().unlockSd();
            _server.send(404, "text/plain", "Track file not found");
            return;
        }
        uint64_t fSize = f.fileSize();
        _server.sendHeader("Content-Disposition", String("attachment; filename=\"") + fname + "\"");
        _server.setContentLength(fSize);
        const char* mime = fname.endsWith(".log") ? "text/plain" : (fname.endsWith(".csv") ? "text/csv" : "application/gpx+xml");
        _server.send(200, mime, "");

        uint8_t buf[512];
        while (f.available()) {
            int n = f.read(buf, sizeof(buf));
            if (n > 0) {
                _server.client().write(buf, n);
            }
        }
        f.close();
        TrackLogger::instance().unlockSd();
    });

    _server.on("/api/sd/format", HTTP_POST, [this]() {
        bool ok = TrackLogger::instance().formatFat32();
        _server.send(200, "application/json", ok ? "{\"success\":true,\"message\":\"SD Formatted\"}"
                                                 : "{\"success\":false,\"message\":\"Format Failed\"}");
    });

    _server.on("/api/tunnel/info", HTTP_GET, [this]() {
        _server.send(200, "application/json", "{\"version\":\"2026.10\",\"count\":82,\"source\":\"LittleFS\"}");
    });

    _server.on("/api/onedrive/status", HTTP_GET, [this]() {
        SystemState& state = SystemState::instance();
        state.lock();
        auto od = state.getContextUnsafe().onedrive;
        state.unlock();

        JsonDocument doc;
        doc["isBound"]         = od.isBound;
        doc["isSyncing"]       = od.isSyncing;
        doc["authInProgress"]  = od.authInProgress;
        doc["autoSyncEnabled"] = od.autoSyncEnabled;
        doc["userCode"]        = od.userCode;
        doc["verifyUrl"]       = od.verifyUrl;
        doc["accountEmail"]    = od.accountEmail;
        doc["lastSyncEpoch"]   = od.lastSyncEpoch;
        doc["pendingCount"]    = od.pendingCount;
        doc["uploadPercent"]   = od.uploadProgressPercent;
        doc["currentFile"]     = od.currentUploadingFile;
        String out;
        serializeJson(doc, out);
        _server.send(200, "application/json", out);
    });

    _server.on("/api/onedrive/login", HTTP_POST, [this]() {
        bool ok = OneDriveSyncService::instance().startDeviceLogin();
        _server.send(200, "application/json", ok ? "{\"success\":true}" : "{\"success\":false,\"message\":\"Wi-Fi required\"}");
    });

    _server.on("/api/onedrive/sync", HTTP_POST, [this]() {
        OneDriveSyncService::instance().triggerManualSync();
        _server.send(200, "application/json", "{\"success\":true}");
    });

    _server.on("/api/onedrive/unbind", HTTP_POST, [this]() {
        OneDriveSyncService::instance().unbindAccount();
        _server.send(200, "application/json", "{\"success\":true}");
    });
}

void AppNetworkManager::update() {
    uint8_t cmd = _pendingPortalCmd;
    if (cmd != 0) {
        _pendingPortalCmd = 0;
        if (cmd == 1)      doStartSetupPortal();
        else if (cmd == 2) doStartSharePortal();
        else if (cmd == 3) doStopPortals();
    }

    if (_portalRunning) {
        for (int i = 0; i < 8; ++i) {
            _dnsServer.processNextRequest();
        }
        _server.handleClient();

        // Handle connection testing state machine
        if (_portalConnectState == PORTAL_STATE_CONNECTING) {
            if (WiFi.status() == WL_CONNECTED) {
                _portalConnectState = PORTAL_STATE_SUCCESS;
                _portalSuccessMs = millis();
                Serial.printf("[WIFI] Portal connected successfully! IP: %s\n", WiFi.localIP().toString().c_str());
            } else if (millis() - _portalConnectStartMs > 18000) {
                _portalConnectState = PORTAL_STATE_FAILED;
                strncpy(_portalErrMsg, "Connection timed out. Check password or range.", sizeof(_portalErrMsg) - 1);
                Serial.println("[WIFI] Portal connection timed out.");
            }
        } else if (_portalConnectState == PORTAL_STATE_SUCCESS) {
            // Allow 4 seconds for browser to render success page before closing SoftAP
            if (millis() - _portalSuccessMs > 4000) {
                Serial.println("[WIFI] Closing setup SoftAP after successful connection.");
                doStopPortals();
            }
        }

        // Auto-stop portal after 10 minutes of inactivity to conserve power
        if (millis() - _portalStartTimeMs > 600000) {
            Serial.println("[WIFI] SoftAP portal timeout (10 min). Stopping portal.");
            doStopPortals();
        }
    }

    bool connected = (WiFi.status() == WL_CONNECTED);
    SystemState& state = SystemState::instance();
    state.lock();
    auto& w = state.getContextUnsafe().wifi;
    w.isConnected = connected;
    if (_portalRunning) {
        w.apStations = (uint8_t)WiFi.softAPgetStationNum();
        w.portalConnectState = _portalConnectState;
    } else {
        w.apStations = 0;
        w.portalConnectState = 0;
    }
    if (connected) {
        strncpy(w.ssid, WiFi.SSID().c_str(), sizeof(w.ssid) - 1);
        strncpy(w.ipAddr, WiFi.localIP().toString().c_str(), sizeof(w.ipAddr) - 1);
        w.rssi = (int8_t)WiFi.RSSI();
    }
    state.unlock();

    uint32_t now = millis();
    if (!connected && !_portalRunning && _savedCount > 0 && (now - _lastReconnectMs >= 15000)) {
        _lastReconnectMs = now;
        tryAutoConnectMultiSsid();
    }
}
