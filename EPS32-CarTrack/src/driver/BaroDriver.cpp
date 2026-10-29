#include "driver/BaroDriver.h"

bool BaroDriver::init(TwoWire* wire) {
    _bmp = Adafruit_BMP280(wire);
    if (_bmp.begin(0x76)) {
        _isAvailable = true;
    } else if (_bmp.begin(0x77)) {
        _isAvailable = true;
    } else {
        _isAvailable = false;
        return false;
    }

    // Configure sampling according to SPEC 2.5:
    // osrs_t = x2, osrs_p = x16 (Ultra high resolution), filter = 16, standby = 62.5ms (approx 10Hz)
    _bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                    Adafruit_BMP280::SAMPLING_X2,
                    Adafruit_BMP280::SAMPLING_X16,
                    Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_63);
    return true;
}

void BaroDriver::update() {
    if (!_isAvailable) return;

    unsigned long now = millis();
    if (now - _lastSampleTime < 100) return; // 10Hz sampling
    _lastSampleTime = now;

    _lastTempC = _bmp.readTemperature();
    _lastPressureHpa = _bmp.readPressure() / 100.0f;
    _lastRawAltitudeM = _bmp.readAltitude(1013.25f);
}
