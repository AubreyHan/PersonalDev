#include "UiManager_AMOLED.h"
#include "service/CarTrackService.h"
#include "service/TimeService_Dual.h"
#include "service/NetworkManager_Setup.h"
#include "driver/GnssDriver_LC76G.h"
#include <cmath>

#define COLOR_AMOLED_BLACK    0x0000
#define COLOR_DARK_BG         0x0842
#define COLOR_CYAN            0x07FF
#define COLOR_ORANGE          0xFD20
#define COLOR_GREEN           0x07E0
#define COLOR_RED             0xF800
#define COLOR_WHITE           0xFFFF
#define COLOR_GREY           0x7BEF
#define COLOR_DARK_GREY      0x2104

bool UiManager_AMOLED::init() {
    Serial.println("[UI] Initializing CO5300 QSPI AMOLED Display (466x466)...");

    _bus = new Arduino_ESP32QSPI(
        BoardConfig::LCD_CS,
        BoardConfig::LCD_PCLK,
        BoardConfig::LCD_SDIO0,
        BoardConfig::LCD_SDIO1,
        BoardConfig::LCD_SDIO2,
        BoardConfig::LCD_SDIO3
    );

    _gfx = new Arduino_CO5300(
        _bus,
        BoardConfig::LCD_RST,
        0,                      // Rotation 0
        false,                  // IPS
        BoardConfig::SCREEN_WIDTH,
        BoardConfig::SCREEN_HEIGHT,
        BoardConfig::COL_OFFSET,
        BoardConfig::ROW_OFFSET,
        0, 0
    );

    if (!_gfx->begin()) {
        Serial.println("[UI] GFX begin failed!");
        return false;
    }

    _gfx->fillScreen(COLOR_AMOLED_BLACK);
    Serial.println("[UI] CO5300 AMOLED initialized successfully (5 Pages).");
    return true;
}

void UiManager_AMOLED::nextPage() {
    _currentPage = (UiPage)((_currentPage + 1) % PAGE_COUNT);
    _gfx->fillScreen(COLOR_AMOLED_BLACK);
}

void UiManager_AMOLED::prevPage() {
    _currentPage = (UiPage)((_currentPage + PAGE_COUNT - 1) % PAGE_COUNT);
    _gfx->fillScreen(COLOR_AMOLED_BLACK);
}

void UiManager_AMOLED::setPage(UiPage page) {
    _currentPage = page;
    _gfx->fillScreen(COLOR_AMOLED_BLACK);
}

void UiManager_AMOLED::update() {
    uint32_t now = millis();
    if (now - _lastRenderMs < 66) return; // ~15 FPS
    _lastRenderMs = now;

    if (!_gfx) return;

    renderStatusBar();

    switch (_currentPage) {
        case PAGE_ALTITUDE: renderPageAltitude(); break;
        case PAGE_SPEED:    renderPageSpeed();    break;
        case PAGE_TRAIL:    renderPageTrail();    break;
        case PAGE_COMPASS:  renderPageCompass();  break;
        case PAGE_WIFI:     renderPageWiFi();     break;
        default: break;
    }
}

