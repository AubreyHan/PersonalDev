#include "driver/ButtonDriver.h"
#include "driver/PowerDriver.h"

void ButtonDriver::init() {
    _touchDown = false;
    _startX = 0;
    _startY = 0;
    _lastX = 0;
    _lastY = 0;
    _longPressTriggered = false;
    _swipeTriggered = false;
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
    auto detail = M5.Touch.getDetail(0);
    bool isPressed = (count > 0) && detail.isPressed();

    if (isPressed) {
        int16_t currentX = detail.x;
        int16_t currentY = detail.y;

        if (!_touchDown) {
            _touchDown = true;
            _startX = (detail.base_x > 0 || detail.base_y > 0) ? detail.base_x : currentX;
            _startY = (detail.base_x > 0 || detail.base_y > 0) ? detail.base_y : currentY;
            _lastX = currentX;
            _lastY = currentY;
            _startTime = now;
            _longPressTriggered = false;
            _swipeTriggered = false;
        } else {
            _lastX = currentX;
            _lastY = currentY;
            int16_t dx = _lastX - _startX;
            int16_t dy = _lastY - _startY;

            // Active drag swipe trigger: fires immediately as soon as finger drags past threshold
            if (!_swipeTriggered && !_longPressTriggered) {
                if (abs(dx) >= GestureConfig::SWIPE_DRAG_TRIGGER_PX && abs(dx) >= abs(dy) * GestureConfig::DIRECTION_RATIO) {
                    _swipeTriggered = true;
                    evt.type = (dx > 0) ? TOUCH_SWIPE_RIGHT : TOUCH_SWIPE_LEFT;
                    evt.x = _startX;
                    evt.y = _startY;
                    return evt;
                } else if (_startY < 240 && abs(dy) >= GestureConfig::SWIPE_DRAG_TRIGGER_PX && abs(dy) >= abs(dx) * GestureConfig::DIRECTION_RATIO) {
                    _swipeTriggered = true;
                    evt.type = (dy < 0) ? TOUCH_SWIPE_UP : TOUCH_SWIPE_DOWN;
                    evt.x = _startX;
                    evt.y = _startY;
                    return evt;
                }
            }

            // Long press detection (only inside 320x240 display area)
            if (_startY < 240 && !_longPressTriggered && !_swipeTriggered && (now - _startTime >= TripResetConfig::LONG_PRESS_MS)) {
                if (abs(dx) < 20 && abs(dy) < 20) {
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
            if (count > 0) {
                _lastX = detail.x;
                _lastY = detail.y;
            }
            if (!_longPressTriggered && !_swipeTriggered) {
                unsigned long duration = now - _startTime;
                int16_t dx = _lastX - _startX;
                int16_t dy = _lastY - _startY;
                if (abs(detail.distanceX()) > abs(dx)) dx = detail.distanceX();
                if (abs(detail.distanceY()) > abs(dy)) dy = detail.distanceY();

                if (abs(dx) >= GestureConfig::SWIPE_HORIZ_THRESHOLD_PX && abs(dx) >= abs(dy) * GestureConfig::DIRECTION_RATIO) {
                    evt.type = (dx > 0) ? TOUCH_SWIPE_RIGHT : TOUCH_SWIPE_LEFT;
                } else if (_startY < 240 && abs(dy) >= GestureConfig::SWIPE_DOWN_THRESHOLD_PX && abs(dy) >= abs(dx) * GestureConfig::DIRECTION_RATIO) {
                    evt.type = (dy < 0) ? TOUCH_SWIPE_UP : TOUCH_SWIPE_DOWN;
                } else if (_startY < 240 && duration < 650 && abs(dx) < 18 && abs(dy) < 18) {
                    evt.type = TOUCH_TAP;
                }
                evt.x = _startX;
                evt.y = _startY;
            }
        }
    }

    return evt;
}
