#include "PageIndicator.h"

namespace {

uint32_t scaleColorBrightness(uint32_t rgb888, float factor) {
    if (factor >= 1.0f) return rgb888;
    if (factor <= 0.0f) return 0U;
    uint8_t r = static_cast<uint8_t>(((rgb888 >> 16) & 0xFFU) * factor);
    uint8_t g = static_cast<uint8_t>(((rgb888 >> 8) & 0xFFU) * factor);
    uint8_t b = static_cast<uint8_t>((rgb888 & 0xFFU) * factor);
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

} // namespace

void PageIndicator::render(M5Canvas& canvas, PageId activePage, uint32_t nowMs) {
    if (!isCarouselMainPage(activePage) || !isVisible(nowMs)) {
        return;
    }

    const uint32_t elapsed = nowMs - _lastActiveMs;
    float fade = 1.0f;
    constexpr uint32_t FADE_START_MS = Viewport::INDICATOR_AUTO_HIDE_MS - 350UL;
    if (elapsed > FADE_START_MS) {
        fade = static_cast<float>(Viewport::INDICATOR_AUTO_HIDE_MS - elapsed) / 350.0f;
    }

    const uint8_t activeIdx = getCarouselPageIndex(activePage);
    const int16_t dotSpacing = 22;
    const int16_t activeExtraW = 10;
    const int16_t totalDotsW = static_cast<int16_t>((_dotCount - 1) * dotSpacing + activeExtraW);
    const int16_t pillW = totalDotsW + 38;
    const int16_t pillH = 22;
    const int16_t pillX = (Viewport::SAFE_W - pillW) / 2;
    const int16_t pillY = Viewport::INDICATOR_OSD_Y - 1;
    const int16_t centerY = pillY + pillH / 2;

    // Semi-transparent floating pill background over 1120x720 viewport
    const uint8_t bgAlpha = static_cast<uint8_t>(185.0f * fade);
    canvas.fillRectAlpha(pillX, pillY, pillW, pillH, bgAlpha, 0x0A1018U);
    canvas.drawRoundRect(pillX, pillY, pillW, pillH, pillH / 2,
                         scaleColorBrightness(ThemeColors::BORDER_SUBTLE, fade));

    const uint32_t activeGlowColor = scaleColorBrightness(0x006644U, fade);
    const uint32_t activeDotColor  = scaleColorBrightness(ThemeColors::NEON_GREEN, fade);
    const uint32_t idleDotColor    = scaleColorBrightness(0x4A5B6EU, fade);

    int16_t curX = pillX + 19;
    for (uint8_t i = 0; i < _dotCount; ++i) {
        if (i == activeIdx) {
            // Glowing elongated active pill dot
            canvas.fillSmoothRoundRect(curX - 3, centerY - 6, 16, 12, 6, activeGlowColor);
            canvas.fillSmoothRoundRect(curX - 1, centerY - 4, 12, 8, 4, activeDotColor);
            curX += dotSpacing + activeExtraW;
        } else {
            canvas.fillSmoothCircle(curX + 2, centerY, 4, idleDotColor);
            curX += dotSpacing;
        }
    }
}