void UiManager_AMOLED::renderStatusBar() {
    GnssDriver_LC76G& gnss = GnssDriver_LC76G::instance();
    NetworkManager_Setup& net = NetworkManager_Setup::instance();

    // 1. Left: GPS Status
    _gfx->fillRect(105, 16, 80, 18, COLOR_AMOLED_BLACK);
    if (gnss.isStable()) {
        _gfx->fillCircle(112, 24, 4, COLOR_GREEN);
        _gfx->setTextColor(COLOR_GREEN, COLOR_AMOLED_BLACK);
        _gfx->setTextSize(1);
        _gfx->setCursor(120, 21);
        _gfx->print("3D FIX");
    } else if (gnss.getActiveSats() > 0) {
        _gfx->fillCircle(112, 24, 4, COLOR_ORANGE);
        _gfx->setTextColor(COLOR_ORANGE, COLOR_AMOLED_BLACK);
        _gfx->setTextSize(1);
        _gfx->setCursor(120, 21);
        _gfx->printf("LOCK %d", gnss.getActiveSats());
    } else {
        _gfx->fillCircle(112, 24, 4, COLOR_GREY);
        _gfx->setTextColor(COLOR_GREY, COLOR_AMOLED_BLACK);
        _gfx->setTextSize(1);
        _gfx->setCursor(120, 21);
        _gfx->print("SEARCH");
    }

    // 2. Center: 5-Page Indicator Dots
    int dotX[5] = {204, 218, 233, 248, 262};
    for (int i = 0; i < 5; ++i) {
        if (i == _currentPage) {
            _gfx->fillCircle(dotX[i], 24, 3, COLOR_CYAN);
        } else {
            _gfx->fillCircle(dotX[i], 24, 2, COLOR_DARK_GREY);
        }
    }

    // 3. Right: WiFi & Clock
    _gfx->fillRect(276, 16, 88, 18, COLOR_AMOLED_BLACK);
    uint16_t wifiColor = (net.getState() == WIFI_STATE_CONNECTED) ? COLOR_GREEN :
                         (net.getState() == WIFI_STATE_AP_PORTAL)  ? COLOR_CYAN  : COLOR_GREY;
    _gfx->drawCircle(282, 24, 3, wifiColor);

    char timeBuf[12];
    TimeService_Dual::instance().getTimeString(timeBuf, sizeof(timeBuf));
    _gfx->setTextColor(COLOR_WHITE, COLOR_AMOLED_BLACK);
    _gfx->setTextSize(1);
    _gfx->setCursor(292, 21);
    _gfx->print(timeBuf);
}

void UiManager_AMOLED::renderPageAltitude() {
    CarTrackService& track = CarTrackService::instance();

    _gfx->setTextColor(COLOR_GREY, COLOR_AMOLED_BLACK);
    _gfx->setTextSize(1);
    _gfx->setCursor(170, 68);
    _gfx->print("ELEVATION / 高度");

    _gfx->fillRect(120, 84, 226, 50, COLOR_AMOLED_BLACK);
    _gfx->setTextColor(COLOR_WHITE, COLOR_AMOLED_BLACK);
    _gfx->setTextSize(4);
    _gfx->setCursor(150, 92);
    _gfx->printf("%d", (int)std::round(track.getCurrentAltitude()));
    _gfx->setTextSize(2);
    _gfx->setTextColor(COLOR_CYAN, COLOR_AMOLED_BLACK);
    _gfx->print(" m");

    const HistoryBuffer& hist = track.getAltitudeHistory();
    float minVal = (track.getMinAltitude() < 9000.0f) ? track.getMinAltitude() : 0.0f;
    float maxVal = (track.getMaxAltitude() > -9000.0f) ? track.getMaxAltitude() : 100.0f;
    if (maxVal - minVal < 20.0f) maxVal = minVal + 20.0f;

    drawLineChart(48, 160, 370, 130, hist.samples, hist.count, minVal, maxVal, COLOR_CYAN, COLOR_DARK_BG);

    _gfx->drawRoundRect(68, 318, 156, 68, 10, COLOR_DARK_GREY);
    _gfx->setTextColor(COLOR_GREY, COLOR_AMOLED_BLACK);
    _gfx->setTextSize(1);
    _gfx->setCursor(80, 328);
    _gfx->print("MAX ALT / 最高");
    _gfx->setTextColor(COLOR_WHITE, COLOR_AMOLED_BLACK);
    _gfx->setTextSize(2);
    _gfx->setCursor(80, 350);
    _gfx->printf("%d m", (int)std::round(track.getMaxAltitude() > -9000.0f ? track.getMaxAltitude() : 0.0f));

    _gfx->drawRoundRect(242, 318, 156, 68, 10, COLOR_DARK_GREY);
    _gfx->setTextColor(COLOR_GREY, COLOR_AMOLED_BLACK);
    _gfx->setTextSize(1);
    _gfx->setCursor(254, 328);
    _gfx->print("MIN ALT / 最低");
    _gfx->setTextColor(COLOR_WHITE, COLOR_AMOLED_BLACK);
    _gfx->setTextSize(2);
    _gfx->setCursor(254, 350);
    _gfx->printf("%d m", (int)std::round(track.getMinAltitude() < 9000.0f ? track.getMinAltitude() : 0.0f));
}

