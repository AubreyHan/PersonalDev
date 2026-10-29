#pragma once
#include "ui/pages/BasePage.h"
#include "ui/pages/tools/ToolTfManager.h"
#include "ui/pages/tools/ToolWifi.h"
#include "ui/pages/tools/ToolGnss.h"
#include "ui/pages/tools/ToolSettings.h"

class PageToolsGrid : public BasePage {
public:
    PageToolsGrid() {}
    void draw(M5Canvas& canvas, const SystemContext& ctx) override;
    void handleTap(int16_t x, int16_t y) override;
    void handleSwipeUp() override;
    void resetToRoot() {
        _tfTool.close();
        _wifiTool.close();
        _gnssTool.close();
        _settingsTool.close();
    }

private:
    ToolTfManager _tfTool;
    ToolWifi      _wifiTool;
    ToolGnss      _gnssTool;
    ToolSettings  _settingsTool;

    void drawToolCard(M5Canvas& canvas, int x, int y, int w, int h, const char* title, const char* sub1, const char* sub2, uint16_t color);
};
