#include "TouchDriver_CST9217.h"
#include "ui/UiManager_AMOLED.h"

bool TouchDriver_CST9217::init() {
    Serial.println("[Touch] Initializing CST9217 capacitive touch controller...");

    // 1. Hardware Reset Pulse
    pinMode(BoardConfig::TP_RST, OUTPUT);
    digitalWrite(BoardConfig::TP_RST, LOW);
    delay(25);
    digitalWrite(BoardConfig::TP_RST, HIGH);
    delay(60);

    // 2. Configure Interrupt line
    pinMode(BoardConfig::TP_INT, INPUT_PULLUP);

    // 3. Attempt SensorLib CST92xx driver initialization
    _touch.setPins(BoardConfig::TP_RST, BoardConfig::TP_INT);
    if (_touch.begin(Wire, I2cAddresses::ADDR_CST9217, BoardConfig::I2C_SDA, BoardConfig::I2C_SCL)) {
        _isOnline = true;
        _useSensorLib = true;
        _touch.setMirrorXY(false, false);
        _touch.setMaxCoordinates(BoardConfig::SCREEN_WIDTH, BoardConfig::SCREEN_HEIGHT);
        Serial.printf("[Touch] SensorLib CST92xx online! Model: %s (Addr: 0x%02X)\n",
                      _touch.getModelName(), I2cAddresses::ADDR_CST9217);
        return true;
    }

    // 4. Fallback: Direct I2C check at 0x5A
    Wire.beginTransmission(I2cAddresses::ADDR_CST9217);
    if (Wire.endTransmission() == 0) {
        _isOnline = true;
        _useSensorLib = false;
        Serial.printf("[Touch] Raw I2C detected at 0x%02X (using direct register reader)\n",
                      I2cAddresses::ADDR_CST9217);
        return true;
    }

    // 5. Try secondary address 0x1A
    Wire.beginTransmission(0x1A);
    if (Wire.endTransmission() == 0) {
        _isOnline = true;
        _useSensorLib = false;
        Serial.println("[Touch] Raw I2C detected at fallback addr 0x1A");
        return true;
    }

    Serial.println("[Touch] CST9217 touch controller probe warning, will keep trying.");
    _isOnline = false;
    return false;
}

bool TouchDriver_CST9217::readRawCoordinates(int16_t& outX, int16_t& outY) {
    // Write 0xD000 command
    Wire.beginTransmission(I2cAddresses::ADDR_CST9217);
    Wire.write(0xD0);
    Wire.write(0x00);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }

    // Read 7 bytes
    if (Wire.requestFrom((uint8_t)I2cAddresses::ADDR_CST9217, (size_t)7) != 7) {
        return false;
    }

    uint8_t buf[7];
    for (int i = 0; i < 7; i++) {
        buf[i] = Wire.read();
    }

    // Send ACK 0xD000 0xAB
    Wire.beginTransmission(I2cAddresses::ADDR_CST9217);
    Wire.write(0xD0);
    Wire.write(0x00);
    Wire.write(0xAB);
    Wire.endTransmission();

    uint8_t fingers = buf[5] & 0x0F;
    if (fingers > 0) {
        outX = ((buf[1] << 4) | (buf[3] >> 4));
        outY = ((buf[2] << 4) | (buf[3] & 0x0F));
        return true;
    }

    return false;
}

void TouchDriver_CST9217::update() {
    uint32_t now = millis();
    if (now - _lastPollMs < 15) return; // ~60Hz polling
    _lastPollMs = now;

    // Check INT line: active LOW when touched
    bool intActive = (digitalRead(BoardConfig::TP_INT) == LOW);

    // If INT is inactive and we weren't pressed, skip I2C bus traffic
    if (!intActive && !_wasPressed) {
        return;
    }

    bool currentlyPressed = false;
    int16_t currentX = _lastX;
    int16_t currentY = _lastY;

    if (_useSensorLib) {
        int16_t x[2], y[2];
        uint8_t pts = _touch.getPoint(x, y, 1);
        if (pts > 0) {
            currentlyPressed = true;
            currentX = x[0];
            currentY = y[0];
        }
    } else {
        currentlyPressed = readRawCoordinates(currentX, currentY);
    }

    if (currentlyPressed) {
        _wasPressed = true;
        _lastX = currentX;
        _lastY = currentY;
        UiManager_AMOLED::instance().handleTouch(_lastX, _lastY, true);
    } else if (_wasPressed) {
        _wasPressed = false;
        UiManager_AMOLED::instance().handleTouch(_lastX, _lastY, false);
    }
}
