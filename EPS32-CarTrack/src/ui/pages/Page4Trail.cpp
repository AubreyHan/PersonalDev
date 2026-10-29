#include "ui/pages/Page4Trail.h"
#include "config.h"
#include <math.h>

void Page4Trail::draw(M5Canvas& canvas, const SystemContext& ctx) {
    // 1. Top HUD Bar
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
    canvas.drawString("VECTOR TRAIL & RETURN", 8, 28);

    // Mode Toggle Button (X: 200~312, Y: 26~46)
    canvas.fillRoundRect(200, 26, 112, 20, 3, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(200, 26, 112, 20, 3, UiColors::COLOR_PRIMARY);
    canvas.setTextColor(UiColors::COLOR_PRIMARY, UiColors::COLOR_PANEL_BG);
    if (ctx.trail.viewMode == TRAIL_VIEW_FIT_ENTIRE) {
        canvas.drawCenterString("⛶ 远景全貌", 256, 30);
    } else {
        canvas.drawCenterString("🔄 5km 跟随", 256, 30);
    }

    // Distance to Start HUD
    char distBuf[32];
    snprintf(distBuf, sizeof(distBuf), "🚩 距起点: %.1f km", ctx.trail.distanceToStartKm);
    canvas.setTextColor(UiColors::COLOR_SECONDARY, UiColors::COLOR_BG);
    canvas.drawString(distBuf, 8, 44);

    // 2. Vector Canvas Viewport: X: 8~312, Y: 58~200 (width 304, height 142)
    int cvsX = 8;
    int cvsY = 58;
    int cvsW = 304;
    int cvsH = 142;

    canvas.drawRect(cvsX, cvsY, cvsW, cvsH, UiColors::COLOR_BORDER);

    size_t count = ctx.trail.count;
    if (count > 0) {
        int32_t minLat = ctx.trail.minLatE7;
        int32_t maxLat = ctx.trail.maxLatE7;
        int32_t minLon = ctx.trail.minLonE7;
        int32_t maxLon = ctx.trail.maxLonE7;

        if (maxLat == minLat) { maxLat += 1000; minLat -= 1000; }
        if (maxLon == minLon) { maxLon += 1000; minLon -= 1000; }

        auto projectX = [&](int32_t lon) -> int {
            return cvsX + 10 + (int)(((double)(lon - minLon) / (maxLon - minLon)) * (cvsW - 20));
        };
        auto projectY = [&](int32_t lat) -> int {
            return cvsY + cvsH - 10 - (int)(((double)(lat - minLat) / (maxLat - minLat)) * (cvsH - 20));
        };

        // Draw trail polyline
        if (count >= 2) {
            int prevX = projectX(ctx.trail.points[0].lonE7);
            int prevY = projectY(ctx.trail.points[0].latE7);

            for (size_t i = 1; i < count; i++) {
                int px = projectX(ctx.trail.points[i].lonE7);
                int py = projectY(ctx.trail.points[i].latE7);
                canvas.drawLine(prevX, prevPy, px, py, UiColors::COLOR_PRIMARY);
                prevX = px;
                prevPy = py;
            }
        }

        // Start Pin (Green flag circle)
        int startX = projectX(ctx.trail.points[0].lonE7);
        int startY = projectY(ctx.trail.points[0].latE7);
        canvas.fillCircle(startX, startY, 4, UiColors::COLOR_PRIMARY);
        canvas.drawCircle(startX, startY, 6, UiColors::COLOR_WHITE);

        // Current Vehicle Triangle
        int curX = projectX(ctx.gnss.latE7 > 0 ? ctx.gnss.lonE7 : ctx.trail.points[count - 1].lonE7);
        int curY = projectY(ctx.gnss.latE7 > 0 ? ctx.gnss.latE7 : ctx.trail.points[count - 1].latE7);

        float rad = ctx.gnss.headingDeg * 0.01745329f;
        int tipX = curX + (int)(8 * sin(rad));
        int tipY = curY - (int)(8 * cos(rad));
        int leftX = curX - (int)(6 * cos(rad));
        int leftY = curY - (int)(6 * sin(rad));
        int rightX = curX + (int)(6 * cos(rad));
        int rightY = curY + (int)(6 * sin(rad));

        canvas.fillTriangle(tipX, tipY, leftX, leftY, rightX, rightY, UiColors::COLOR_SECONDARY);
    } else {
        canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
        canvas.drawCenterString("行车起步后自动绘制矢量轨迹", 160, 120);
    }

    // 3. Bottom Status Info
    char bottomBuf[64];
    snprintf(bottomBuf, sizeof(bottomBuf), "HDOP: %.1f | 航点: %d/800", ctx.gnss.hdop, (int)ctx.trail.count);
    canvas.setTextColor(UiColors::COLOR_MUTED, UiColors::COLOR_BG);
    canvas.drawString(bottomBuf, 10, 206);
}

void Page4Trail::handleTap(int16_t x, int16_t y) {
    // Mode toggle button (X: 190~315, Y: 24~50)
    if (x >= 190 && x <= 315 && y >= 24 && y <= 50) {
        SystemState& state = SystemState::instance();
        state.lock();
        if (state.getContextUnsafe().trail.viewMode == TRAIL_VIEW_FIT_ENTIRE) {
            state.getContextUnsafe().trail.viewMode = TRAIL_VIEW_FOLLOW_5KM;
        } else {
            state.getContextUnsafe().trail.viewMode = TRAIL_VIEW_FIT_ENTIRE;
        }
        state.unlock();
    }
}
