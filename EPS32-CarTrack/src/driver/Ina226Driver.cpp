#include "driver/Ina226Driver.h"
#include "driver/I2cBusManager.h"

bool Ina226Driver::init() {
    auto& bus = I2cBusManager::instance();
    uint16_t manufId = 0;

    if (bus.readReg16BE(Ina226Config::I2C_ADDR, Ina226Config::REG_MANUF_ID, &manufId) && manufId == 0x5449) {
        _i2cAddr = Ina226Config::I2C_ADDR;
        _isAvailable = true;
    } else if (bus.readReg16BE(I2cAddrConfig::ADDR_INA226_ALT, Ina226Config::REG_MANUF_ID, &manufId) && manufId == 0x5449) {
        _i2cAddr = I2cAddrConfig::ADDR_INA226_ALT;
        _isAvailable = true;
    } else {
        _isAvailable = false;
        return false;
    }

    // Write Config Word 0x4127 (16 averages, 1.1ms conversion time, continuous shunt & bus)
    bus.writeReg16BE(_i2cAddr, Ina226Config::REG_CONFIG, Ina226Config::CONFIG_WORD_DEFAULT);
    // Write Calibration Word CAL = 2560 (0x0A00) for R_shunt = 20 mOhm, Current_LSB = 0.1 mA/bit
    bus.writeReg16BE(_i2cAddr, Ina226Config::REG_CALIBRATION, Ina226Config::CALIBRATION_VAL);

    return true;
}

bool Ina226Driver::readMeasurements(float& busVoltV, float& shuntVoltMv, float& currentMa, float& powerMw) {
    if (!_isAvailable) return false;

    auto& bus = I2cBusManager::instance();
    uint16_t rawBus = 0, rawShunt = 0, rawCurrent = 0, rawPower = 0;

    if (!bus.readReg16BE(_i2cAddr, Ina226Config::REG_BUSVOLTAGE, &rawBus)) return false;
    bus.readReg16BE(_i2cAddr, Ina226Config::REG_SHUNTVOLTAGE, &rawShunt);
    bus.readReg16BE(_i2cAddr, Ina226Config::REG_CURRENT, &rawCurrent);
    bus.readReg16BE(_i2cAddr, Ina226Config::REG_POWER, &rawPower);

    busVoltV    = (rawBus * Ina226Config::BUS_VOLT_LSB_MV) / 1000.0f;
    shuntVoltMv = ((int16_t)rawShunt * 2.5f) / 1000.0f;
    currentMa   = ((int16_t)rawCurrent) * Ina226Config::CURRENT_LSB_MA;
    powerMw     = rawPower * Ina226Config::POWER_LSB_MW;
    return true;
}
