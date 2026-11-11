#include "Page4Trail.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <esp_heap_caps.h>

namespace {

// 4-Corner Floating Semi-Transparent HUD Capsule Hitboxes & Geometry
constexpr int16_t HUD_TL_X = 18;
constexpr int16_t HUD_TL_Y = 14;
constexpr int16_t HUD_TL_W = 432;
constexpr int16_t HUD_TL_H = 104;

constexpr int16_t HUD_TR_X = 838;
constexpr int16_t HUD_TR_Y = 14;
constexpr int16_t HUD_TR_W = 264;
constexpr int16_t HUD_TR_H = 98;

constexpr int16_t HUD_BL_X = 18;
constexpr int16_t HUD_BL_Y = 602;
constexpr int16_t HUD_BL_W = 368;
constexpr int16_t HUD_BL_H = 98;

constexpr int16_t HUD_BR_X = 724;
constexpr int16_t HUD_BR_Y = 602;
constexpr int16_t HUD_BR_W = 378;
constexpr int16_t HUD_BR_H = 98;

constexpr float FOLLOW_HALF_SPAN_M[3] = { 250.0f, 650.0f, 1800.0f };

double computeHaversineKm(double lat1, double lon1, double lat2, double lon2) {
    constexpr double R_KM = 6371.0088;
    const double dLat = (lat2 - lat1) * (M_PI / 180.0);
    const double dLon = (lon2 - lon1) * (M_PI / 180.0);
    const double a = sin(dLat * 0.5) * sin(dLat * 0.5) +
                     cos(lat1 * (M_PI / 180.0)) * cos(lat2 * (M_PI / 180.0)) *
                     sin(dLon * 0.5) * sin(dLon * 0.5);
    return 2.0 * R_KM * atan2(sqrt(a), sqrt(fmax(0.0, 1.0 - a)));
}

float computeBearingDeg(double lat1, double lon1, double lat2, double lon2) {
    const double phi1 = lat1 * (M_PI / 180.0);
    const double phi2 = lat2 * (M_PI / 180.0);
    const double dLam = (lon2 - lon1) * (M_PI / 180.0);
    const double y    = sin(dLam) * cos(phi2);
    const double x    = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(dLam);
    double deg        = atan2(y, x) * (180.0 / M_PI);
    if (deg < 0.0) deg += 360.0;
    return static_cast<float>(deg);
}

} // namespace

Page4Trail::~Page4Trail() {
    if (_points != nullptr) {
        free(_points);
        _points = nullptr;
    }
}

void Page4Trail::ensureTrailBuffer() {
    if (_points != nullptr) {
        return;
    }
    const size_t bytes = TrailPreviewMeta::MAX_POINTS * sizeof(TrailPoint);
    void* mem = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mem == nullptr) {
        mem = malloc(bytes);
    }
    _points = static_cast<TrailPoint*>(mem);
}

TripCategory Page4Trail::effectiveCategory(const SystemStateSnapshot& state) {
    TripCategory cat = state.tripControl.activeViewCategory;
    if (cat >= TRIP_CATEGORY_COUNT) {
        return TRIP_SINGLE;
    }
    if (cat == TRIP_MANUAL && !state.tripControl.manualTripActive) {
        return TRIP_SINGLE;
    }
    return cat;
}

void Page4Trail::onEnter(const SystemStateSnapshot& state) {
    ensureTrailBuffer();
    _cachedCategory = TRIP_CATEGORY_COUNT; // Force immediate copy on enter
    _lastResetVersion = state.tripControl.tripResetVersion;
}

