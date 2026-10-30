#include "ui/pages/tools/ToolWifi.h"
#include "service/NetworkManager.h"

void ToolWifi::render(M5Canvas& canvas, const SystemContext& ctx) {
    beginViewport(canvas);

    canvas.fillRoundRect(185, 20, 1070, 72, 10, UiColors::COLOR_PANEL_BG);
    canvas.setTextDatum(middle_left);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_BLUE);
    canvas.drawString("  Tool 2: Wi-Fi 6 Multi-SSID Manager & QR Provisioning", 205, 56);

    canvas.fillRoundRect(1080, 32, 155, 48, 8, UiColors::COLOR_DANGER);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(UiColors::COLOR_WHITE);
    canvas.drawString("Back", 1157, 56);

    SavedWifiCredential creds[5];
    uint8_t count = NetworkManager::instance().getSavedNetworks(creds, 5);

    for (int i = 0; i < 5; i++) {
        int ry = 110 + i * 85;
        canvas.fillRoundRect(185, ry, 1070, 74, 8, UiColors::COLOR_PANEL_BG);
        canvas.drawRoundRect(185, ry, 1070, 74, 8, UiColors::COLOR_BORDER);

        canvas.setTextDatum(middle_left);
        canvas.setTextSize(2);
        if (i < count) {
            canvas.setTextColor(UiColors::COLOR_WHITE);
            char rowStr[64];
            snprintf(rowStr, sizeof(rowStr), "Slot #%d: %s", i + 1, creds[i].ssid.c_str());
            canvas.drawString(rowStr, 210, ry + 37);

            // Delete button
            canvas.fillRoundRect(1060, ry + 13, 170, 48, 6, UiColors::COLOR_DANGER);
            canvas.setTextDatum(middle_center);
            canvas.drawString("Delete", 1145, ry + 37);
        } else {
            canvas.setTextColor(UiColors::COLOR_MUTED);
            char emptyStr[48];
            snprintf(emptyStr, sizeof(emptyStr), "Slot #%d: [Empty]", i + 1);
            canvas.drawString(emptyStr, 210, ry + 37);
        }
    }

    // Add New Network via QR Code Setup Button
    canvas.fillRoundRect(185, 555, 1070, 115, 12, 0x0208);
    canvas.drawRoundRect(185, 555, 1070, 115, 12, UiColors::COLOR_PRIMARY);
    canvas.setTextDatum(middle_center);
    canvas.setTextSize(2);
    canvas.setTextColor(UiColors::COLOR_PRIMARY);
    canvas.drawString("+ Add New Wi-Fi via Smart QR Code (M5Track-Setup)", 720, 612);

    endViewport(canvas);
}

bool ToolWifi::onTouchTap(int16_t x, int16_t y, SystemContext& ctx) {
    if (x >= 1080 && x <= 1235 && y >= 32 && y <= 80) {
        ctx.currentPage = PAGE_8_TOOLS;
        return true;
    }

    SavedWifiCredential creds[5];
    uint8_t count = NetworkManager::instance().getSavedNetworks(creds, 5);
    for (int i = 0; i < count; i++) {
        int ry = 110 + i * 85;
        if (x >= 1060 && x <= 1230 && y >= ry + 13 && y <= ry + 61) {
            NetworkManager::instance().deleteNetworkCredential(creds[i].ssid);
            return true;
        }
    }

    if (x >= 185 && x <= 1255 && y >= 555 && y <= 670) {
        NetworkManager::instance().startSetupPortal();
        ctx.currentPage = PAGE_WIFI_CONFIG;
        return true;
    }
    return false;
}
