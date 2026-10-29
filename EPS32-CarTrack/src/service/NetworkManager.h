#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "model/SystemState.h"

enum NetMode : uint8_t {
    NET_MODE_STA_IDLE,
    NET_MODE_AP_CONFIG,
    NET_MODE_AP_SHARE
};

class NetworkManager {
public:
    static NetworkManager& instance() {
        static NetworkManager inst;
        return inst;
    }

    void init();
    void update();

    void startConfigPortal();
    void startShareHotspot();
    void stopHotspot();

    bool isApActive() const { return _currentMode != NET_MODE_STA_IDLE; }

private:
    NetworkManager() : _server(80), _currentMode(NET_MODE_STA_IDLE), _lastScanTime(0), _lastConnectCheck(0) {}

    WebServer    _server;
    DNSServer    _dnsServer;
    Preferences  _prefs;
    NetMode      _currentMode;
    unsigned long _lastScanTime;
    unsigned long _lastConnectCheck;

    void setupWebServerRoutes();
    void handleRoot();
    void handleScan();
    void handleSaved();
    void handleConnect();
    void handleDelete();
    void handleTracks();
    void handleDownload();
    void handleSdFormat();
    void autoConnectKnown();
};
