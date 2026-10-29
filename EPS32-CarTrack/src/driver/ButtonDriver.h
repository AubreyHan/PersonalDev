#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include "model/SystemState.h"

enum TouchEventType : uint8_t {
    TOUCH_NONE,
    TOUCH_TAP,
    TOUCH_SWIPE_LEFT,
    TOUCH_SWIPE_RIGHT,
    TOUCH_SWIPE_UP,
    TOUCH_SWIPE_DOWN,
    TOUCH_LONG_PRESS_2S,
    TOUCH_BTN_HOME,        // Button 1 (Left / BtnA): Go Home
    TOUCH_BTN_TRIP_RESET,  // Button 2 (Middle / BtnB): 1.5s Long Press -> Trip Reset
    TOUCH_BTN_TOOLS        // Button 3 (Right / BtnC): Go to Tools
};

struct TouchEvent {
    TouchEventType type;
    int16_t        x;
    int16_t        y;
};

class ButtonDriver {
public:
    static ButtonDriver& instance() {
        static ButtonDriver inst;
        return inst;
    }

    void init();
    TouchEvent update();

private:
    ButtonDriver() : _touchDown(false), _startX(0), _startY(0), _lastX(0), _lastY(0), _startTime(0), _longPressTriggered(false), _swipeTriggered(false), _screenOn(true), _btnBPressedTime(0), _btnBResetTriggered(false) {}

    bool          _touchDown;
    int16_t       _startX;
    int16_t       _startY;
    int16_t       _lastX;
    int16_t       _lastY;
    unsigned long _startTime;
    bool          _longPressTriggered;
    bool          _swipeTriggered;
    bool          _screenOn;
    unsigned long _btnBPressedTime;
    bool          _btnBResetTriggered;
};
