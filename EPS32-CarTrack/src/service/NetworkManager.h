#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "model/SystemState.h"

struct SavedWifiCredential {
    char ssid[36];
    char password[68];
};

class AppNetworkManager {
public:
    static AppNetworkManager& instance() {
        static AppNetworkManager inst;
        return inst;
    }

    void init();
    void update();

    // SoftAP Modes with QR Code & Captive Portal Auto-Popup
    void startSetupPortal(); // SSID: "M5Track-Setup", IP: 192.168.4.1
    void startSharePortal(); // SSID: "M5Track-Share", IP: 192.168.4.1
    void stopPortals();

    bool saveNetworkCredential(const char* ssid, const char* pwd);
    bool deleteNetworkCredential(const char* ssid);
    uint8_t getSavedNetworks(SavedWifiCredential* outArr, uint8_t maxCount);

private:
    AppNetworkManager() : _server(80) {
        _netMutex = xSemaphoreCreateRecursiveMutex();
        memset(_saved, 0, sizeof(_saved));
    }

    void loadSavedNetworks();
    void tryAutoConnectMultiSsid();
    void setupHttpRoutes();
    void handleRootPage();
    void handleShareTracksPage();

    SemaphoreHandle_t   _netMutex         = nullptr;
    WebServer           _server;
    DNSServer           _dnsServer;
    SavedWifiCredential _saved[5];
    uint8_t             _savedCount       = 0;
    uint8_t             _connectAttemptIdx= 0;
    uint32_t            _lastReconnectMs  = 0;
    bool                _portalRunning    = false;
    bool                _isShareMode      = false;
};
