#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "config_s3_amoled.h"

enum AppWiFiState : uint8_t {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_CONNECTING   = 1,
    WIFI_STATE_CONNECTED    = 2,
    WIFI_STATE_AP_PORTAL    = 3
};

class NetworkManager_Setup {
public:
    static NetworkManager_Setup& instance() {
        static NetworkManager_Setup inst;
        return inst;
    }

    void init();
    void update();

    // Start Captive Portal SoftAP Mode
    void startCaptivePortal();
    void stopCaptivePortal();

    AppWiFiState getState() const { return _state; }
    const char* getApSsid() const { return _apSsid; }
    const char* getConnectedSsid() const { return _connectedSsid; }
    IPAddress getIpAddress() const { return _ipAddress; }
    int8_t getRssi() const { return _rssi; }

    // Generates QR Code content string for scanning
    const char* getQrCodeUrl() const { return _qrString; }

private:
    NetworkManager_Setup() = default;

    void handleRoot();
    void handleSave();
    void handleNotFound();
    bool tryConnectStoredWiFi();

    AppWiFiState _state = WIFI_STATE_DISCONNECTED;
    char _apSsid[32];
    char _connectedSsid[32];
    char _qrString[64];
    IPAddress _ipAddress;
    int8_t _rssi = -100;

    WebServer _server{80};
    DNSServer _dnsServer;
    bool _portalActive = false;
    uint32_t _lastConnCheckMs = 0;
};
