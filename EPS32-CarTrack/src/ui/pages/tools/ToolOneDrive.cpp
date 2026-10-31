#include "ui/pages/tools/ToolOneDrive.h"
#include "service/OneDriveSyncService.h"
#include "service/TrackLogger.h"
#include "driver/HapticDriver.h"

void ToolOneDrive::render(M5Canvas& canvas, const SystemContext& ctx) {
    beginViewport(canvas);

    // Top Header: Title & Navigation
    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.drawString("Microsoft OneDrive Cloud Sync", 185, 24);
    canvas.setTextSize(1);
    canvas.setTextColor(UiColors::COLOR_MUTED);
    canvas.drawString("OAuth 2.0 Device Code Flow & Microsoft Graph API Background Sync", 185, 54);

    // Back button (X: 1080..1235, Y: 24..70)
    canvas.fillRoundRect(1080, 24, 155, 46, 8, UiColors::COLOR_PANEL_BG);
    canvas.drawRoundRect(1080, 24, 155, 46, 8, UiColors::COLOR_BORDER);
    canvas.setTextDatum(middle_center);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString("< Back", 1157, 47);

    if (!ctx.onedrive.isBound) {
        // STATE 1: NOT BOUND
        if (ctx.onedrive.authInProgress) {
            // SUB-STATE 1B: Device Code Verification in Progress
            // Left Card: QR Code
            canvas.fillRoundRect(185, 95, 480, 580, 12, UiColors::COLOR_PANEL_BG);
            canvas.drawRoundRect(185, 95, 480, 580, 12, UiColors::COLOR_SECONDARY);
            canvas.setTextDatum(top_center);
            canvas.setTextSize(2);
            canvas.setTextColor(UiColors::COLOR_SECONDARY);
            canvas.drawString("1. Scan on Phone / PC", 425, 115);
            canvas.setTextSize(1);
            canvas.setTextColor(UiColors::COLOR_WHITE);
            canvas.drawString(ctx.onedrive.verifyUrl, 425, 150);

            canvas.fillRect(285, 195, 280, 280, UiColors::COLOR_WHITE);
            canvas.qrcode(ctx.onedrive.verifyUrl[0] != '\0' ? ctx.onedrive.verifyUrl : "https://microsoft.com/devicelogin",
                          300, 210, 250, 6);

            canvas.setTextSize(2);
            canvas.setTextColor(UiColors::COLOR_MUTED);
            canvas.drawString("Waiting for approval...", 425, 500);

            // Right Card: Code Display & Cancel Button
            canvas.fillRoundRect(690, 95, 565, 580, 12, UiColors::COLOR_PANEL_BG);
            canvas.drawRoundRect(690, 95, 565, 580, 12, UiColors::COLOR_BORDER);

            canvas.setTextDatum(top_center);
            canvas.setTextSize(2);
            canvas.setTextColor(UiColors::COLOR_PRIMARY);
            canvas.drawString("2. Enter Device Authorization Code", 972, 115);

            // Large Code Display Box
            canvas.fillRoundRect(730, 160, 485, 90, 10, 0x0208);
            canvas.drawRoundRect(730, 160, 485, 90, 10, UiColors::COLOR_PRIMARY);
            canvas.setTextDatum(middle_center);
            canvas.setTextSize(4);
            canvas.setTextColor(UiColors::COLOR_PRIMARY);
            canvas.drawString(ctx.onedrive.userCode[0] != '\0' ? ctx.onedrive.userCode : "WAITING", 972, 205);

            // Instructions
            canvas.setTextDatum(top_left);
            canvas.setTextSize(2);
            canvas.setTextColor(UiColors::COLOR_WHITE);
            canvas.drawString("Instructions:", 730, 280);
            canvas.setTextColor(UiColors::COLOR_MUTED);
            canvas.drawString("1. Open microsoft.com/devicelogin", 730, 320);
            canvas.drawString("2. Enter the code shown above", 730, 360);
            canvas.drawString("3. Sign in to your Microsoft Account", 730, 400);
            canvas.drawString("4. Grant access to files (Files.ReadWrite)", 730, 440);
            canvas.drawString("Device will auto-complete within 5 seconds.", 730, 480);

            // Cancel Button
            canvas.fillRoundRect(730, 540, 485, 75, 10, UiColors::COLOR_DANGER);
            canvas.setTextDatum(middle_center);
            canvas.setTextSize(2);
            canvas.setTextColor(UiColors::COLOR_WHITE);
            canvas.drawString("Cancel Authorization", 972, 577);

        } else {
            // SUB-STATE 1A: Idle - Explain & Start Button
            canvas.fillRoundRect(185, 95, 1070, 580, 12, UiColors::COLOR_PANEL_BG);
            canvas.drawRoundRect(185, 95, 1070, 580, 12, UiColors::COLOR_BORDER);

            canvas.setTextDatum(top_left);
            canvas.setTextSize(2);
            canvas.setTextColor(UiColors::COLOR_PRIMARY);
            canvas.drawString("OneDrive Cloud Backup Overview", 225, 135);

            canvas.setTextColor(UiColors::COLOR_WHITE);
            canvas.drawString("Features & Capabilities:", 225, 195);

            canvas.setTextColor(UiColors::COLOR_MUTED);
            canvas.drawString("  * Automatic Cloud Backup: Silently syncs whenever connected to Wi-Fi", 225, 235);
            canvas.drawString("  * Dedicated Directory Structure in OneDrive /VehicleTracks/:", 225, 275);
            canvas.setTextColor(UiColors::COLOR_SECONDARY);
            canvas.drawString("      - trips.csv             (Historical Trip Summary Journal)", 225, 315);
            canvas.drawString("      - single/W-*.gpx        (Per-Drive Single Trip Tracks)", 225, 345);
            canvas.drawString("      - daily/W-Day-*.gpx     (Full Daily Cumulative Tracks)", 225, 375);
            canvas.drawString("      - sub/W-sub*-*.gpx      (Manual Sub-Trip 10MB Split Volumes)", 225, 405);

            canvas.setTextColor(UiColors::COLOR_MUTED);
            canvas.drawString("  * Secure Device Code Flow: No password input on car touch screen required.", 225, 445);

            if (!ctx.wifi.isConnected) {
                canvas.setTextColor(UiColors::COLOR_WARN);
                canvas.drawString("Note: Wi-Fi connection is required to start device authorization.", 225, 485);
            }

            // Start Login Button (X: 360..1080, Y: 535..625)
            uint16_t btnBg = ctx.wifi.isConnected ? UiColors::COLOR_PRIMARY : UiColors::COLOR_CARD_BG;
            uint16_t btnFg = ctx.wifi.isConnected ? UiColors::COLOR_BG : UiColors::COLOR_MUTED;
            canvas.fillRoundRect(360, 535, 720, 90, 12, btnBg);
            canvas.drawRoundRect(360, 535, 720, 90, 12, ctx.wifi.isConnected ? UiColors::COLOR_PRIMARY : UiColors::COLOR_BORDER);
            canvas.setTextDatum(middle_center);
            canvas.setTextSize(2);
            canvas.setTextColor(btnFg);
            canvas.drawString("Start OneDrive Device Authorization", 720, 580);
        }

    } else {
        // STATE 2: ALREADY BOUND
        canvas.fillRoundRect(185, 95, 1070, 360, 12, UiColors::COLOR_PANEL_BG);
        canvas.drawRoundRect(185, 95, 1070, 360, 12, UiColors::COLOR_PRIMARY);

        canvas.setTextDatum(top_left);
        canvas.setTextSize(2);
        canvas.setTextColor(UiColors::COLOR_PRIMARY);
        canvas.drawString("Account Status: BOUND & READY", 215, 125);

        canvas.setTextColor(UiColors::COLOR_WHITE);
        char lineAcc[80], lineFdr[80], linePnd[80], lineLst[80];
        snprintf(lineAcc, sizeof(lineAcc), "Microsoft Account : %s", ctx.onedrive.accountEmail[0] != '\0' ? ctx.onedrive.accountEmail : "(Personal/Work)");
        snprintf(lineFdr, sizeof(lineFdr), "Cloud Destination : OneDrive / VehicleTracks /");
        snprintf(linePnd, sizeof(linePnd), "Pending Unsynced  : %u Track Volume(s)", ctx.onedrive.pendingCount);

        char lastTimeStr[32] = "Never";
        if (ctx.onedrive.lastSyncEpoch > 0) {
            TrackLogger::instance().formatEpochToLocalTime(ctx.onedrive.lastSyncEpoch, lastTimeStr, sizeof(lastTimeStr));
        }
        snprintf(lineLst, sizeof(lineLst), "Last Sync Time    : %s", lastTimeStr);

        canvas.drawString(lineAcc, 215, 175);
        canvas.drawString(lineFdr, 215, 215);
        canvas.drawString(linePnd, 215, 255);
        canvas.drawString(lineLst, 215, 295);

        // Sync Status / Progress Bar
        if (ctx.onedrive.isSyncing) {
            canvas.setTextColor(UiColors::COLOR_SECONDARY);
            char syncMsg[96];
            snprintf(syncMsg, sizeof(syncMsg), "Syncing Now: %s (%u%%)",
                     ctx.onedrive.currentUploadingFile[0] != '\0' ? ctx.onedrive.currentUploadingFile : "trips.csv",
                     ctx.onedrive.uploadProgressPercent);
            canvas.drawString(syncMsg, 215, 345);

            // Progress bar (X: 215, Y: 385, W: 1010, H: 18)
            canvas.fillRoundRect(215, 385, 1010, 18, 6, UiColors::COLOR_CARD_BG);
            int barW = (1010 * ctx.onedrive.uploadProgressPercent) / 100;
            if (barW > 0) {
                canvas.fillRoundRect(215, 385, barW, 18, 6, UiColors::COLOR_PRIMARY);
            }
        } else {
            canvas.setTextColor(ctx.wifi.isConnected ? UiColors::COLOR_PRIMARY : UiColors::COLOR_MUTED);
            char stMsg[96];
            snprintf(stMsg, sizeof(stMsg), "Service State     : %s",
                     ctx.wifi.isConnected ? (ctx.onedrive.autoSyncEnabled ? "Auto-Sync Active (Wi-Fi 6 Connected)" : "Manual Mode (Auto-Sync Off)")
                                           : "Standby (Awaiting Wi-Fi Connection)");
            canvas.drawString(stMsg, 215, 345);
        }

        // Action Buttons Row (Y: 485..575)
        // Button 1: Sync Now (X: 185..520)
        bool canSync = ctx.wifi.isConnected && !ctx.onedrive.isSyncing;
        canvas.fillRoundRect(185, 485, 335, 90, 12, canSync ? 0x0208 : UiColors::COLOR_PANEL_BG);
        canvas.drawRoundRect(185, 485, 335, 90, 12, canSync ? UiColors::COLOR_PRIMARY : UiColors::COLOR_BORDER);
        canvas.setTextDatum(middle_center);
        canvas.setTextSize(2);
        canvas.setTextColor(canSync ? UiColors::COLOR_PRIMARY : UiColors::COLOR_MUTED);
        canvas.drawString(ctx.onedrive.isSyncing ? "Syncing..." : "Sync Now", 352, 530);

        // Button 2: Toggle Auto Sync (X: 550..885)
        canvas.fillRoundRect(550, 485, 335, 90, 12, UiColors::COLOR_PANEL_BG);
        canvas.drawRoundRect(550, 485, 335, 90, 12, ctx.onedrive.autoSyncEnabled ? UiColors::COLOR_SECONDARY : UiColors::COLOR_BORDER);
        canvas.setTextColor(ctx.onedrive.autoSyncEnabled ? UiColors::COLOR_SECONDARY : UiColors::COLOR_WHITE);
        canvas.drawString(ctx.onedrive.autoSyncEnabled ? "Auto-Sync: ON" : "Auto-Sync: OFF", 717, 530);

        // Button 3: Unbind Account (X: 915..1250)
        canvas.fillRoundRect(915, 485, 335, 90, 12, UiColors::COLOR_PANEL_BG);
        canvas.drawRoundRect(915, 485, 335, 90, 12, UiColors::COLOR_DANGER);
        canvas.setTextColor(UiColors::COLOR_DANGER);
        canvas.drawString(_confirmUnbind ? "Confirm Unbind?" : "Unbind Account", 1082, 530);
    }

    endViewport(canvas);
}