void UiManager_AMOLED::renderPageSpeed() {
    CarTrackService& track = CarTrackService::instance();

    _gfx->setTextColor(COLOR_GREY, COLOR_AMOLED_BLACK);
    _gfx->setTextSize(1);
    _gfx->setCursor(180, 68);
    _gfx->print("SPEED / 实时时速");

    _gfx->fillRect(130, 84, 210, 50, COLOR_AMOLED_BLACK);
    _gfx->setTextColor(COLOR_WHITE, COLOR_AMOLED_BLACK);
    _gfx->setTextSize(4);
    _gfx->setCursor(170, 92);
    _gfx->printf("%d", (int)std::round(track.getCurrentSpeed()));
    _gfx->setTextSize(2);
    _gfx->setTextColor(COLOR_ORANGE, COLOR_AMOLED_BLACK);
    _gfx->print(" km/h");

    const HistoryBuffer& hist = track.getSpeedHistory();
    float maxSpd = track.getMaxSpeed() > 30.0f ? track.getMaxSpeed() : 30.0f;
    drawLineChart(48, 160, 370, 130, hist.samples, hist.count, 0.0f, maxSpd, COLOR_ORANGE, COLOR_DARK_BG);

    _gfx->drawRoundRect(68, 318, 156, 68, 10, COLOR_DARK_GREY);
    _gfx->setTextColor(COLOR_GREY, COLOR_AMOLED_BLACK);
    _gfx->setTextSize(1);
    _gfx->setCursor(80, 328);
    _gfx->print("TRIP / 本次里程");
    _gfx->setTextColor(COLOR_WHITE, COLOR_AMOLED_BLACK);
    _gfx->setTextSize(2);
    _gfx->setCursor(80, 350);
    _gfx->printf("%.1f km", track.getTripDistanceKm());

    _gfx->drawRoundRect(242, 318, 156, 68, 10, COLOR_DARK_GREY);
    _gfx->setTextColor(COLOR_GREY, COLOR_AMOLED_BLACK);
    _gfx->setTextSize(1);
    _gfx->setCursor(254, 328);
    _gfx->print("MAX / 最高时速");
    _gfx->setTextColor(COLOR_WHITE, COLOR_AMOLED_BLACK);
    _gfx->setTextSize(2);
    _gfx->setCursor(254, 350);
    _gfx->printf("%d km/h", (int)std::round(track.getMaxSpeed()));
}

