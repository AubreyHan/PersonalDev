#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include "config.h"

struct I2cScanReport {
    bool bmi270Master0x68;
    bool bmi270Slave0x69;
    bool rx8130ceRtc0x32;
    bool ina226Fuel0x40;
    bool bmp280Baro0x76;
    bool bmm150Mag0x10;
};

class I2cBusManager {
public:
    static I2cBusManager& instance() {
        static I2cBusManager inst;
        return inst;
    }

    I2cScanReport scanBus();
    bool probeAddress(uint8_t addr, uint32_t freq = 400000);
    bool readReg8(uint8_t addr, uint8_t reg, uint8_t* val, uint32_t freq = 400000);
    bool readRegs(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len, uint32_t freq = 400000);
    bool writeReg8(uint8_t addr, uint8_t reg, uint8_t val, uint32_t freq = 400000);
    bool writeReg16BE(uint8_t addr, uint8_t reg, uint16_t val, uint32_t freq = 400000);
    bool readReg16BE(uint8_t addr, uint8_t reg, uint16_t* val, uint32_t freq = 400000);

private:
    I2cBusManager() = default;
};
