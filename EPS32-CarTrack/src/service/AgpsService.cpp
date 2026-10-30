#include "service/AgpsService.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>

void AgpsService::init(GnssDriver* gnss) {
    _gnss = gnss;
}

bool AgpsService::downloadAndInjectAssistNow(const char* token) {
    if (!_gnss || !_gnss->isOnline() || !token || strlen(token) < 8) {
        return false;
    }

    String url = String("http://online-live1.services.u-blox.com/GetOnlineData.ashx?token=") +
                 token + "&gnss=gps,bds,glo,gal&datatype=eph,alm,aux";

    HTTPClient http;
    http.setTimeout(5000);
    if (!http.begin(url)) {
        return false;
    }

    int code = http.GET();
    if (code == HTTP_CODE_OK) {
        NetworkClient* stream = http.getStreamPtr();
        uint8_t buf[256];
        size_t totalBytes = 0;
        while (http.connected() && stream && stream->available() > 0) {
            int n = stream->readBytes(buf, sizeof(buf));
            if (n > 0) {
                _gnss->pushRawData(buf, n);
                totalBytes += n;
                delay(5);
            }
        }
        http.end();
        Serial.printf("[AGPS] Injected %u bytes of u-blox AssistNow ephemeris!\n", (unsigned)totalBytes);
        _injectedThisBoot = (totalBytes > 0);

        SystemState& state = SystemState::instance();
        state.lock();
        state.getContextUnsafe().wifi.agpsInjected = _injectedThisBoot;
        state.unlock();
        return _injectedThisBoot;
    }

    http.end();
    return false;
}

void AgpsService::update() {
    if (_injectedThisBoot) return;

    uint32_t now = millis();
    if (now - _lastTryMs < 20000) return;
    _lastTryMs = now;

    SystemContext snap = SystemState::instance().getSnapshot();
    if (snap.wifi.isConnected && !snap.gnss.isValid) {
        Preferences prefs;
        String token = "";
        if (prefs.begin("m5track", true)) {
            token = prefs.getString("agps_token", "");
            prefs.end();
        }
        if (token.length() >= 8) {
            downloadAndInjectAssistNow(token.c_str());
        }
    }
}
