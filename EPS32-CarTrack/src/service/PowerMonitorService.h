#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include "model/SystemState.h"

class PowerMonitorService {
public:
    static PowerMonitorService& instance() {
        static PowerMonitorService inst;
        return inst;
    }

    bool init();
    void update();

    bool isAvailable() const { return _isAvailable; }
    float getBusVoltage() const { return _busVoltageV; }
    float getCurrentMa() const { return _currentMa; }
    float getSocPercent() const { return _socPercent; }

private:
    PowerMonitorService() : _isAvailable(false), _busVoltageV(0.0f), _currentMa(0.0f), _powerMw(0.0f), _remainingMah(1200.0f), _socPercent(0.0f), _lastSampleMs(0), _lastProbeMs(0), _standstillStartMs(0) {}

    bool          _isAvailable;
    float         _busVoltageV;
    float         _currentMa;
    float         _powerMw;
    float         _remainingMah;
    float         _socPercent;
    unsigned long _lastSampleMs;
    unsigned long _lastProbeMs;
    unsigned long _standstillStartMs;

    uint16_t readRegister16(uint8_t reg);
    void writeRegister16(uint8_t reg, uint16_t val);
    float lookupOcvSoc(uint16_t mv);
};
