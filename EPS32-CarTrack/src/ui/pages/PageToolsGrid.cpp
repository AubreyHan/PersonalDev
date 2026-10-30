#include "ui/pages/PageToolsGrid.h"
#include <math.h>

void PageToolsGrid::render(M5Canvas& canvas, const SystemContext& ctx) {
    beginViewport(canvas);

    const int cardW = 520, cardH = 300;
    const int posX[4] = {190, 730, 190, 730};
    const int posY[4] = {24,  24,  344, 344};

    // Card 1: TF Card Storage & QR Wireless Export
    canvas.fillRoundRect(posX[0], posY[0], cardW, cardH, 12, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(posX[0], posY[0], cardW, cardH, 12, UiColors::COLOR_PRIMARY);
    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.drawString("1. TF Card & Wi-Fi QR Export", posX[0] + 24, posY[0] + 24);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    char tfSt[64], tfCap[64], tfCnt[64];
    snprintf(tfSt,  sizeof(tfSt),  "Status : %s", ctx.storage.isMounted ? "Mounted (FAT32)" : "Standby / No Card");
    float totGb  = ctx.storage.totalBytes / (1024.0f * 1024.0f * 1024.0f);
    float trkMb  = ctx.storage.tracksDirBytes / (1024.0f * 1024.0f);
    snprintf(tfCap, sizeof(tfCap), "Tracks : %.1f MB (50%% Quota of %.1fGB)", trkMb, totGb);
    snprintf(tfCnt, sizeof(tfCnt), "Pairs  : %u W/C Dual GPX Tracks", ctx.storage.trackFilePairsCount);
    canvas.drawString(tfSt,  posX[0] + 24, posY[0] + 80);
    canvas.drawString(tfCap, posX[0] + 24, posY[0] + 125);
    canvas.drawString(tfCnt, posX[0] + 24, posY[0] + 170);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.drawString("[Tap -> QR Code Share / Format]", posX[0] + 24, posY[0] + 235);

    // Card 2: Wi-Fi 6 Network Manager
    canvas.fillRoundRect(posX[1], posY[1], cardW, cardH, 12, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(posX[1], posY[1], cardW, cardH, 12, UiColors::COLOR_BLUE);
    canvas.setTextColor(UiColors::COLOR_BLUE);
    canvas.drawString("2. Wi-Fi 6 & NTP Manager", posX[1] + 24, posY[1] + 24);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    char wfSt[64], wfIp[64], wfSv[64];
    snprintf(wfSt, sizeof(wfSt), "Status : %s", ctx.wifi.isConnected ? ctx.wifi.ssid : "Disconnected");
    snprintf(wfIp, sizeof(wfIp), "IP Addr: %s", ctx.wifi.isConnected ? ctx.wifi.ipAddr : "0.0.0.0");
    snprintf(wfSv, sizeof(wfSv), "Saved  : %u / 5 Multi-SSID Slots", ctx.wifi.savedNetworkCount);
    canvas.drawString(wfSt, posX[1] + 24, posY[1] + 80);
    canvas.drawString(wfIp, posX[1] + 24, posY[1] + 125);
    canvas.drawString(wfSv, posX[1] + 24, posY[1] + 170);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.drawString("[Tap -> Saved List / QR Add Wi-Fi]", posX[1] + 24, posY[1] + 235);

    // Card 3: GNSS Satellite & RF Diagnostics
    canvas.fillRoundRect(posX[2], posY[2], cardW, cardH, 12, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(posX[2], posY[2], cardW, cardH, 12, UiColors::COLOR_SECONDARY);
    canvas.setTextColor(UiColors::COLOR_SECONDARY);
    canvas.drawString("3. GNSS & RF Diagnostics", posX[2] + 24, posY[2] + 24);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    char gnFix[64], gnSat[64], gnRf[64];
    snprintf(gnFix, sizeof(gnFix), "Fix    : %s (HDOP %.2f)", ctx.gnss.isValid ? "3D Fix" : "Searching", ctx.gnss.hdop);
    snprintf(gnSat, sizeof(gnSat), "Sats   : %u (BDS:%u GPS:%u GLO:%u)", ctx.gnss.numSats, ctx.gnss.numBdsSats, ctx.gnss.numGpsSats, ctx.gnss.numGloSats);
    snprintf(gnRf,  sizeof(gnRf),  "Antenna: %s | Jamming: %u", ctx.gnss.antennaStatus == 2 ? "OPEN!" : "Normal", ctx.gnss.jammingState);
    canvas.drawString(gnFix, posX[2] + 24, posY[2] + 80);
    canvas.drawString(gnSat, posX[2] + 24, posY[2] + 125);
    canvas.drawString(gnRf,  posX[2] + 24, posY[2] + 170);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.drawString("[Tap -> Skyplot & Hot-Restart]", posX[2] + 24, posY[2] + 235);

    // Card 4: System Preferences & INA226 Power Telemetry
    canvas.fillRoundRect(posX[3], posY[3], cardW, cardH, 12, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(posX[3], posY[3], cardW, cardH, 12, UiColors::COLOR_WARN);
    canvas.setTextColor(UiColors::COLOR_WARN);
    canvas.drawString("4. System & INA226 Gauge", posX[3] + 24, posY[3] + 24);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    char pwV[64], pwI[64], pwB[64];
    snprintf(pwV, sizeof(pwV), "NP-F550: %.2f V | %.0f%% (%.0f mAh)", ctx.power.busVoltageV, ctx.power.batterySocPercent, ctx.power.remainingMah);
    snprintf(pwI, sizeof(pwI), "Current: %+.1f mA | %.0f mW", ctx.power.currentMa, ctx.power.powerMw);
    snprintf(pwB, sizeof(pwB), "Bright : %u / 255 | ESP32-P4 32MB", ctx.screenBrightness);
    canvas.drawString(pwV, posX[3] + 24, posY[3] + 80);
    canvas.drawString(pwI, posX[3] + 24, posY[3] + 125);
    canvas.drawString(pwB, posX[3] + 24, posY[3] + 170);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.drawString("[Tap -> Brightness & Power Info]", posX[3] + 24, posY[3] + 235);

    endViewport(canvas);
}

bool PageToolsGrid::onTouchTap(int16_t x, int16_t y, SystemContext& ctx) {
    const int cardW = 520, cardH = 300;
    if (x >= 190 && x <= 190 + cardW && y >= 24 && y <= 24 + cardH) {
        ctx.currentPage = SUBPAGE_TOOL_TF;
        return true;
    }
    if (x >= 730 && x <= 730 + cardW && y >= 24 && y <= 24 + cardH) {
        ctx.currentPage = SUBPAGE_TOOL_WIFI;
        return true;
    }
    if (x >= 190 && x <= 190 + cardW && y >= 344 && y <= 344 + cardH) {
        ctx.currentPage = SUBPAGE_TOOL_GNSS;
        return true;
    }
    if (x >= 730 && x <= 730 + cardW && y >= 344 && y <= 344 + cardH) {
        ctx.currentPage = SUBPAGE_TOOL_SYS;
        return true;
    }
    return false;
}
