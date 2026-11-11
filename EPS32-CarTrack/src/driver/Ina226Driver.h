#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "../config.h"

// =============================================================================
// Ina226Driver - Tab5 Onboard TI INA226 Power Monitor Driver
// - STRICTLY addresses I2C 0x41 (NEVER 0x40 ES7210 Mic ADC or 0x10 ES8388!)
// - Physical sense resistor R_SHUNT = 0.005 Ohm (5 mOhm)
// - Current_LSB = 0.1 mA/bit -> CAL = 10240 (0x2800), Power_LSB = 2.5 mW/bit
// - Synchronizes with M5.Power.Ina226 & M5.In_I2C (16-bit Big-Endian registers)
// - Polarity normalized to SystemState contract: +currentMa = Charging,
//   -currentMa = Discharging (Tab5 PCB shunt raw polarity inverted in hardware)
// =============================================================================

struct Ina226Sample {
    bool     valid          = false;
    float    busVoltageV    = 0.0f; // Battery pack bus voltage (V), 1.25 mV/LSB
    float    shuntVoltageMv = 0.0f; // Sense voltage across 5mOhm shunt (mV, +chg / -dischg)
    float    currentMa      = 0.0f; // +Charging / -Discharging current (mA), 0.1 mA/LSB
    float    powerMw        = 0.0f; // Instantaneous battery power magnitude (mW), 2.5 mW/LSB
    uint16_t rawConfig      = 0;
    uint16_t rawCalibration = 0;
};

class Ina226Driver {
public:
    static Ina226Driver& instance();

    Ina226Driver() = default;

    // Verify Manufacturer ID (0x5449) & Die ID (0x2260) on 0x41, synchronize
    // M5.Power.Ina226 configuration, and write CONFIG=0x4127 + CAL=10240 (0x2800).
    bool begin();

    bool     isOnline() const { return _online; }
    uint16_t manufacturerId() const { return _manufId; }
    uint16_t dieId() const { return _dieId; }

    // Read bus voltage (V), shunt voltage (mV), signed current (mA), and power (mW).
    // Automatically restores CAL=10240 if brownout cleared register 0x05.
    bool readSample(Ina226Sample& outSample);

    // Re-apply authoritative Tab5 calibration (0x4127, CAL=0x2800)
    bool configureAndCalibrate();

private:
    bool readReg16Be(uint8_t reg, uint16_t& outVal);
    bool writeReg16Be(uint8_t reg, uint16_t val);

    bool     _online  = false;
    uint16_t _manufId = 0;
    uint16_t _dieId   = 0;
};
