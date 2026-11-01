#include "NetworkManager_Setup.h"
#include <Preferences.h>
#include <esp_wifi.h>

void NetworkManager_Setup::init() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(_apSsid, sizeof(_apSsid), "%s%02X%02X",
             NetworkConfig::AP_SSID_PREFIX, mac[4], mac[5]);
    snprintf(_qrString, sizeof(_qrString), "http://%s", NetworkConfig::AP_DEFAULT_IP);

    _connectedSsid[0] = '\0';
    Serial.printf("[Network] Initializing, default AP SSID: %s\n", _apSsid);

    // Try connecting to stored credentials
    if (!tryConnectStoredWiFi()) {
        // If not connected, start captive portal AP mode automatically
        startCaptivePortal();
    }
}

bool NetworkManager_Setup::tryConnectStoredWiFi() {
    Preferences prefs;
    if (!prefs.begin("cartrack_wifi", true)) return false;
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    prefs.end();

    if (ssid.length() == 0) return false;

    Serial.printf("[Network] Connecting to stored WiFi: %s ...\n", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    _state = WIFI_STATE_CONNECTING;
    strncpy(_connectedSsid, ssid.c_str(), sizeof(_connectedSsid) - 1);

    uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < 6000) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (WiFi.status() == WL_CONNECTED) {
        _state = WIFI_STATE_CONNECTED;
        _ipAddress = WiFi.localIP();
        _rssi = WiFi.RSSI();
        Serial.printf("[Network] WiFi Connected! IP: %s, RSSI: %d dBm\n",
                      _ipAddress.toString().c_str(), _rssi);
        return true;
    }

    Serial.println("[Network] Stored WiFi connection timed out.");
    _state = WIFI_STATE_DISCONNECTED;
    return false;
}

void NetworkManager_Setup::startCaptivePortal() {
    if (_portalActive) return;

    Serial.println("[Network] Starting SoftAP & Captive Portal WebServer...");
    WiFi.mode(WIFI_AP_STA); // AP + STA allows scanning nearby SSIDs while broadcasting
    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(_apSsid);

    _dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    _dnsServer.start(53, "*", apIP);

    _server.on("/", [this]() { handleRoot(); });
    _server.on("/save", HTTP_POST, [this]() { handleSave(); });
    _server.onNotFound([this]() { handleNotFound(); });
    _server.begin();

    _portalActive = true;
    _state = WIFI_STATE_AP_PORTAL;
    _ipAddress = apIP;
    Serial.printf("[Network] AP '%s' active at http://192.168.4.1\n", _apSsid);
}

void NetworkManager_Setup::stopCaptivePortal() {
    if (!_portalActive) return;
    _server.stop();
    _dnsServer.stop();
    WiFi.softAPdisconnect(true);
    _portalActive = false;
    Serial.println("[Network] Captive Portal stopped.");
}

void NetworkManager_Setup::update() {
    if (_portalActive) {
        _dnsServer.processNextRequest();
        _server.handleClient();
    }

    // Monitor WiFi Station status
    if (millis() - _lastConnCheckMs > 3000) {
        _lastConnCheckMs = millis();
        if (WiFi.status() == WL_CONNECTED) {
            _state = WIFI_STATE_CONNECTED;
            _rssi = WiFi.RSSI();
            _ipAddress = WiFi.localIP();
        } else if (_portalActive) {
            _state = WIFI_STATE_AP_PORTAL;
        } else {
            _state = WIFI_STATE_DISCONNECTED;
        }
    }
}

void NetworkManager_Setup::handleRoot() {
    int n = WiFi.scanNetworks();
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>CarTrack WiFi 配置</title>";
    html += "<style>body{font-family:-apple-system,sans-serif;background:#0d1117;color:#c9d1d9;padding:20px;text-align:center;}";
    html += "input,select,button{width:100%;max-width:320px;padding:12px;margin:8px 0;border-radius:8px;border:1px solid #30363d;background:#161b22;color:#fff;box-sizing:border-box;}";
    html += "button{background:#238636;color:#fff;font-weight:bold;cursor:pointer;border:none;margin-top:16px;}";
    html += ".card{max-width:360px;margin:0 auto;background:#21262d;padding:24px;border-radius:12px;box-shadow:0 8px 24px rgba(0,0,0,0.5);}";
    html += "h2{color:#58a6ff;margin-top:0;}</style></head><body>";
    html += "<div class='card'><h2>CarTrack WiFi 配网</h2>";
    html += "<form method='POST' action='/save'>";
    html += "<label style='display:block;text-align:left;font-size:13px;color:#8b949e;'>选择无线网络 (2.4GHz):</label>";
    html += "<select name='ssid'>";
    for (int i = 0; i < n; ++i) {
        html += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + "dBm)</option>";
    }
    html += "</select>";
    html += "<label style='display:block;text-align:left;font-size:13px;color:#8b949e;'>WiFi 密码:</label>";
    html += "<input type='password' name='pass' placeholder='输入密码 (无密码留空)'>";
    html += "<button type='submit'>连接网络</button>";
    html += "</form></div></body></html>";

    _server.send(200, "text/html", html);
}

void NetworkManager_Setup::handleSave() {
    String ssid = _server.arg("ssid");
    String pass = _server.arg("pass");

    Preferences prefs;
    if (prefs.begin("cartrack_wifi", false)) {
        prefs.putString("ssid", ssid);
        prefs.putString("pass", pass);
        prefs.end();
    }

    String resp = "<!DOCTYPE html><html><body style='background:#0d1117;color:#58a6ff;text-align:center;padding:40px;font-family:sans-serif;'>";
    resp += "<h2>WiFi 配置已保存</h2><p>设备正在连接: <b>" + ssid + "</b></p><p>请查看设备屏幕状态。</p></body></html>";
    _server.send(200, "text/html", resp);

    vTaskDelay(pdMS_TO_TICKS(500));
    tryConnectStoredWiFi();
}

void NetworkManager_Setup::handleNotFound() {
    // Captive portal redirect to /
    _server.sendHeader("Location", "http://192.168.4.1/", true);
    _server.send(302, "text/plain", "");
}