bool ToolOneDrive::onTouchTap(int16_t x, int16_t y, SystemContext& ctx) {
    // Back button (X: 1080..1235, Y: 24..70)
    if (x >= 1080 && x <= 1235 && y >= 24 && y <= 70) {
        _confirmUnbind = false;
        ctx.currentPage = SUBPAGE_TOOL_TF;
        return true;
    }

    if (!ctx.onedrive.isBound) {
        if (ctx.onedrive.authInProgress) {
            // Cancel Authorization Button (X: 730..1215, Y: 540..615)
            if (x >= 730 && x <= 1215 && y >= 540 && y <= 615) {
                OneDriveSyncService::instance().cancelDeviceLogin();
                HapticDriver::instance().triggerShortPulse(60);
                return true;
            }
        } else {
            // Start Authorization Button (X: 360..1080, Y: 535..625)
            if (x >= 360 && x <= 1080 && y >= 535 && y <= 625) {
                if (ctx.wifi.isConnected) {
                    OneDriveSyncService::instance().startDeviceLogin();
                    HapticDriver::instance().triggerShortPulse(80);
                } else {
                    HapticDriver::instance().triggerErrorPattern();
                }
                return true;
            }
        }
    } else {
        // Button 1: Sync Now (X: 185..520, Y: 485..575)
        if (x >= 185 && x <= 520 && y >= 485 && y <= 575) {
            if (ctx.wifi.isConnected && !ctx.onedrive.isSyncing) {
                OneDriveSyncService::instance().triggerManualSync();
                HapticDriver::instance().triggerShortPulse(80);
            } else {
                HapticDriver::instance().triggerErrorPattern();
            }
            return true;
        }

        // Button 2: Toggle Auto-Sync (X: 550..885, Y: 485..575)
        if (x >= 550 && x <= 885 && y >= 485 && y <= 575) {
            bool nextState = !ctx.onedrive.autoSyncEnabled;
            OneDriveSyncService::instance().setAutoSync(nextState);
            HapticDriver::instance().triggerShortPulse(50);
            return true;
        }

        // Button 3: Unbind Account (X: 915..1250, Y: 485..575)
        if (x >= 915 && x <= 1250 && y >= 485 && y <= 575) {
            if (!_confirmUnbind) {
                _confirmUnbind = true;
                HapticDriver::instance().triggerShortPulse(60);
            } else {
                _confirmUnbind = false;
                OneDriveSyncService::instance().unbindAccount();
                HapticDriver::instance().triggerDoublePulse();
            }
            return true;
        }
    }

    return false;
}