void Page4Trail::syncTrailCache(const SystemStateSnapshot& state, TripCategory cat) {
    ensureTrailBuffer();
    if (_points == nullptr) {
        _pointCount = 0;
        return;
    }

    const bool resetOccurred = (state.tripControl.tripResetVersion != _lastResetVersion);
    const uint32_t targetVer = state.trailMeta[cat].version;

    if (resetOccurred || _cachedCategory != cat || _cachedMetaVersion != targetVer) {
        _lastResetVersion  = state.tripControl.tripResetVersion;
        _cachedCategory    = cat;
        _cachedMetaVersion = targetVer;
        _pointCount        = SystemState::instance().copyTrailPoints(
            cat, _points, TrailPreviewMeta::MAX_POINTS, &_cachedMeta
        );
    }
}

void Page4Trail::drawDashedSegment(M5Canvas& canvas, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                                   float width, uint32_t color) {
    const float dx  = static_cast<float>(x1 - x0);
    const float dy  = static_cast<float>(y1 - y0);
    const float len = hypotf(dx, dy);
    if (len < 1.0f) {
        return;
    }
    constexpr float DASH_PX = 10.0f;
    constexpr float GAP_PX  = 7.0f;
    const float ux = dx / len;
    const float uy = dy / len;

    for (float d = 0.0f; d < len; d += (DASH_PX + GAP_PX)) {
        const float dEnd = fminf(len, d + DASH_PX);
        const int16_t sx = x0 + static_cast<int16_t>(lroundf(ux * d));
        const int16_t sy = y0 + static_cast<int16_t>(lroundf(uy * d));
        const int16_t ex = x0 + static_cast<int16_t>(lroundf(ux * dEnd));
        const int16_t ey = y0 + static_cast<int16_t>(lroundf(uy * dEnd));
        canvas.drawWideLine(sx, sy, ex, ey, width, color);
    }
}

void Page4Trail::drawStartFlag(M5Canvas& canvas, int16_t x, int16_t y) {
    // Glowing start anchor ring + crisp flagpole + green pennant flag
    canvas.fillSmoothCircle(x, y, 8, 0x074D38U);
    canvas.fillSmoothCircle(x, y, 4, ThemeColors::NEON_GREEN);
    canvas.drawWideLine(x, y, x, y - 28, 2.5f, ThemeColors::TEXT_WHITE);
    canvas.fillTriangle(x + 1, y - 28, x + 22, y - 21, x + 1, y - 14, ThemeColors::NEON_GREEN);

    canvas.setFont(&fonts::efontCN_12_b);
    canvas.setTextDatum(textdatum_t::bottom_center);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.drawString("起点", x, y - 31);
}

void Page4Trail::drawVehicleChevron(M5Canvas& canvas, int16_t cx, int16_t cy, float headingDeg, uint32_t color) {
    const float rad = headingDeg * (static_cast<float>(M_PI) / 180.0f);
    const float s   = sinf(rad);
    const float c   = cosf(rad);

    auto rotX = [&](float lx, float ly) -> int16_t {
        return cx + static_cast<int16_t>(lroundf(lx * c - ly * s));
    };
    auto rotY = [&](float lx, float ly) -> int16_t {
        return cy + static_cast<int16_t>(lroundf(lx * s + ly * c));
    };

    // Subtle outer navigation ring
    canvas.drawCircle(cx, cy, 20, 0x1A4252U);

    // Local coordinates with Nose at (0, -17), Left Wing (-12, +12), Tail Notch (0, +5), Right Wing (+12, +12)
    const int16_t nx  = rotX(0.0f, -17.0f);
    const int16_t ny  = rotY(0.0f, -17.0f);
    const int16_t lwx = rotX(-12.0f, 12.0f);
    const int16_t lwy = rotY(-12.0f, 12.0f);
    const int16_t rwx = rotX(12.0f, 12.0f);
    const int16_t rwy = rotY(12.0f, 12.0f);
    const int16_t tx  = rotX(0.0f, 4.0f);
    const int16_t ty  = rotY(0.0f, 4.0f);

    canvas.fillTriangle(nx, ny, lwx, lwy, tx, ty, color);
    canvas.fillTriangle(nx, ny, rwx, rwy, tx, ty, ThemeColors::TEXT_WHITE);
    canvas.drawWideLine(nx, ny, lwx, lwy, 1.6f, ThemeColors::TEXT_WHITE);
    canvas.drawWideLine(nx, ny, rwx, rwy, 1.6f, color);
}

