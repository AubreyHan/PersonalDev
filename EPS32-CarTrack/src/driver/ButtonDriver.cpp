#include "driver/ButtonDriver.h"
#include "driver/PowerDriver.h"

void ButtonDriver::init() {
    _touchDown = false;
    _screenOn = true;
}

TouchEvent ButtonDriver::update() {
    TouchEvent evt = { TOUCH_NONE, 0, 0 };

    // 1. Check lateral physical power button (BtnPWR)
    if (M5.BtnPWR.wasClicked()) {
        // Short click toggles screen backlight
        _screenOn = !_screenOn;
        PowerDriver::instance().setBacklight(_screenOn ? 180 : 0);
    }

    // 2. Check touch screen (FT6336U via M5Unified)
    auto count = M5.Touch.getCount();
    unsigned long now = millis();

    if (count > 0) {
        auto detail = M5.Touch.getDetail(0);
        int16_t currentX = detail.x;
        int16_t currentY = detail.y;

        if (!_touchDown) {
            // Touch began
            _touchDown = true;
            _startX = currentX;
            _startY = currentY;
            _startTime = now;
            _longPressTriggered = false;
        } else {
            // Touch holding - check for 2-second long press
            if (!_longPressTriggered && (now - _startTime >= TripResetConfig::LONG_PRESS_MS)) {
                int16_t dx = abs(currentX - _startX);
                int16_t dy = abs(currentY - _startY);
                if (dx < 20 && dy < 20) {
                    _longPressTriggered = true;
                    evt.type = TOUCH_LONG_PRESS_2S;
                    evt.x = _startX;
                    evt.y = _startY;
                    return evt;
                }
            }
        }
    } else {
        if (_touchDown) {
            // Touch released
            _touchDown = false;
            if (!_longPressTriggered) {
                unsigned long duration = now - _startTime;
                auto lastDetail = M5.Touch.getDetail(0);
                int16_t endX = lastDetail.prev_x;
                int16_t endY = lastDetail.prev_y;
                int16_t dx = endX - _startX;
                int16_t dy = endY - _startY;

                if (abs(dx) > GestureConfig::SWIPE_HORIZ_THRESHOLD_PX && abs(dx) > abs(dy) * GestureConfig::DIRECTION_RATIO) {
                    // Horizontal swipe
                    if (dx > 0) {
                        evt.type = TOUCH_SWIPE_RIGHT;
                    } else {
                        evt.type = TOUCH_SWIPE_LEFT;
                    }
                } else if (abs(dy) > abs(GestureConfig::SWIPE_UP_THRESHOLD_PX) && abs(dy) > abs(dx) * GestureConfig::DIRECTION_RATIO) {
                    // Vertical swipe
                    if (dy < 0) {
                        evt.type = TOUCH_SWIPE_UP;
                    } else {
                        evt.type = TOUCH_SWIPE_DOWN;
                    }
                } else if (duration < 500 && abs(dx) < 15 && abs(dy) < 15) {
                    // Tap
                    evt.type = TOUCH_TAP;
                }
                evt.x = _startX;
                evt.y = _startY;
            }
        }
    }

    return evt;
}