// =============================================================================
// Page 3: Auto-Fit Breadcrumb Trail Map (自适应整个显示屏的面包屑轨迹图)
// =============================================================================
void UiManager_AMOLED::renderPageTrail() {
    CarTrackService& track = CarTrackService::instance();

    int16_t cx = 233;
    int16_t cy = 233;

    // Draw Radar Range Rings (Concentric background circles)
    _gfx->drawCircle(cx, cy, 175, COLOR_DARK_GREY);
    _gfx->drawCircle(cx, cy, 115, COLOR_DARK_BG);
    _gfx->drawCircle(cx, cy, 55,  COLOR_DARK_BG);

    // Title Overlay
    _gfx->setTextColor(COLOR_CYAN, COLOR_AMOLED_BLACK);
    _gfx->setTextSize(1);
    _gfx->setCursor(162, 60);
    _gfx->print("TRAIL / 面包屑轨迹");

    size_t count = track.getTrailPointCount();
    const BreadcrumbPoint* pts = track.getTrailPoints();

    if (count < 2) {
        _gfx->setTextColor(COLOR_GREY, COLOR_AMOLED_BLACK);
        _gfx->setCursor(155, cy);
        _gfx->print("等待有效轨迹记录...");
        return;
    }

    // Dynamic Bounding Box Calculation
    double minLat, maxLat, minLon, maxLon;
    track.getTrailBounds(minLat, maxLat, minLon, maxLon);

    double centerLat = (minLat + maxLat) * 0.5;
    double centerLon = (minLon + maxLon) * 0.5;

    // Equirectangular local projection deltas in meters
    double dLatMeters = (maxLat - minLat) * 111319.5;
    double dLonMeters = (maxLon - minLon) * 111319.5 * std::cos(centerLat * 0.0174532925);

    if (dLatMeters < 50.0) dLatMeters = 50.0;
    if (dLonMeters < 50.0) dLonMeters = 50.0;

    // Usable display diameter: safe area radius = 145px (290px safe bounding circle)
    float safeDim = 280.0f;
    float scale = safeDim / std::max(dLatMeters, dLonMeters);

    // Convert GPS Point to Screen Pixel
    auto projectPoint = [&](double lat, double lon, int16_t& px, int16_t& py) {
        double dx = (lon - centerLon) * 111319.5 * std::cos(centerLat * 0.0174532925);
        double dy = (lat - centerLat) * 111319.5;
        px = cx + (int16_t)(dx * scale);
        py = cy - (int16_t)(dy * scale); // Invert Y for screen coordinates
    };

    // Draw Polyline & Breadcrumbs
    int16_t prevX, prevY;
    projectPoint(pts[0].lat, pts[0].lon, prevX, prevY);

    for (size_t i = 1; i < count; ++i) {
        int16_t curX, curY;
        projectPoint(pts[i].lat, pts[i].lon, curX, curY);

        _gfx->drawLine(prevX, prevY, curX, curY, COLOR_CYAN);
        _gfx->fillCircle(curX, curY, 1, COLOR_WHITE); // Small breadcrumb dot

        prevX = curX;
        prevY = curY;
    }

    // Draw START pin (pts[0])
    int16_t startX, startY;
    projectPoint(pts[0].lat, pts[0].lon, startX, startY);
    _gfx->fillCircle(startX, startY, 4, COLOR_GREEN);
    _gfx->drawCircle(startX, startY, 6, COLOR_WHITE);

    // Draw CURRENT NOW pin (last point)
    int16_t nowX, nowY;
    projectPoint(pts[count - 1].lat, pts[count - 1].lon, nowX, nowY);
    _gfx->fillCircle(nowX, nowY, 5, COLOR_CYAN);
    _gfx->drawCircle(nowX, nowY, 9, COLOR_CYAN);

    // Bottom Telemetry HUD Card
    _gfx->drawRoundRect(68, 360, 330, 52, 10, COLOR_DARK_GREY);
    _gfx->setTextColor(COLOR_GREY, COLOR_AMOLED_BLACK);
    _gfx->setTextSize(1);
    _gfx->setCursor(82, 370);
    _gfx->printf("POINTS: %d pts", (int)count);

    _gfx->setCursor(220, 370);
    _gfx->printf("SPAN: %.1fx%.1f km", dLonMeters / 1000.0, dLatMeters / 1000.0);

    _gfx->setTextColor(COLOR_GREEN, COLOR_AMOLED_BLACK);
    _gfx->setCursor(82, 388);
    _gfx->printf("STATUS: RECORDING (AUTO-FIT)");
}

