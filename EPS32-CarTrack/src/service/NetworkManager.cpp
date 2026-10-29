#include "service/NetworkManager.h"
#include "service/TrackLogger.h"
#include <ArduinoJson.h>

void WifiNetworkManager::init() {
    _prefs.begin("m5track", false);
    _currentMode = NET_MODE_STA_IDLE;
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    _lastConnectCheck = millis();
}

void WifiNetworkManager::startConfigPortal() {
    _currentMode = NET_MODE_AP_CONFIG;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("M5Track-Setup", "");
    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

    _dnsServer.start(53, "*", apIP);
    setupWebServerRoutes();
    _server.begin();

    SystemState::instance().lock();
    SystemContext& ctx = SystemState::instance().getContextUnsafe();
    ctx.currentState = STATE_WIFI_CONFIG;
    ctx.localIp = apIP;
    SystemState::instance().unlock();
}

void WifiNetworkManager::startShareHotspot() {
    _currentMode = NET_MODE_AP_SHARE;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("M5Track-Share", "");
    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

    setupWebServerRoutes();
    _server.begin();

    SystemState::instance().lock();
    SystemState::instance().getContextUnsafe().localIp = apIP;
    SystemState::instance().unlock();
}

void WifiNetworkManager::stopHotspot() {
    if (_currentMode != NET_MODE_STA_IDLE) {
        _server.stop();
        _dnsServer.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        _currentMode = NET_MODE_STA_IDLE;
    }
}

