#include "driver/ButtonDriver.h"
#include "driver/PowerDriver.h"

void ButtonDriver::init() {
    _touchDown = false;
    _screenOn = true;
    _btnBPressedTime = 0;
    _btnBResetTriggered = false;
}

TouchEvent ButtonDriver::update() {
    TouchEvent evt = { TOUCH_NONE, 0, 0 };
    unsigned long now = millis();

    // 1. Check lateral physical power button (BtnPWR)
    if (M5.BtnPWR.wasClicked()) {
        _screenOn = !_screenOn;
        PowerDriver::instance().setBacklight(_screenOn ? 180 : 0);
    }

    // 2. Check Bottom 3 Red Circle Touch Buttons (BtnA, BtnB, BtnC)
    // Button 1 (Left / BtnA): Go to Home Page
    if (M5.BtnA.wasClicked()) {
        evt.type = TOUCH_BTN_HOME;
        return evt;
    }

    // Button 2 (Middle / BtnB): Long press 1.5s -> Trip Mileage Reset
    if (M5.BtnB.isPressed()) {
        if (_btnBPressedTime == 0) {
            _btnBPressedTime = now;
            _btnBResetTriggered = false;
        } else if (!_btnBResetTriggered && (now - _btnBPressedTime >= 1500)) {
            _btnBResetTriggered = true;
            evt.type = TOUCH_BTN_TRIP_RESET;
            return evt;
        }
    } else {
        _btnBPressedTime = 0;
        _btnBResetTriggered = false;
    }

    // Button 3 (Right / BtnC): Go to Tools Page
    if (M5.BtnC.wasClicked()) {
        evt.type = TOUCH_BTN_TOOLS;
        return evt;
    }

    // 3. Check screen surface touch gestures (FT6336U via M5Unified)
    auto count = M5.Touch.getCount();

    if (count > 0) {
        auto detail = M5.Touch.getDetail(0);
        int16_t currentX = detail.x;
        int16_t currentY = detail.y;

        if (!_touchDown) {
            _touchDown = true;
            _startX = currentX;
            _startY = currentY;
            _startTime = now;
            _longPressTriggered = false;
        } else {
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
            _touchDown = false;
            if (!_longPressTriggered) {
                unsigned long duration = now - _startTime;
                auto lastDetail = M5.Touch.getDetail(0);
                int16_t endX = lastDetail.prev_x;
                int16_t endY = lastDetail.prev_y;
                int16_t dx = endX - _startX;
                int16_t dy = endY - _startY;

                if (abs(dx) > GestureConfig::SWIPE_HORIZ_THRESHOLD_PX && abs(dx) > abs(dy) * GestureConfig::DIRECTION_RATIO) {
                    if (dx > 0) {
                        evt.type = TOUCH_SWIPE_RIGHT;
                    } else {
                        evt.type = TOUCH_SWIPE_LEFT;
                    }
                } else if (abs(dy) > abs(GestureConfig::SWIPE_UP_THRESHOLD_PX) && abs(dy) > abs(dx) * GestureConfig::DIRECTION_RATIO) {
                    if (dy < 0) {
                        evt.type = TOUCH_SWIPE_UP;
                    } else {
                        evt.type = TOUCH_SWIPE_DOWN;
                    }
                } else if (duration < 500 && abs(dx) < 15 && abs(dy) < 15) {
                    evt.type = TOUCH_TAP;
                }
                evt.x = _startX;
                evt.y = _startY;
            }
        }
    }

    return evt;
}