void Page4Trail::drawTacticalGrid(M5Canvas& canvas) {
    // Pure black background already cleared; draw ultra-subtle night-safe tactical grid
    constexpr uint32_t GRID_COL = 0x0B121AU;
    for (int16_t x = 140; x < Viewport::SAFE_W; x += 140) {
        canvas.drawFastVLine(x, 0, Viewport::SAFE_H, GRID_COL);
    }
    for (int16_t y = 120; y < Viewport::SAFE_H; y += 120) {
        canvas.drawFastHLine(0, y, Viewport::SAFE_W, GRID_COL);
    }
    // Subtle center reticle
    canvas.drawFastHLine(Viewport::SAFE_W / 2 - 14, Viewport::SAFE_H / 2, 29, 0x182636U);
    canvas.drawFastVLine(Viewport::SAFE_W / 2, Viewport::SAFE_H / 2 - 14, 29, 0x182636U);
}

void Page4Trail::render(M5Canvas& canvas, const SystemStateSnapshot& state) {
    clearViewportCanvas(canvas);
    drawTacticalGrid(canvas);

    const TripCategory cat = effectiveCategory(state);
    syncTrailCache(state, cat);

    const bool hasLivePos = state.gnss.isValid &&
                            (fabs(state.gnss.latitude) > 1e-6 || fabs(state.gnss.longitude) > 1e-6);
    const int32_t liveLatE7 = hasLivePos
                                  ? ((state.gnss.latE7 != 0) ? state.gnss.latE7
                                                             : static_cast<int32_t>(llround(state.gnss.latitude * 1e7)))
                                  : 0;
    const int32_t liveLonE7 = hasLivePos
                                  ? ((state.gnss.lonE7 != 0) ? state.gnss.lonE7
                                                             : static_cast<int32_t>(llround(state.gnss.longitude * 1e7)))
                                  : 0;

    if (_pointCount == 0 && !hasLivePos) {
        // Waiting for GPS anchor empty state
        canvas.drawCircle(Viewport::SAFE_W / 2, Viewport::SAFE_H / 2, 92, 0x162432U);
        canvas.drawCircle(Viewport::SAFE_W / 2, Viewport::SAFE_H / 2, 48, 0x162432U);
        canvas.setFont(&fonts::efontCN_24_b);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.setTextColor(ThemeColors::TEXT_MUTED);
        canvas.drawString("等待 3D 卫星定位生成自驾轨迹...", Viewport::SAFE_W / 2, Viewport::SAFE_H / 2 - 12);
        canvas.setFont(&fonts::efontCN_16);
        canvas.drawString("定位成功后将自动居中铺满绘制全画幅矢量路线", Viewport::SAFE_W / 2, Viewport::SAFE_H / 2 + 26);

        drawCornerHuds(canvas, state, cat, 2.0f);
        return;
    }

    // Determine Bounding Box & Equirectangular Projection Center
    int32_t minLat = 0, maxLat = 0, minLon = 0, maxLon = 0;
    if (_pointCount > 0) {
        minLat = _cachedMeta.minLatE7;
        maxLat = _cachedMeta.maxLatE7;
        minLon = _cachedMeta.minLonE7;
        maxLon = _cachedMeta.maxLonE7;
        if (hasLivePos) {
            if (liveLatE7 < minLat) minLat = liveLatE7;
            if (liveLatE7 > maxLat) maxLat = liveLatE7;
            if (liveLonE7 < minLon) minLon = liveLonE7;
            if (liveLonE7 > maxLon) maxLon = liveLonE7;
        }
    } else {
        minLat = maxLat = liveLatE7;
        minLon = maxLon = liveLonE7;
    }

    double centerLatDeg = 0.0;
    double centerLonDeg = 0.0;

    if (_followMode) {
        if (hasLivePos) {
            centerLatDeg = static_cast<double>(liveLatE7) * 1e-7;
            centerLonDeg = static_cast<double>(liveLonE7) * 1e-7;
        } else {
            centerLatDeg = static_cast<double>(_points[_pointCount - 1].latE7) * 1e-7;
            centerLonDeg = static_cast<double>(_points[_pointCount - 1].lonE7) * 1e-7;
        }
    } else {
        centerLatDeg = 0.5 * static_cast<double>(minLat + maxLat) * 1e-7;
        centerLonDeg = 0.5 * static_cast<double>(minLon + maxLon) * 1e-7;
    }

    const double cosLat = fmax(0.15, cos(centerLatDeg * (M_PI / 180.0)));
    constexpr double DEG_TO_M_LAT = 111132.95;
    const double     degToMLon    = 111319.49 * cosLat;

    // Usable full-bleed safe fitting box with comfortable padding inside 1120x720
    constexpr float FIT_W_PX = 860.0f;
    constexpr float FIT_H_PX = 470.0f;

    float metersPerPx = 1.0f;
    if (_followMode) {
        const float halfSpanM = FOLLOW_HALF_SPAN_M[_followZoomIdx % 3U];
        metersPerPx = (halfSpanM * 2.0f) / FIT_W_PX;
    } else {
        const double spanLatM = fmax(120.0, static_cast<double>(maxLat - minLat) * 1e-7 * DEG_TO_M_LAT);
        const double spanLonM = fmax(160.0, static_cast<double>(maxLon - minLon) * 1e-7 * degToMLon);
        const float mppX = static_cast<float>(spanLonM / FIT_W_PX);
        const float mppY = static_cast<float>(spanLatM / FIT_H_PX);
        metersPerPx = fmaxf(0.25f, fmaxf(mppX, mppY) * 1.12f); // 1:1 physical aspect ratio!
    }

    auto projectX = [&](int32_t lonE7) -> int16_t {
        const double dxM = (static_cast<double>(lonE7) * 1e-7 - centerLonDeg) * degToMLon;
        const float px   = static_cast<float>(Viewport::SAFE_W / 2) + static_cast<float>(dxM) / metersPerPx;
        return static_cast<int16_t>(lroundf(fminf(4000.0f, fmaxf(-2000.0f, px))));
    };

    auto projectY = [&](int32_t latE7) -> int16_t {
        const double dyM = (static_cast<double>(latE7) * 1e-7 - centerLatDeg) * DEG_TO_M_LAT;
        const float py   = static_cast<float>(Viewport::SAFE_H / 2) - static_cast<float>(dyM) / metersPerPx;
        return static_cast<int16_t>(lroundf(fminf(4000.0f, fmaxf(-2000.0f, py))));
    };

    // 1. Render Continuous Vector Trail Segments
    if (_pointCount >= 2 && _points != nullptr) {
        int16_t prevX = projectX(_points[0].lonE7);
        int16_t prevY = projectY(_points[0].latE7);

        // Pass 1: Outer glow + dashed gap detection (long tunnel/DR jumps > 280m * stride rendered dashed)
        const double gapThreshM = 280.0 * fmax(1.0, static_cast<double>(_cachedMeta.decimationStride));

        for (size_t i = 1; i < _pointCount; ++i) {
            const int16_t curX = projectX(_points[i].lonE7);
            const int16_t curY = projectY(_points[i].latE7);

            const double dLatM = fabs(static_cast<double>(_points[i].latE7 - _points[i - 1].latE7) * 1e-7 * DEG_TO_M_LAT);
            const double dLonM = fabs(static_cast<double>(_points[i].lonE7 - _points[i - 1].lonE7) * 1e-7 * degToMLon);
            const double segM  = hypot(dLatM, dLonM);

            if (segM > gapThreshM) {
                // Tunnel / Blind-Zone interpolated span -> Distinct Dashed Cyan/Amber Stroke
                drawDashedSegment(canvas, prevX, prevY, curX, curY, 2.8f, ThemeColors::ICE_CYAN);
            } else {
                // Skip sub-pixel redundant vertices while keeping endpoints crisp
                if (abs(curX - prevX) >= 2 || abs(curY - prevY) >= 2 || i + 1 == _pointCount) {
                    canvas.drawWideLine(prevX, prevY, curX, curY, 6.0f, 0x064532U);
                    canvas.drawWideLine(prevX, prevY, curX, curY, 2.8f, ThemeColors::NEON_GREEN);
                    prevX = curX;
                    prevY = curY;
                    continue;
                }
            }
            prevX = curX;
            prevY = curY;
        }
    }

    // 2. Render Active Live Segment from Last Recorded Trail Point to Current Vehicle Position
    const bool isDrOrTunnel = state.gnss.isDeadReckoning || state.gnss.isInTunnel;
    if (_pointCount >= 1 && _points != nullptr && hasLivePos) {
        const int16_t lx = projectX(_points[_pointCount - 1].lonE7);
        const int16_t ly = projectY(_points[_pointCount - 1].latE7);
        const int16_t vx = projectX(liveLonE7);
        const int16_t vy = projectY(liveLatE7);

        if (isDrOrTunnel) {
            const uint32_t drCol = state.gnss.isInTunnel ? ThemeColors::ICE_CYAN : ThemeColors::AMBER_WARN;
            drawDashedSegment(canvas, lx, ly, vx, vy, 3.0f, drCol);
        } else if (abs(vx - lx) >= 2 || abs(vy - ly) >= 2) {
            canvas.drawWideLine(lx, ly, vx, vy, 2.8f, ThemeColors::NEON_GREEN);
        }
    }

    // 3. Render Start Point Green Flag (Origin)
    if (_cachedMeta.hasOrigin) {
        const int16_t ox = projectX(_cachedMeta.originLonE7);
        const int16_t oy = projectY(_cachedMeta.originLatE7);
        drawStartFlag(canvas, ox, oy);
    } else if (_pointCount > 0 && _points != nullptr) {
        drawStartFlag(canvas, projectX(_points[0].lonE7), projectY(_points[0].latE7));
    }

    // 4. Render Current Vehicle Heading Chevron Arrow
    int16_t vehX = Viewport::SAFE_W / 2;
    int16_t vehY = Viewport::SAFE_H / 2;
    if (hasLivePos) {
        vehX = projectX(liveLonE7);
        vehY = projectY(liveLatE7);
    } else if (_pointCount > 0 && _points != nullptr) {
        vehX = projectX(_points[_pointCount - 1].lonE7);
        vehY = projectY(_points[_pointCount - 1].latE7);
    }
    const float hdg = state.gnss.isValid ? state.gnss.headingDeg : state.imu.fusedHeadingDeg;
    const uint32_t vehCol = state.gnss.isInTunnel
                                ? ThemeColors::ICE_CYAN
                                : (state.gnss.isDeadReckoning ? ThemeColors::AMBER_WARN : ThemeColors::RACING_YELLOW);
    drawVehicleChevron(canvas, vehX, vehY, hdg, vehCol);

    // 5. Render 4-Corner Semi-Transparent Floating HUD Data Cards
    drawCornerHuds(canvas, state, cat, metersPerPx);
}