void UiManager_AMOLED::renderPageCompass() {
    CarTrackService& track = CarTrackService::instance();
    float heading = track.getFusedHeading();

    int16_t cx = 233;
    int16_t cy = 240;
    int16_t r  = 180;

    _gfx->drawCircle(cx, cy, r, COLOR_DARK_GREY);
    _gfx->drawCircle(cx, cy, r - 30, COLOR_DARK_BG);

    const char* cards[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int angles[8] = {0, 45, 90, 135, 180, 225, 270, 315};

    for (int i = 0; i < 8; ++i) {
        float rad = (angles[i] - 90) * (M_PI / 180.0f);
        int16_t tx = cx + (int16_t)((r - 18) * std::cos(rad));
        int16_t ty = cy + (int16_t)((r - 18) * std::sin(rad));

        if (i == 0) {
            _gfx->setTextColor(COLOR_RED, COLOR_AMOLED_BLACK);
        } else {
            _gfx->setTextColor(COLOR_GREY, COLOR_AMOLED_BLACK);
        }
        _gfx->setTextSize(1);
        _gfx->setCursor(tx - 3, ty - 4);
        _gfx->print(cards[i]);
    }

    float radNeedle = (heading - 90.0f) * (M_PI / 180.0f);
    int16_t nx = cx + (int16_t)((r - 40) * std::cos(radNeedle));
    int16_t ny = cy + (int16_t)((r - 40) * std::sin(radNeedle));
    int16_t sx = cx - (int16_t)((r - 40) * std::cos(radNeedle));
    int16_t sy = cy - (int16_t)((r - 40) * std::sin(radNeedle));

    _gfx->drawLine(cx, cy, nx, ny, COLOR_RED);
    _gfx->drawLine(cx, cy, sx, sy, COLOR_WHITE);

    _gfx->fillCircle(cx, cy, 54, COLOR_AMOLED_BLACK);
    _gfx->drawCircle(cx, cy, 54, COLOR_DARK_GREY);

    _gfx->setTextColor(COLOR_WHITE, COLOR_AMOLED_BLACK);
    _gfx->setTextSize(3);
    _gfx->setCursor(cx - 30, cy - 16);
    _gfx->printf("%03d", (int)std::round(heading));

    _gfx->setTextSize(1);
    _gfx->setTextColor(COLOR_CYAN, COLOR_AMOLED_BLACK);
    _gfx->setCursor(cx - 16, cy + 14);

    int idx = ((int)(heading + 22.5f) % 360) / 45;
    _gfx->print(cards[idx]);
}

void UiManager_AMOLED::renderPageWiFi() {
    NetworkManager_Setup& net = NetworkManager_Setup::instance();

    _gfx->setTextColor(COLOR_CYAN, COLOR_AMOLED_BLACK);
    _gfx->setTextSize(1);
    _gfx->setCursor(160, 68);
    _gfx->print("WIFI CONFIG / 配网");

    _gfx->setTextColor(COLOR_GREY, COLOR_AMOLED_BLACK);
    _gfx->setCursor(140, 84);
    _gfx->print("微信或手机浏览器扫码配网");

    drawQrCode(233, 220, 180, net.getQrCodeUrl());

    _gfx->drawRoundRect(68, 335, 330, 56, 10, COLOR_DARK_GREY);
    _gfx->setTextColor(COLOR_WHITE, COLOR_AMOLED_BLACK);
    _gfx->setTextSize(1);
    _gfx->setCursor(82, 347);
    _gfx->printf("AP: %s", net.getApSsid());
    _gfx->setTextColor(COLOR_CYAN, COLOR_AMOLED_BLACK);
    _gfx->setCursor(82, 365);
    _gfx->printf("IP: %s", net.getIpAddress().toString().c_str());
}

void UiManager_AMOLED::drawLineChart(int16_t x, int16_t y, int16_t w, int16_t h,
                                      const float* data, uint16_t count,
                                      float minVal, float maxVal,
                                      uint16_t lineColor, uint16_t fillColor) {
    _gfx->drawRoundRect(x, y, w, h, 8, COLOR_DARK_GREY);
    _gfx->drawFastHLine(x + 10, y + h / 2, w - 20, COLOR_DARK_BG);

    if (count < 2) return;

    float range = (maxVal - minVal > 0.001f) ? (maxVal - minVal) : 1.0f;
    float xStep = (float)(w - 30) / (HistoryBuffer::CAPACITY - 1);

    int16_t prevPx = x + 15;
    int16_t prevPy = y + h - 15 - (int16_t)(((data[0] - minVal) / range) * (h - 30));

    for (uint16_t i = 1; i < count; ++i) {
        int16_t curPx = x + 15 + (int16_t)(i * xStep);
        int16_t curPy = y + h - 15 - (int16_t)(((data[i] - minVal) / range) * (h - 30));

        _gfx->drawLine(prevPx, prevPy, curPx, curPy, lineColor);
        prevPx = curPx;
        prevPy = curPy;
    }
}

void UiManager_AMOLED::drawQrCode(int16_t cx, int16_t cy, int16_t size, const char* text) {
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(3)];
    qrcode_initText(&qrcode, qrcodeData, 3, 0, text);

    int16_t startX = cx - size / 2;
    int16_t startY = cy - size / 2;
    int scale = size / qrcode.size;
    if (scale < 1) scale = 1;

    _gfx->fillRoundRect(startX - 8, startY - 8, size + 16, size + 16, 8, COLOR_WHITE);

    for (uint8_t y = 0; y < qrcode.size; ++y) {
        for (uint8_t x = 0; x < qrcode.size; ++x) {
            if (qrcode_getModule(&qrcode, x, y)) {
                _gfx->fillRect(startX + x * scale, startY + y * scale, scale, scale, COLOR_AMOLED_BLACK);
            }
        }
    }
}

void UiManager_AMOLED::handleTouch(int16_t x, int16_t y, bool isPressed) {
    if (isPressed) {
        if (!_touchDown) {
            _touchDown = true;
            _touchStartX = x;
            _touchStartY = y;
        }
    } else {
        if (_touchDown) {
            _touchDown = false;
            int16_t dx = x - _touchStartX;
            if (dx < -50) {
                nextPage();
            } else if (dx > 50) {
                prevPage();
            }
        }
    }
}
