#include "Ina226Driver.h"
#include <M5Unified.h>
#include <math.h>

// Hard compile-time guards enforcing Board #3 Correction #1:
static_assert(Ina226Config::I2C_ADDR == 0x41,
              "Tab5 onboard INA226 I2C address MUST be 0x41!");
static_assert(Ina226Config::I2C_ADDR != SysI2cConfig::ADDR_ES7210_MIC_DO_NOT_TOUCH,
              "NEVER write INA226 registers to 0x40 (ES7210 Mic ADC)!");
static_assert(Ina226Config::I2C_ADDR != SysI2cConfig::ADDR_ES8388_AUDIO_DO_NOT_TOUCH,
              "NEVER write INA226 registers to 0x10 (ES8388 Audio Codec)!");
static_assert(Ina226Config::CALIBRATION_VAL == 10240,
              "Tab5 INA226 CAL for R_SHUNT=0.005 Ohm & Current_LSB=0.1mA MUST be 10240 (0x2800)!");

Ina226Driver& Ina226Driver::instance() {
    static Ina226Driver s_instance;
    return s_instance;
}

bool Ina226Driver::readReg16Be(uint8_t reg, uint16_t& outVal) {
    uint8_t rx[2] = { 0, 0 };
    if (!M5.In_I2C.readRegister(Ina226Config::I2C_ADDR,
                                reg,
                                rx, sizeof(rx),
                                SysI2cConfig::FREQ_HZ)) {
        return false;
    }
    outVal = static_cast<uint16_t>((static_cast<uint16_t>(rx[0]) << 8) | rx[1]);
    return true;
}

bool Ina226Driver::writeReg16Be(uint8_t reg, uint16_t val) {
    const uint8_t tx[2] = {
        static_cast<uint8_t>((val >> 8) & 0xFFU),
        static_cast<uint8_t>(val & 0xFFU)
    };
    return M5.In_I2C.writeRegister(Ina226Config::I2C_ADDR,
                                   reg,
                                   tx, sizeof(tx),
                                   SysI2cConfig::FREQ_HZ);
}

bool Ina226Driver::configureAndCalibrate() {
    // Keep M5Unified's M5.Power.Ina226 internal _cur_lsb synchronized with
    // R_SHUNT = 0.005f Ohm and Current_LSB = 0.0001 A (0.1 mA/bit -> max = 3.2768 A)
    m5::INA226_Class::config_t m5Cfg;
    m5Cfg.shunt_res            = Ina226Config::SHUNT_RESISTOR_OHM; // 0.005f Ohm
    m5Cfg.max_expected_current = 3.2768f;                          // 32768 * 0.0001A -> CAL = 10240
    m5Cfg.sampling_rate        = m5::INA226_Class::Sampling::Rate16;
    m5Cfg.bus_conversion_time  = m5::INA226_Class::ConversionTime::US_1100;
    m5Cfg.shunt_conversion_time = m5::INA226_Class::ConversionTime::US_1100;
    m5Cfg.mode                 = m5::INA226_Class::Mode::ShuntAndBus;
    M5.Power.Ina226.config(m5Cfg);

    // Explicitly write authoritative 16-bit Big-Endian CONFIG (0x4127) and CAL (0x2800 = 10240)
    if (!writeReg16Be(Ina226Config::REG_CONFIG, Ina226Config::CONFIG_WORD_DEFAULT)) {
        return false;
    }
    if (!writeReg16Be(Ina226Config::REG_CALIBRATION, Ina226Config::CALIBRATION_VAL)) {
        return false;
    }

    uint16_t readBackCal = 0;
    if (!readReg16Be(Ina226Config::REG_CALIBRATION, readBackCal)) {
        return false;
    }
    return (readBackCal == Ina226Config::CALIBRATION_VAL);
}

bool Ina226Driver::begin() {
    _online  = false;
    _manufId = 0;
    _dieId   = 0;

    if (!readReg16Be(Ina226Config::REG_MANUF_ID, _manufId)) {
        return false;
    }
    if (!readReg16Be(Ina226Config::REG_DIE_ID, _dieId)) {
        return false;
    }
    if (_manufId != Ina226Config::EXPECTED_MANUF_ID ||
        _dieId   != Ina226Config::EXPECTED_DIE_ID) {
        return false;
    }

    _online = configureAndCalibrate();
    return _online;
}

bool Ina226Driver::readSample(Ina226Sample& outSample) {
    outSample = Ina226Sample{};

    // Self-check calibration register so REG_CURRENT (0x04) & REG_POWER (0x03) never read 0 after POR
    uint16_t calReg = 0;
    if (!readReg16Be(Ina226Config::REG_CALIBRATION, calReg)) {
        _online = false;
        return false;
    }
    if (calReg != Ina226Config::CALIBRATION_VAL) {
        if (!configureAndCalibrate()) {
            _online = false;
            return false;
        }
        calReg = Ina226Config::CALIBRATION_VAL;
    }

    uint16_t rawBus   = 0;
    uint16_t rawShunt = 0;
    uint16_t rawCurr  = 0;
    uint16_t rawPower = 0;
    uint16_t rawCfg   = 0;

    if (!readReg16Be(Ina226Config::REG_BUS_VOLTAGE, rawBus) ||
        !readReg16Be(Ina226Config::REG_SHUNT_VOLTAGE, rawShunt) ||
        !readReg16Be(Ina226Config::REG_CURRENT, rawCurr) ||
        !readReg16Be(Ina226Config::REG_POWER, rawPower)) {
        _online = false;
        return false;
    }
    readReg16Be(Ina226Config::REG_CONFIG, rawCfg);

    const int16_t signedShunt = static_cast<int16_t>(rawShunt);
    const int16_t signedCurr  = static_cast<int16_t>(rawCurr);

    // Bus voltage: unsigned 16-bit, 1.25 mV / bit -> Volts
    const float busV = static_cast<float>(rawBus) * (Ina226Config::BUS_VOLT_LSB_MV * 0.001f);

    // Tab5 PCB high-side shunt orientation (see M5Unified Power_Class.inl L2836-2838):
    // Raw positive shunt/current = battery DISCHARGING; raw negative = battery CHARGING.
    // Invert sign so +currentMa = Charging and -currentMa = Discharging per SystemState.h!
    const float shuntMv = -static_cast<float>(signedShunt) * (Ina226Config::SHUNT_VOLT_LSB_UV * 0.001f);
    const float currMa  = -static_cast<float>(signedCurr) * Ina226Config::CURRENT_LSB_MA;

    float pwrMw = static_cast<float>(rawPower) * Ina226Config::POWER_LSB_MW;
    if (pwrMw <= 0.0f && fabsf(currMa) > 0.5f) {
        pwrMw = fabsf(busV * currMa);
    }

    outSample.valid          = true;
    outSample.busVoltageV    = busV;
    outSample.shuntVoltageMv = shuntMv;
    outSample.currentMa      = currMa;
    outSample.powerMw        = pwrMw;
    outSample.rawConfig      = rawCfg;
    outSample.rawCalibration = calReg;

    _online = true;
    return true;
}
