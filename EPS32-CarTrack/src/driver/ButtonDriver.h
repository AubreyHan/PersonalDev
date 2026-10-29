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
    TOUCH_LONG_PRESS_2S
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
    ButtonDriver() : _touchDown(false), _startX(0), _startY(0), _startTime(0), _longPressTriggered(false), _screenOn(true) {}

    bool          _touchDown;
    int16_t       _startX;
    int16_t       _startY;
    unsigned long _startTime;
    bool          _longPressTriggered;
    bool          _screenOn;
};