void WifiNetworkManager::setupWebServerRoutes() {
    _server.on("/", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/api/scan", HTTP_GET, [this]() { handleScan(); });
    _server.on("/api/saved", HTTP_GET, [this]() { handleSaved(); });
    _server.on("/api/connect", HTTP_POST, [this]() { handleConnect(); });
    _server.on("/api/delete", HTTP_POST, [this]() { handleDelete(); });
    _server.on("/api/tracks", HTTP_GET, [this]() { handleTracks(); });
    _server.on("/api/download", HTTP_GET, [this]() { handleDownload(); });
    _server.on("/api/sd/format", HTTP_POST, [this]() { handleSdFormat(); });
}

void WifiNetworkManager::handleRoot() {
    String html = 
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>M5Track-M9N</title>"
        "<style>body{background:#000;color:#00ffaa;font-family:sans-serif;padding:20px;}"
        "h1{color:#ffd700;font-size:22px;}button{background:#111;color:#fff;border:1px solid #00ffaa;padding:10px 16px;border-radius:6px;margin:6px 0;}"
        "a{color:#00ffaa;text-decoration:none;display:block;margin:6px 0;}</style></head>"
        "<body><h1>M5Track-M9N 车载终端管理</h1>"
        "<p>模式: " + String(_currentMode == NET_MODE_AP_CONFIG ? "Wi-Fi 配网" : "轨迹导出") + "</p>"
        "<div id='tracks'><h3>📁 轨迹文件下载</h3><p>访问 /api/tracks 查看轨迹列表</p></div>"
        "</body></html>";
    _server.send(200, "text/html", html);
}

void WifiNetworkManager::handleScan() {
    int n = WiFi.scanNetworks();
    JsonDocument doc;
    JsonArray aps = doc["aps"].to<JsonArray>();
    for (int i = 0; i < n; i++) {
        JsonObject ap = aps.add<JsonObject>();
        ap["ssid"] = WiFi.SSID(i);
        ap["rssi"] = WiFi.RSSI(i);
        ap["auth"] = (int)WiFi.encryptionType(i);
    }
    String out;
    serializeJson(doc, out);
    _server.send(200, "application/json", out);
}

void WifiNetworkManager::handleSaved() {
    uint8_t count = _prefs.getUChar("wifi_count", 0);
    JsonDocument doc;
    JsonArray arr = doc["saved"].to<JsonArray>();
    for (uint8_t i = 0; i < count; i++) {
        char keySsid[16];
        snprintf(keySsid, sizeof(keySsid), "ssid_%d", i);
        String s = _prefs.getString(keySsid, "");
        if (s.length() > 0) {
            JsonObject obj = arr.add<JsonObject>();
            obj["ssid"] = s;
        }
    }
    String out;
    serializeJson(doc, out);
    _server.send(200, "application/json", out);
}

void WifiNetworkManager::handleConnect() {
    if (_server.hasArg("plain")) {
        JsonDocument doc;
        deserializeJson(doc, _server.arg("plain"));
        const char* ssid = doc["ssid"];
        const char* pwd = doc["pwd"];
        if (ssid && strlen(ssid) > 0) {
            uint8_t count = _prefs.getUChar("wifi_count", 0);
            if (count < 5) {
                char keyS[16], keyP[16];
                snprintf(keyS, sizeof(keyS), "ssid_%d", count);
                snprintf(keyP, sizeof(keyP), "pwd_%d", count);
                _prefs.putString(keyS, ssid);
                _prefs.putString(keyP, pwd ? pwd : "");
                _prefs.putUChar("wifi_count", count + 1);
            }
            _server.send(200, "application/json", "{\"success\":true,\"message\":\"Saved\"}");
            return;
        }
    }
    _server.send(400, "application/json", "{\"success\":false}");
}

void WifiNetworkManager::handleDelete() {
    if (_server.hasArg("plain")) {
        JsonDocument doc;
        deserializeJson(doc, _server.arg("plain"));
        const char* targetSsid = doc["ssid"];
        uint8_t count = _prefs.getUChar("wifi_count", 0);
        for (uint8_t i = 0; i < count; i++) {
            char keyS[16];
            snprintf(keyS, sizeof(keyS), "ssid_%d", i);
            if (_prefs.getString(keyS, "") == targetSsid) {
                _prefs.remove(keyS);
                char keyP[16];
                snprintf(keyP, sizeof(keyP), "pwd_%d", i);
                _prefs.remove(keyP);
                break;
            }
        }
        _server.send(200, "application/json", "{\"success\":true}");
        return;
    }
    _server.send(400, "application/json", "{\"success\":false}");
}

void WifiNetworkManager::handleTracks() {
    _server.send(200, "application/json", "{\"files\":[]}");
}

void WifiNetworkManager::handleDownload() {
    _server.send(404, "text/plain", "File not specified");
}

void WifiNetworkManager::handleSdFormat() {
    bool ok = TrackLogger::instance().formatSd();
    _server.send(200, "application/json", ok ? "{\"success\":true}" : "{\"success\":false}");
}

void WifiNetworkManager::autoConnectKnown() {
    if (WiFi.status() == WL_CONNECTED) return;

    uint8_t count = _prefs.getUChar("wifi_count", 0);
    for (uint8_t i = 0; i < count; i++) {
        char keyS[16], keyP[16];
        snprintf(keyS, sizeof(keyS), "ssid_%d", i);
        snprintf(keyP, sizeof(keyP), "pwd_%d", i);
        String s = _prefs.getString(keyS, "");
        String p = _prefs.getString(keyP, "");
        if (s.length() > 0) {
            WiFi.begin(s.c_str(), p.c_str());
            unsigned long t0 = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - t0 < 4000) {
                delay(100);
            }
            if (WiFi.status() == WL_CONNECTED) {
                break;
            }
        }
    }
}

void WifiNetworkManager::update() {
    if (_currentMode != NET_MODE_STA_IDLE) {
        if (_currentMode == NET_MODE_AP_CONFIG) {
            _dnsServer.processNextRequest();
        }
        _server.handleClient();
    } else {
        // In STA mode, check connection periodically
        unsigned long now = millis();
        if (now - _lastConnectCheck > 15000) {
            _lastConnectCheck = now;
            if (WiFi.status() != WL_CONNECTED) {
                autoConnectKnown();
            }
        }

        SystemState& state = SystemState::instance();
        state.lock();
        SystemContext& ctx = state.getContextUnsafe();
        ctx.isWifiConnected = (WiFi.status() == WL_CONNECTED);
        if (ctx.isWifiConnected) {
            ctx.wifiRssi = WiFi.RSSI();
            ctx.wifiSsid = WiFi.SSID();
            ctx.localIp = WiFi.localIP();
        } else {
            ctx.wifiRssi = -100;
        }
        state.unlock();
    }
}
