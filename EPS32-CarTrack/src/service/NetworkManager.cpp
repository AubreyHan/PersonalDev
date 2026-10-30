#include "service/NetworkManager.h"
#include "service/TrackLogger.h"
#include "service/TunnelService.h"
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

void AppNetworkManager::init() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    loadSavedNetworks();
    setupHttpRoutes();
    tryAutoConnectMultiSsid();
}

void AppNetworkManager::loadSavedNetworks() {
    Preferences prefs;
    _savedCount = 0;
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
                _saved[_savedCount].ssid     = s;
                _saved[_savedCount].password = p;
                _savedCount++;
            }
        }
        prefs.end();
    }

    SystemState& state = SystemState::instance();
    state.lock();
    state.getContextUnsafe().wifi.savedNetworkCount = _savedCount;
    state.unlock();
}

bool AppNetworkManager::saveNetworkCredential(const String& ssid, const String& pwd) {
    if (ssid.length() == 0) return false;
    loadSavedNetworks();

    int existingIdx = -1;
    for (uint8_t i = 0; i < _savedCount; i++) {
        if (_saved[i].ssid == ssid) {
            existingIdx = i;
            break;
        }
    }

    if (existingIdx >= 0) {
        _saved[existingIdx].password = pwd;
    } else if (_savedCount < 5) {
        _saved[_savedCount].ssid     = ssid;
        _saved[_savedCount].password = pwd;
        _savedCount++;
    } else {
        for (int i = 0; i < 4; i++) _saved[i] = _saved[i + 1];
        _saved[4].ssid     = ssid;
        _saved[4].password = pwd;
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

    SystemState& state = SystemState::instance();
    state.lock();
    state.getContextUnsafe().wifi.savedNetworkCount = _savedCount;
    state.unlock();

    WiFi.begin(ssid.c_str(), pwd.c_str());
    return true;
}

bool AppNetworkManager::deleteNetworkCredential(const String& ssid) {
    loadSavedNetworks();
    uint8_t newCount = 0;
    SavedWifiCredential temp[5];
    for (uint8_t i = 0; i < _savedCount; i++) {
        if (_saved[i].ssid != ssid) {
            temp[newCount++] = _saved[i];
        }
    }
    _savedCount = newCount;
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

    SystemState& state = SystemState::instance();
    state.lock();
    state.getContextUnsafe().wifi.savedNetworkCount = _savedCount;
    state.unlock();
    return true;
}

uint8_t AppNetworkManager::getSavedNetworks(SavedWifiCredential* outArr, uint8_t maxCount) {
    uint8_t n = (_savedCount < maxCount) ? _savedCount : maxCount;
    for (uint8_t i = 0; i < n; i++) outArr[i] = _saved[i];
    return n;
}

void AppNetworkManager::tryAutoConnectMultiSsid() {
    if (_savedCount == 0 || _portalRunning) return;
    if (WiFi.status() == WL_CONNECTED) return;

    uint8_t idx = _connectAttemptIdx % _savedCount;
    _connectAttemptIdx++;
    WiFi.begin(_saved[idx].ssid.c_str(), _saved[idx].password.c_str());
}

void AppNetworkManager::startSetupPortal() {
    stopPortals();
    WiFi.mode(WIFI_AP_STA);
    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP("M5Track-Setup");

    // Start DNS Captive Portal (Redirect all domains to 192.168.4.1 so mobile phone auto-pops UI)
    _dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    _dnsServer.start(53, "*", apIP);

    _server.begin();
    _portalRunning = true;
    _isShareMode   = false;

    SystemState& state = SystemState::instance();
    state.lock();
    state.getContextUnsafe().wifi.apSetupActive = true;
    state.getContextUnsafe().wifi.apShareActive = false;
    state.unlock();
    Serial.println("[WIFI] Started Captive Portal SoftAP: M5Track-Setup (192.168.4.1)");
}

void AppNetworkManager::startSharePortal() {
    stopPortals();
    WiFi.mode(WIFI_AP_STA);
    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP("M5Track-Share");

    // Start DNS Captive Portal (Redirect all domains to 192.168.4.1 so mobile phone auto-pops Track Download UI)
    _dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    _dnsServer.start(53, "*", apIP);

    _server.begin();
    _portalRunning = true;
    _isShareMode   = true;

    SystemState& state = SystemState::instance();
    state.lock();
    state.getContextUnsafe().wifi.apShareActive = true;
    state.getContextUnsafe().wifi.apSetupActive = false;
    state.unlock();
    Serial.println("[WIFI] Started Track Share SoftAP: M5Track-Share (192.168.4.1)");
}

void AppNetworkManager::stopPortals() {
    if (_portalRunning) {
        _dnsServer.stop();
        _server.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        _portalRunning = false;
        _isShareMode   = false;

        SystemState& state = SystemState::instance();
        state.lock();
        state.getContextUnsafe().wifi.apShareActive = false;
        state.getContextUnsafe().wifi.apSetupActive = false;
        state.unlock();
    }
}

void AppNetworkManager::handleRootPage() {
    if (_isShareMode) {
        handleShareTracksPage();
        return;
    }

    const char* html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>M5Track-Tab5 Wi-Fi Setup</title>
<style>
body{background:#000;color:#00FFAA;font-family:sans-serif;margin:16px;}
.card{background:#121518;border:1px solid #283038;border-radius:10px;padding:16px;margin-bottom:16px;}
input,button{width:100%;padding:12px;margin-top:8px;border-radius:6px;border:1px solid #00FFAA;background:#080C08;color:#fff;box-sizing:border-box;font-size:16px;}
button{background:#00FFAA;color:#000;font-weight:bold;cursor:pointer;}
.ap-item{padding:8px;border-bottom:1px solid #283038;cursor:pointer;color:#fff;}
</style></head><body>
<h2>M5Track-Tab5 Wi-Fi Provisioning</h2>
<div class="card">
  <h3>Connect New Wi-Fi</h3>
  <input id="ssid" placeholder="Wi-Fi SSID">
  <input id="pwd" type="password" placeholder="Wi-Fi Password">
  <button onclick="connectWifi()">Save & Connect</button>
  <p id="msg"></p>
</div>
<div class="card">
  <h3>Nearby Networks (Tap to select)</h3>
  <div id="aps">Scanning...</div>
</div>
<script>
function connectWifi(){
  fetch('/api/connect',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify({ssid:document.getElementById('ssid').value,pwd:document.getElementById('pwd').value})})
  .then(r=>r.json()).then(d=>{document.getElementById('msg').innerText=d.message;});
}
fetch('/api/scan').then(r=>r.json()).then(d=>{
  let h='';
  (d.aps||[]).forEach(a=>{h+=`<div class='ap-item' onclick="document.getElementById('ssid').value='${a.ssid}'">${a.ssid} (${a.rssi} dBm)</div>`;});
  document.getElementById('aps').innerHTML=h||'No networks found';
});
</script></body></html>
)rawliteral";
    _server.send(200, "text/html", html);
}

void AppNetworkManager::handleShareTracksPage() {
    const char* html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>M5Track-Tab5 GPX Track Share</title>
<style>
body{background:#000;color:#00FFAA;font-family:sans-serif;margin:16px;}
.card{background:#121518;border:1px solid #283038;border-radius:10px;padding:16px;margin-bottom:12px;}
a.btn{display:inline-block;padding:10px 16px;background:#00FFAA;color:#000;font-weight:bold;text-decoration:none;border-radius:6px;margin-top:8px;}
.badge{padding:3px 8px;border-radius:4px;font-size:12px;background:#283038;color:#FFD700;}
</style></head><body>
<h2>M5Track-Tab5 High-Speed GPX Download</h2>
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
    // Primary entry & Captive Portal OS detection endpoints (iOS / Android / HarmonyOS / Windows)
    _server.on("/", HTTP_GET, [this]() { handleRootPage(); });
    _server.on("/setup", HTTP_GET, [this]() { _isShareMode = false; handleRootPage(); });
    _server.on("/tracks", HTTP_GET, [this]() { handleShareTracksPage(); });
    _server.on("/generate_204", HTTP_GET, [this]() { handleRootPage(); });
    _server.on("/hotspot-detect.html", HTTP_GET, [this]() { handleRootPage(); });
    _server.on("/ncsi.txt", HTTP_GET, [this]() { handleRootPage(); });
    _server.onNotFound([this]() { handleRootPage(); });

    _server.on("/api/scan", HTTP_GET, [this]() {
        int n = WiFi.scanNetworks();
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
        String ssid = doc["ssid"] | "";
        String pwd  = doc["pwd"]  | "";
        bool ok = saveNetworkCredential(ssid, pwd);
        _server.send(200, "application/json", ok ? "{\"success\":true,\"message\":\"Connecting...\"}"
                                                 : "{\"success\":false,\"message\":\"Invalid SSID\"}");
    });

    _server.on("/api/delete", HTTP_POST, [this]() {
        JsonDocument doc;
        deserializeJson(doc, _server.arg("plain"));
        String ssid = doc["ssid"] | "";
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
            o["badge"] = entries[i].isGcj02 ? "GCJ-02 (Amap/Domestic)" : "WGS-84 (Global/Google Earth)";
        }
        String out;
        serializeJson(doc, out);
        _server.send(200, "application/json", out);
    });

    _server.on("/api/download", HTTP_GET, [this]() {
        String fname = _server.arg("file");
        FsFile f;
        if (!TrackLogger::instance().openFileForRead(fname.c_str(), f)) {
            _server.send(404, "text/plain", "Track file not found");
            return;
        }
        _server.sendHeader("Content-Disposition", String("attachment; filename=\"") + fname + "\"");
        _server.setContentLength(f.fileSize());
        _server.send(200, "application/gpx+xml", "");

        uint8_t buf[512];
        while (f.available()) {
            int n = f.read(buf, sizeof(buf));
            if (n > 0) {
                _server.client().write(buf, n);
            }
        }
        f.close();
    });

    _server.on("/api/sd/format", HTTP_POST, [this]() {
        bool ok = TrackLogger::instance().formatFat32();
        _server.send(200, "application/json", ok ? "{\"success\":true,\"message\":\"SD Formatted\"}"
                                                 : "{\"success\":false,\"message\":\"Format Failed\"}");
    });

    _server.on("/api/tunnel/info", HTTP_GET, [this]() {
        _server.send(200, "application/json", "{\"version\":\"2026.10\",\"count\":82,\"source\":\"LittleFS\"}");
    });
}

void AppNetworkManager::update() {
    if (_portalRunning) {
        _dnsServer.processNextRequest();
        _server.handleClient();
    }

    bool connected = (WiFi.status() == WL_CONNECTED);
    SystemState& state = SystemState::instance();
    state.lock();
    auto& w = state.getContextUnsafe().wifi;
    w.isConnected = connected;
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
