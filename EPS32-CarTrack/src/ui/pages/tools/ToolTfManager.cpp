#include "ui/pages/tools/ToolTfManager.h"
#include "service/NetworkManager.h"
#include "service/TrackLogger.h"

void ToolTfManager::render(M5Canvas& canvas, const SystemContext& ctx) {
    beginViewport(canvas);

    // Top Header
    canvas.fillRoundRect(185, 20, 1070, 72, 10, UiColors::COLOR_PANEL_BG);
    canvas.setTextDatum(middle_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.drawString("  Tool 1: TF Card Storage & Wireless QR Track Download", 205, 56);

    // Back Button (stops Share AP automatically on exit)
    canvas.fillRoundRect(1080, 32, 155, 48, 8, UiColors::COLOR_DANGER);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString("Back", 1157, 56);

    if (ctx.wifi.apShareActive) {
        // Dual QR Code View for "M5Track-Share" Wireless Track Download
        canvas.fillRoundRect(185, 110, 520, 575, 12, UiColors::COLOR_PANEL_BG);
        canvas.drawRoundRect(185, 110, 520, 575, 12, UiColors::COLOR_PRIMARY);
        canvas.setTextDatum(top_center);
        canvas.setTextColor(UiColors::COLOR_PRIMARY);
        canvas.setTextSize(2);
        canvas.drawString("STEP 1: Scan to Join Share Wi-Fi", 445, 130);
        canvas.setTextSize(1);
        canvas.setTextColor(UiColors::COLOR_WHITE);
        canvas.drawString("Hotspot: M5Track-Share (No Password)", 445, 165);

        canvas.fillRect(305, 195, 280, 280, UiColors::COLOR_WHITE);
        canvas.qrcode("WIFI:T:nopass;S:M5Track-Share;;", 320, 210, 250, 6);

        canvas.setTextColor(UiColors::COLOR_SECONDARY);
        canvas.setTextSize(2);
        canvas.drawString("Auto-Pops Track List!", 445, 500);
        canvas.setTextSize(1);
        canvas.setTextColor(UiColors::COLOR_MUTED);
        canvas.drawString("Captive Portal opens download page on phone", 445, 535);

        // Step 2 Card: Direct URL QR (http://192.168.4.1/tracks)
        canvas.fillRoundRect(730, 110, 525, 575, 12, UiColors::COLOR_PANEL_BG);
        canvas.drawRoundRect(730, 110, 525, 575, 12, UiColors::COLOR_SECONDARY);
        canvas.setTextDatum(top_center);
        canvas.setTextColor(UiColors::COLOR_SECONDARY);
        canvas.setTextSize(2);
        canvas.drawString("STEP 2: Or Scan to Open Tracks", 992, 130);
        canvas.setTextSize(1);
        canvas.setTextColor(UiColors::COLOR_WHITE);
        canvas.drawString("URL: http://192.168.4.1/tracks", 992, 165);

        canvas.fillRect(852, 195, 280, 280, UiColors::COLOR_WHITE);
        canvas.qrcode("http://192.168.4.1/tracks", 867, 210, 250, 6);

        // Stop Share Hotspot Button
        canvas.fillRoundRect(790, 515, 405, 64, 10, UiColors::COLOR_DANGER);
        canvas.setTextDatum(middle_center);
        canvas.setTextSize(2);
        canvas.setTextColor(UiColors::COLOR_WHITE);
        canvas.drawString("Stop Wi-Fi Share Hotspot", 992, 547);
    } else {
        // Storage Overview + Action Buttons
        canvas.fillRoundRect(185, 110, 1070, 320, 12, UiColors::COLOR_PANEL_BG);
        canvas.drawRoundRect(185, 110, 1070, 320, 12, UiColors::COLOR_BORDER);

        canvas.setTextDatum(top_left);
        canvas.setTextSize(2);
        canvas.setTextColor(UiColors::COLOR_WHITE);
        float totGb = ctx.storage.totalBytes / (1024.0f * 1024.0f * 1024.0f);
        float trkMb = ctx.storage.tracksDirBytes / (1024.0f * 1024.0f);
        char line1[80], line2[80], line3[80], line4[80];
        snprintf(line1, sizeof(line1), "Card Status     : %s", ctx.storage.isMounted ? "MOUNTED (FAT32 LFN Ready)" : "NOT MOUNTED");
        snprintf(line2, sizeof(line2), "Tracks Storage  : %.2f MB used (Strict 50%% Quota of %.1f GB Card)", trkMb, totGb);
        snprintf(line3, sizeof(line3), "Recorded Pairs  : %u Pairs (WGS-84 'W-' + GCJ-02 'C-' GPX)", ctx.storage.trackFilePairsCount);
        snprintf(line4, sizeof(line4), "Active Segment  : %s (%u pts)", ctx.storage.isRecording ? ctx.storage.currentWgsFile : "Idle", ctx.storage.currentSegmentPoints);

        canvas.drawString(line1, 215, 140);
        canvas.drawString(line2, 215, 195);
        canvas.drawString(line3, 215, 250);
        canvas.setTextColor(UiColors::COLOR_PRIMARY);
        canvas.drawString(line4, 215, 305);

        // Button 1: Start Wi-Fi QR Code Export (M5Track-Share)
        canvas.fillRoundRect(185, 460, 520, 110, 12, 0x0208);
        canvas.drawRoundRect(185, 460, 520, 110, 12, UiColors::COLOR_PRIMARY);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(UiColors::COLOR_PRIMARY);
        canvas.drawString("Wi-Fi QR Code Track Download", 445, 515);

        // Button 2: One-Click Format FAT32
        canvas.fillRoundRect(735, 460, 520, 110, 12, UiColors::COLOR_PANEL_BG);
        canvas.drawRoundRect(735, 460, 520, 110, 12, UiColors::COLOR_WARN);
        canvas.setTextColor(UiColors::COLOR_WARN);
        canvas.drawString(_confirmFormatModal ? "TAP AGAIN TO CONFIRM FORMAT!" : "One-Click Format TF (FAT32)", 995, 515);
    }

    endViewport(canvas);
}

bool ToolTfManager::onTouchTap(int16_t x, int16_t y, SystemContext& ctx) {
    // Back button
    if (x >= 1080 && x <= 1235 && y >= 32 && y <= 80) {
        NetworkManager::instance().stopPortals();
        _confirmFormatModal = false;
        ctx.currentPage = PAGE_8_TOOLS;
        return true;
    }

    if (ctx.wifi.apShareActive) {
        // Stop Share Hotspot button (X: 790..1195, Y: 515..579)
        if (x >= 790 && x <= 1195 && y >= 515 && y <= 579) {
            NetworkManager::instance().stopPortals();
            return true;
        }
    } else {
        // Start Wi-Fi QR Code Export button (X: 185..705, Y: 460..570)
        if (x >= 185 && x <= 705 && y >= 460 && y <= 570) {
            NetworkManager::instance().startSharePortal();
            return true;
        }
        // Format FAT32 button (X: 735..1255, Y: 460..570)
        if (x >= 735 && x <= 1255 && y >= 460 && y <= 570) {
            if (!_confirmFormatModal) {
                _confirmFormatModal = true;
            } else {
                _confirmFormatModal = false;
                TrackLogger::instance().formatFat32();
            }
            return true;
        }
    }
    return false;
}