void Page4Trail::drawCornerHuds(M5Canvas& canvas,
                                const SystemStateSnapshot& state,
                                TripCategory cat,
                                float metersPerPx) {
    const TripStats& stats = state.trips[cat];

    // --- Top-Left HUD: [TRIP/DAY/(SUB)] Tabs + Straight-Line Distance & Bearing to Origin ---
    canvas.fillRectAlpha(HUD_TL_X, HUD_TL_Y, HUD_TL_W, HUD_TL_H, 185, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(HUD_TL_X, HUD_TL_Y, HUD_TL_W, HUD_TL_H, 12, ThemeColors::BORDER_SUBTLE);

    const int tabCount = state.tripControl.manualTripActive ? 3 : 2;
    const TripCategory cats[3] = { TRIP_SINGLE, TRIP_DAILY, TRIP_MANUAL };
    const char* tabNames[3]    = { "单次 TRIP", "当日 DAY", "小记 SUB" };
    const int16_t pillW = (tabCount == 3) ? 128 : 148;

    for (int i = 0; i < tabCount; ++i) {
        const int16_t px = HUD_TL_X + 12 + i * (pillW + 8);
        const int16_t py = HUD_TL_Y + 10;
        const bool sel   = (cat == cats[i]);
        const uint32_t col = (cats[i] == TRIP_MANUAL) ? ThemeColors::RACING_YELLOW : ThemeColors::NEON_GREEN;
        canvas.fillRoundRect(px, py, pillW, 34, 8, sel ? col : 0x162230U);
        canvas.setFont(&fonts::efontCN_14_b);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.setTextColor(sel ? TFT_BLACK : ThemeColors::TEXT_WHITE);
        canvas.drawString(tabNames[i], px + pillW / 2, py + 17);
    }

    float distToStartKm = _cachedMeta.straightDistToStartKm;
    float retBearing    = _cachedMeta.returnBearingDeg;
    if (distToStartKm <= 0.0001f && _cachedMeta.hasOrigin && state.gnss.isValid) {
        const double oLat = static_cast<double>(_cachedMeta.originLatE7) * 1e-7;
        const double oLon = static_cast<double>(_cachedMeta.originLonE7) * 1e-7;
        distToStartKm = static_cast<float>(computeHaversineKm(state.gnss.latitude, state.gnss.longitude, oLat, oLon));
        retBearing    = computeBearingDeg(state.gnss.latitude, state.gnss.longitude, oLat, oLon);
    }

    char originInfo[80];
    snprintf(originInfo, sizeof(originInfo), "距起点直线: %.2f km   返程方位: %03d°",
             static_cast<double>(distToStartKm),
             static_cast<int>(lroundf(retBearing)) % 360);
    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextDatum(textdatum_t::middle_left);
    canvas.setTextColor(ThemeColors::ICE_CYAN);
    canvas.drawString(originInfo, HUD_TL_X + 14, HUD_TL_Y + 74);

    // --- Top-Right HUD: One-Tap View Mode Switcher (Overview <-> Vehicle Follow) & North Badge ---
    canvas.fillRectAlpha(HUD_TR_X, HUD_TR_Y, HUD_TR_W, HUD_TR_H, 185, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(HUD_TR_X, HUD_TR_Y, HUD_TR_W, HUD_TR_H, 12,
                         _followMode ? ThemeColors::ICE_CYAN : ThemeColors::NEON_GREEN);

    // North-up compass needle icon on left of TR HUD
    const int16_t ncx = HUD_TR_X + 34;
    const int16_t ncy = HUD_TR_Y + HUD_TR_H / 2;
    canvas.drawCircle(ncx, ncy, 22, ThemeColors::BORDER_SUBTLE);
    canvas.fillTriangle(ncx, ncy - 17, ncx - 6, ncy + 2, ncx + 6, ncy + 2, ThemeColors::ALERT_RED);
    canvas.fillTriangle(ncx, ncy + 17, ncx - 6, ncy + 2, ncx + 6, ncy + 2, ThemeColors::TEXT_MUTED);
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextDatum(textdatum_t::bottom_center);
    canvas.setTextColor(ThemeColors::ALERT_RED);
    canvas.drawString("N", ncx, ncy - 22);

    canvas.setFont(&fonts::efontCN_16_b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(_followMode ? ThemeColors::ICE_CYAN : ThemeColors::NEON_GREEN);
    canvas.drawString(_followMode ? "车头居中跟随模式" : "全画幅自适应全览", HUD_TR_X + 68, HUD_TR_Y + 16);

    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    if (_followMode) {
        static constexpr const char* ZOOM_LBL[3] = { "轻触切换: 近距视野", "轻触切换: 标准视野", "轻触切换: 广域视野" };
        canvas.drawString(ZOOM_LBL[_followZoomIdx % 3U], HUD_TR_X + 68, HUD_TR_Y + 46);
        canvas.drawString("再次轻触切回全览", HUD_TR_X + 68, HUD_TR_Y + 68);
    } else {
        canvas.drawString("轻触切换为车头居中", HUD_TR_X + 68, HUD_TR_Y + 48);
        canvas.drawString("1:1 真实等比例缩放", HUD_TR_X + 68, HUD_TR_Y + 70);
    }

    // --- Bottom-Left HUD: Trip Distance, Moving Duration & Point Count ---
    canvas.fillRectAlpha(HUD_BL_X, HUD_BL_Y, HUD_BL_W, HUD_BL_H, 185, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(HUD_BL_X, HUD_BL_Y, HUD_BL_W, HUD_BL_H, 12, ThemeColors::BORDER_SUBTLE);

    char distStr[32];
    snprintf(distStr, sizeof(distStr), "%.2f km", static_cast<double>(stats.tripDistanceKm));
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.drawString(distStr, HUD_BL_X + 16, HUD_BL_Y + 14);

    const uint32_t hh = stats.movingDurationSec / 3600UL;
    const uint32_t mm = (stats.movingDurationSec % 3600UL) / 60UL;
    const uint32_t ss = stats.movingDurationSec % 60UL;
    char blSub[64];
    snprintf(blSub, sizeof(blSub), "行驶 %02lu:%02lu:%02lu  |  轨迹点 %u (x%u)",
             static_cast<unsigned long>(hh),
             static_cast<unsigned long>(mm),
             static_cast<unsigned long>(ss),
             static_cast<unsigned>(_pointCount),
             static_cast<unsigned>(_cachedMeta.decimationStride));
    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(ThemeColors::TEXT_MUTED);
    canvas.drawString(blSub, HUD_BL_X + 16, HUD_BL_Y + 60);

    // --- Bottom-Right HUD: Live Speed, Altitude, DR/Tunnel Badge & Metric Scale Bar ---
    canvas.fillRectAlpha(HUD_BR_X, HUD_BR_Y, HUD_BR_W, HUD_BR_H, 185, ThemeColors::BG_CARD_DARK);
    canvas.drawRoundRect(HUD_BR_X, HUD_BR_Y, HUD_BR_W, HUD_BR_H, 12, ThemeColors::BORDER_SUBTLE);

    char spdAlt[48];
    snprintf(spdAlt, sizeof(spdAlt), "%.0f km/h   %.0f m",
             static_cast<double>(fmaxf(0.0f, state.gnss.speedKmh)),
             static_cast<double>(state.baro.fusedAltitudeM));
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(ThemeColors::TEXT_WHITE);
    canvas.drawString(spdAlt, HUD_BR_X + 16, HUD_BR_Y + 14);

    if (state.gnss.isInTunnel || state.gnss.isDeadReckoning) {
        canvas.setFont(&fonts::efontCN_12_b);
        canvas.setTextDatum(textdatum_t::top_right);
        canvas.setTextColor(state.gnss.isInTunnel ? ThemeColors::ICE_CYAN : ThemeColors::AMBER_WARN);
        canvas.drawString(state.gnss.isInTunnel ? "隧道虚线导航" : "DR 盲区虚线",
                          HUD_BR_X + HUD_BR_W - 14, HUD_BR_Y + 18);
    }

    // Nice snapped scale bar (target width ~ 80..140 px)
    static constexpr float NICE_SCALES_M[] = {
        20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f, 50000.0f, 100000.0f
    };
    float chosenM = 100.0f;
    for (size_t i = 0; i < sizeof(NICE_SCALES_M) / sizeof(NICE_SCALES_M[0]); ++i) {
        chosenM = NICE_SCALES_M[i];
        if ((chosenM / fmaxf(0.05f, metersPerPx)) >= 72.0f) {
            break;
        }
    }
    int16_t scalePx = static_cast<int16_t>(lroundf(chosenM / fmaxf(0.05f, metersPerPx)));
    if (scalePx < 44)  scalePx = 44;
    if (scalePx > 150) scalePx = 150;

    const int16_t sbRight = HUD_BR_X + HUD_BR_W - 18;
    const int16_t sbLeft  = sbRight - scalePx;
    const int16_t sbY     = HUD_BR_Y + 76;

    canvas.drawFastHLine(sbLeft, sbY, scalePx, ThemeColors::NEON_GREEN);
    canvas.drawFastHLine(sbLeft, sbY + 1, scalePx, ThemeColors::NEON_GREEN);
    canvas.drawFastVLine(sbLeft, sbY - 6, 8, ThemeColors::NEON_GREEN);
    canvas.drawFastVLine(sbRight, sbY - 6, 8, ThemeColors::NEON_GREEN);

    char scaleLbl[24];
    if (chosenM >= 1000.0f) {
        snprintf(scaleLbl, sizeof(scaleLbl), "%.0f km", static_cast<double>(chosenM / 1000.0f));
    } else {
        snprintf(scaleLbl, sizeof(scaleLbl), "%.0f m", static_cast<double>(chosenM));
    }
    canvas.setFont(&fonts::efontCN_14_b);
    canvas.setTextDatum(textdatum_t::middle_right);
    canvas.setTextColor(ThemeColors::NEON_GREEN);
    canvas.drawString(scaleLbl, sbLeft - 10, sbY - 2);
}

bool Page4Trail::onTouch(const TouchEvent& event, const SystemStateSnapshot& state) {
    if (event.type != TOUCH_EVT_TAP) {
        return false;
    }

    // 1. Top-Left HUD [TRIP / DAY / (SUB)] Dimension Switcher
    if (event.hitTestLocal(HUD_TL_X, HUD_TL_Y, HUD_TL_W, 54)) {
        const int tabCount = state.tripControl.manualTripActive ? 3 : 2;
        const TripCategory cats[3] = { TRIP_SINGLE, TRIP_DAILY, TRIP_MANUAL };
        const int16_t pillW = (tabCount == 3) ? 128 : 148;

        for (int i = 0; i < tabCount; ++i) {
            const int16_t px = HUD_TL_X + 12 + i * (pillW + 8);
            if (event.hitTestLocal(px, HUD_TL_Y + 6, pillW, 42)) {
                playFeedbackTone(state);
                StateGuard guard(SystemState::instance());
                if (guard.locked()) {
                    SystemState::instance().data().tripControl.activeViewCategory = cats[i];
                }
                return true;
            }
        }
    }

    // 2. Top-Right HUD View Mode Toggle: Overview Auto-Fit -> Follow Near -> Follow Std -> Follow Wide -> Overview
    if (event.hitTestLocal(HUD_TR_X, HUD_TR_Y, HUD_TR_W, HUD_TR_H)) {
        playFeedbackTone(state);
        if (!_followMode) {
            _followMode    = true;
            _followZoomIdx = 1; // Start at balanced 650m follow view
        } else if (_followZoomIdx == 1) {
            _followZoomIdx = 0; // Zoom in 250m
        } else if (_followZoomIdx == 0) {
            _followZoomIdx = 2; // Zoom out 1800m
        } else {
            _followMode    = false;
            _followZoomIdx = 1;
        }
        return true;
    }

    return false;
}
