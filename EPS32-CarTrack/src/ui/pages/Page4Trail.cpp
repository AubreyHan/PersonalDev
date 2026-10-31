#include "ui/pages/Page4Trail.h"
#include "driver/HapticDriver.h"
#include <math.h>

void Page4Trail::render(M5Canvas& canvas, const SystemContext& ctx) {
    beginViewport(canvas);

    if (!_renderCache) {
        _renderCache = (TrailPoint*)ps_malloc(sizeof(TrailPoint) * SystemState::MAX_TRAIL_POINTS);
    }

    size_t count = 0;
    if (_renderCache) {
        count = SystemState::instance().copyTrailPoints(_renderCache, SystemState::MAX_TRAIL_POINTS);
    }

    // Full-Bleed Dynamic Auto-Fitting inside 1120x720 (with 24px margin)
    int mapX = Viewport::SAFE_X + 24;
    int mapY = 24;
    int mapW = Viewport::SAFE_W - 48;
    int mapH = Viewport::SAFE_H - 48;

    if (count >= 2 && _renderCache) {
        double minLat = _renderCache[0].lat, maxLat = _renderCache[0].lat;
        double minLon = _renderCache[0].lon, maxLon = _renderCache[0].lon;

        if (_followMode5Km) {
            double curLat = _renderCache[count - 1].lat;
            double curLon = _renderCache[count - 1].lon;
            minLat = curLat - 0.022; maxLat = curLat + 0.022;
            minLon = curLon - 0.025; maxLon = curLon + 0.025;
        } else {
            for (size_t i = 1; i < count; i++) {
                if (_renderCache[i].lat < minLat) minLat = _renderCache[i].lat;
                if (_renderCache[i].lat > maxLat) maxLat = _renderCache[i].lat;
                if (_renderCache[i].lon < minLon) minLon = _renderCache[i].lon;
                if (_renderCache[i].lon > maxLon) maxLon = _renderCache[i].lon;
            }
        }

        double spanLat = maxLat - minLat;
        double spanLon = maxLon - minLon;
        if (spanLat < 0.0005) { minLat -= 0.00025; maxLat += 0.00025; spanLat = 0.0005; }
        if (spanLon < 0.0005) { minLon -= 0.00025; maxLon += 0.00025; spanLon = 0.0005; }

        auto toScreen = [&](double lat, double lon, int& sx, int& sy) {
            sx = mapX + (int)(((lon - minLon) / spanLon) * mapW);
            sy = mapY + mapH - (int)(((lat - minLat) / spanLat) * mapH);
        };

        for (size_t i = 1; i < count; i++) {
            int x0, y0, x1, y1;
            toScreen(_renderCache[i - 1].lat, _renderCache[i - 1].lon, x0, y0);
            toScreen(_renderCache[i].lat,     _renderCache[i].lon,     x1, y1);
            canvas.drawLine(x0, y0, x1, y1, UiColors::COLOR_PRIMARY);
        }

        // Start Green Flag Marker
        int sx0, sy0, sxCur, syCur;
        toScreen(_renderCache[0].lat, _renderCache[0].lon, sx0, sy0);
        toScreen(_renderCache[count - 1].lat, _renderCache[count - 1].lon, sxCur, syCur);
        canvas.fillCircle(sx0, sy0, 7, UiColors::COLOR_PRIMARY);
        canvas.fillCircle(sxCur, syCur, 8, UiColors::COLOR_SECONDARY);
    } else {
        // Center placeholder crosshair when stationary/demo
        int cx = Viewport::SAFE_X + Viewport::SAFE_W / 2;
        int cy = Viewport::SAFE_H / 2;
        canvas.drawCircle(cx, cy, 120, UiColors::COLOR_BORDER);
        canvas.fillCircle(cx, cy, 8, UiColors::COLOR_PRIMARY);
        canvas.setTextDatum(top_center);
        canvas.setTextSize(2);
        canvas.setTextColor(UiColors::COLOR_MUTED);
        canvas.drawString("Waiting for vehicle movement (>3 km/h) to plot vector trail", cx, cy + 145);
    }

    // 4 Corner Floating HUD Capsules (No bottom bar so trail fills 100% height)
    double distStartKm = 0.0;
    float  retBearing  = 0.0f;
    if (ctx.trip.hasStartAnchor && (ctx.gnss.isValid || ctx.gnss.isDeadReckoning)) {
        double dLatM = (ctx.trip.startLatitude  - ctx.gnss.latitude)  * 111139.0;
        double dLonM = (ctx.trip.startLongitude - ctx.gnss.longitude) * 111139.0 * cos(ctx.gnss.latitude * 0.0174532925);
        distStartKm = sqrt(dLatM * dLatM + dLonM * dLonM) * 0.001;
        retBearing  = fmodf(atan2f((float)dLonM, (float)dLatM) * 57.29578f + 360.0f, 360.0f);
    }

    // Top-Left Capsule: Straight-line distance & return compass bearing to Start
    canvas.fillRoundRect(180, 18, 430, 46, 23, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(180, 18, 430, 46, 23, UiColors::COLOR_PRIMARY);
    canvas.setTextDatum(middle_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    char tlBuf[64];
    snprintf(tlBuf, sizeof(tlBuf), "  To Start: %.2f km (%03.0f deg)", distStartKm, retBearing);
    canvas.drawString(tlBuf, 195, 41);

    // Top-Center Capsule: Dynamic Interactive Trip View Switcher [TRIP单次] [DAY单日] (and [SUB小记] when active)
    canvas.fillRoundRect(630, 18, 275, 46, 23, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(630, 18, 275, 46, 23, UiColors::COLOR_BORDER);

    auto drawTrailTab = [&](int x, int w, const char* label, bool active) {
        if (active) {
            canvas.fillRoundRect(x, 24, w, 34, 17, 0x0208);
            canvas.drawRoundRect(x, 24, w, 34, 17, UiColors::COLOR_PRIMARY);
            canvas.setTextColor(UiColors::COLOR_PRIMARY);
        } else {
            canvas.fillRoundRect(x, 24, w, 34, 17, UiColors::COLOR_CARD_BG);
            canvas.setTextColor(UiColors::COLOR_MUTED);
        }
        canvas.setTextDatum(middle_center);
        canvas.setTextSize(2);
        canvas.drawString(label, x + w / 2, 41);
    };

    if (!ctx.manualTripActive) {
        drawTrailTab(638, 126, "TRIP单次", ctx.activeTripCategory == TRIP_SINGLE);
        drawTrailTab(770, 126, "DAY单日",  ctx.activeTripCategory == TRIP_DAILY);
    } else {
        drawTrailTab(636, 84, "TRIP单次", ctx.activeTripCategory == TRIP_SINGLE);
        drawTrailTab(725, 84, "DAY单日",  ctx.activeTripCategory == TRIP_DAILY);
        drawTrailTab(814, 84, "SUB小记",  ctx.activeTripCategory == TRIP_MANUAL);
    }

    // Top-Right Capsule: Full-Bleed Panorama vs 5km Follow Toggle
    canvas.fillRoundRect(920, 18, 340, 46, 23, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(920, 18, 340, 46, 23, UiColors::COLOR_SECONDARY);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.drawString(_followMode5Km ? "Mode: 5km Follow" : "Mode: Full Auto-Fit", 1090, 41);

    // Bottom-Left Capsule: Cumulative Trip & Trail Memory Points
    canvas.fillRoundRect(180, 654, 460, 46, 23, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(180, 654, 460, 46, 23, UiColors::COLOR_BORDER);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    char blBuf[64];
    snprintf(blBuf, sizeof(blBuf), "  Trip: %.2f km | Pts: %u", ctx.trip.tripDistanceKm, (unsigned)count);
    canvas.drawString(blBuf, 195, 677);

    // Bottom-Right Capsule: Current Heading & Tunnel Status
    canvas.fillRoundRect(920, 654, 340, 46, 23, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(920, 654, 340, 46, 23, UiColors::COLOR_BORDER);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(ctx.gnss.isInTunnel ? UiColors::COLOR_WARN : UiColors::COLOR_PRIMARY);
    char brBuf[48];
    if (ctx.gnss.isInTunnel) {
        snprintf(brBuf, sizeof(brBuf), "Tunnel: %.1f/%.1fkm", ctx.gnss.tunnelProgressKm, ctx.gnss.tunnelLengthKm);
    } else {
        snprintf(brBuf, sizeof(brBuf), "Heading: %03.0f deg", ctx.imu.fusedHeadingDeg);
    }
    canvas.drawString(brBuf, 1090, 677);

    endViewport(canvas);
}

bool Page4Trail::onTouchTap(int16_t x, int16_t y, SystemContext& ctx) {
    // Top-Center Trip View Switcher Capsule (X: 630..905, Y: 18..64)
    if (y >= 18 && y <= 64) {
        if (!ctx.manualTripActive) {
            if (x >= 635 && x <= 765) {
                SystemState::instance().switchActiveTripCategory(TRIP_SINGLE);
                HapticDriver::instance().triggerShortPulse(50);
                return true;
            }
            if (x >= 765 && x <= 900) {
                SystemState::instance().switchActiveTripCategory(TRIP_DAILY);
                HapticDriver::instance().triggerShortPulse(50);
                return true;
            }
        } else {
            if (x >= 635 && x <= 722) {
                SystemState::instance().switchActiveTripCategory(TRIP_SINGLE);
                HapticDriver::instance().triggerShortPulse(50);
                return true;
            }
            if (x >= 722 && x <= 810) {
                SystemState::instance().switchActiveTripCategory(TRIP_DAILY);
                HapticDriver::instance().triggerShortPulse(50);
                return true;
            }
            if (x >= 810 && x <= 900) {
                SystemState::instance().switchActiveTripCategory(TRIP_MANUAL);
                HapticDriver::instance().triggerShortPulse(50);
                return true;
            }
        }
    }

    // Top-Right Mode Toggle Capsule (X: 920..1260, Y: 18..64)
    if (x >= 920 && x <= 1260 && y >= 18 && y <= 64) {
        _followMode5Km = !_followMode5Km;
        return true;
    }
    return false;
}
