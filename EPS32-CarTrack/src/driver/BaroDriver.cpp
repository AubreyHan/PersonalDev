#include "BaroDriver.h"

#include <M5Unified.h>
#include "../model/SystemState.h"

namespace {
    constexpr uint8_t REG_CALIB_START = 0x88;
    constexpr uint8_t REG_CHIP_ID     = 0xD0;
    constexpr uint8_t REG_SOFT_RESET  = 0xE0;
    constexpr uint8_t REG_CTRL_MEAS   = 0xF4;
    constexpr uint8_t REG_CONFIG      = 0xF5;
    constexpr uint8_t REG_PRESS_MSB   = 0xF7;
    constexpr size_t  CALIB_BYTE_LEN  = 24;
}

BaroDriver& BaroDriver::instance() {
    static BaroDriver s_instance;
    return s_instance;
}

bool BaroDriver::begin() {
    _online      = false;
    _calibLoaded = false;
    _failStreak  = 0;

    const uint8_t  addr = BaroFusionConfig::I2C_ADDR;
    const uint32_t freq = SysI2cConfig::FREQ_HZ;

    if (!M5.In_I2C.scanID(addr, freq)) {
        StateGuard guard(SystemState::instance());
        if (guard) {
            SystemState::instance().data().boot.baroOnline = false;
            SystemState::instance().data().baro.isOnline   = false;
        }
        return false;
    }

    const uint8_t chipId = M5.In_I2C.readRegister8(addr, REG_CHIP_ID, freq);
    if (chipId != BaroFusionConfig::CHIP_ID_BMP280) {
        StateGuard guard(SystemState::instance());
        if (guard) {
            SystemState::instance().data().boot.baroOnline = false;
            SystemState::instance().data().baro.isOnline   = false;
        }
        return false;
    }

    if (!readCalibration()) {
        return false;
    }

    // Configure IIR filter=16 (t_sb=0.5ms, filter=16 -> 0x10) then Normal mode osrs_t=x2, osrs_p=x16 (0x57)
    if (!M5.In_I2C.writeRegister8(addr, REG_CONFIG, BaroFusionConfig::REG_CONFIG_VAL, freq) ||
        !M5.In_I2C.writeRegister8(addr, REG_CTRL_MEAS, BaroFusionConfig::REG_CTRL_MEAS_VAL, freq)) {
        return false;
    }

    _online       = true;
    _calibLoaded  = true;
    _lastSampleMs = 0;

    // Give BMP280 one short conversion cycle if called during boot, then take initial sample
    delay(12);
    float initTemp  = _temperatureC;
    float initPress = _pressureHpa;
    if (readCompensatedSample(initTemp, initPress)) {
        _temperatureC = initTemp;
        _pressureHpa  = initPress;
        _lastSampleMs = millis();
    }

    {
        StateGuard guard(SystemState::instance());
        if (guard) {
            auto& st             = SystemState::instance().data();
            st.boot.baroOnline   = true;
            st.baro.isOnline     = true;
            st.baro.temperatureC = _temperatureC;
            st.baro.pressureHpa  = _pressureHpa;
        }
    }
    return true;
}

bool BaroDriver::readCalibration() {
    uint8_t buf[CALIB_BYTE_LEN] = {};
    if (!M5.In_I2C.readRegister(BaroFusionConfig::I2C_ADDR, REG_CALIB_START,
                                buf, CALIB_BYTE_LEN, SysI2cConfig::FREQ_HZ)) {
        return false;
    }

    auto u16le = [&](size_t idx) -> uint16_t {
        return static_cast<uint16_t>(buf[idx]) | (static_cast<uint16_t>(buf[idx + 1]) << 8);
    };
    auto s16le = [&](size_t idx) -> int16_t {
        return static_cast<int16_t>(u16le(idx));
    };

    _calib.dig_T1 = u16le(0);
    _calib.dig_T2 = s16le(2);
    _calib.dig_T3 = s16le(4);
    _calib.dig_P1 = u16le(6);
    _calib.dig_P2 = s16le(8);
    _calib.dig_P3 = s16le(10);
    _calib.dig_P4 = s16le(12);
    _calib.dig_P5 = s16le(14);
    _calib.dig_P6 = s16le(16);
    _calib.dig_P7 = s16le(18);
    _calib.dig_P8 = s16le(20);
    _calib.dig_P9 = s16le(22);

    return (_calib.dig_T1 != 0U && _calib.dig_P1 != 0U);
}

bool BaroDriver::readCompensatedSample(float& outTempC, float& outPressHpa) {
    uint8_t raw[6] = {};
    if (!M5.In_I2C.readRegister(BaroFusionConfig::I2C_ADDR, REG_PRESS_MSB,
                                raw, sizeof(raw), SysI2cConfig::FREQ_HZ)) {
        return false;
    }

    const int32_t adcP = (static_cast<int32_t>(raw[0]) << 12) |
                         (static_cast<int32_t>(raw[1]) << 4)  |
                         (static_cast<int32_t>(raw[2]) >> 4);
    const int32_t adcT = (static_cast<int32_t>(raw[3]) << 12) |
                         (static_cast<int32_t>(raw[4]) << 4)  |
                         (static_cast<int32_t>(raw[5]) >> 4);

    // 0x80000 indicates measurement not yet completed after power-up
    if (adcP == 0x80000 || adcT == 0x80000 || adcP == 0 || adcT == 0) {
        return false;
    }

    // Official Bosch BMP280 64-bit integer compensation formula
    const int32_t varT1 = ((((adcT >> 3) - (static_cast<int32_t>(_calib.dig_T1) << 1))) *
                           static_cast<int32_t>(_calib.dig_T2)) >> 11;
    const int32_t diffT = (adcT >> 4) - static_cast<int32_t>(_calib.dig_T1);
    const int32_t varT2 = (((diffT * diffT) >> 12) * static_cast<int32_t>(_calib.dig_T3)) >> 14;
    _tFine              = varT1 + varT2;
    const float tempC   = static_cast<float>((_tFine * 5 + 128) >> 8) * 0.01f;

    int64_t varP1 = static_cast<int64_t>(_tFine) - 128000LL;
    int64_t varP2 = varP1 * varP1 * static_cast<int64_t>(_calib.dig_P6);
    varP2         = varP2 + ((varP1 * static_cast<int64_t>(_calib.dig_P5)) << 17);
    varP2         = varP2 + (static_cast<int64_t>(_calib.dig_P4) << 35);
    varP1         = ((varP1 * varP1 * static_cast<int64_t>(_calib.dig_P3)) >> 8) +
                    ((varP1 * static_cast<int64_t>(_calib.dig_P2)) << 12);
    varP1         = (((1LL << 47) + varP1) * static_cast<int64_t>(_calib.dig_P1)) >> 33;

    if (varP1 == 0) {
        return false;
    }

    int64_t p = 1048576LL - adcP;
    p         = (((p << 31) - varP2) * 3125LL) / varP1;
    varP1     = (static_cast<int64_t>(_calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    varP2     = (static_cast<int64_t>(_calib.dig_P8) * p) >> 19;
    p         = ((p + varP1 + varP2) >> 8) + (static_cast<int64_t>(_calib.dig_P7) << 4);

    // p is in Q24.8 Pascals; divide by 256 -> Pa, divide by 100 -> hPa
    const float pressHpa = static_cast<float>(p) / 25600.0f;
    if (!isfinite(pressHpa) || pressHpa < 300.0f || pressHpa > 1150.0f ||
        !isfinite(tempC) || tempC < -45.0f || tempC > 95.0f) {
        return false;
    }

    outTempC    = tempC;
    outPressHpa = pressHpa;
    return true;
}

bool BaroDriver::update(uint32_t nowMs) {
    if (!_online) {
        // Periodic non-blocking hot-plug recovery attempt every 3 seconds
        if (nowMs - _lastRetryMs >= 3000UL) {
            _lastRetryMs = nowMs;
            const uint8_t addr = BaroFusionConfig::I2C_ADDR;
            const uint32_t freq = SysI2cConfig::FREQ_HZ;
            if (M5.In_I2C.scanID(addr, freq) &&
                M5.In_I2C.readRegister8(addr, REG_CHIP_ID, freq) == BaroFusionConfig::CHIP_ID_BMP280) {
                if (_calibLoaded || readCalibration()) {
                    _calibLoaded = true;
                    M5.In_I2C.writeRegister8(addr, REG_CONFIG, BaroFusionConfig::REG_CONFIG_VAL, freq);
                    M5.In_I2C.writeRegister8(addr, REG_CTRL_MEAS, BaroFusionConfig::REG_CTRL_MEAS_VAL, freq);
                    _online     = true;
                    _failStreak = 0;
                }
            }
        }
        return false;
    }

    if (_lastSampleMs != 0 && (nowMs - _lastSampleMs) < BaroFusionConfig::SAMPLE_INTERVAL_MS) {
        return false;
    }
    _lastSampleMs = nowMs;

    float newTemp  = _temperatureC;
    float newPress = _pressureHpa;
    if (!readCompensatedSample(newTemp, newPress)) {
        if (++_failStreak >= 5U) {
            _online      = false;
            _lastRetryMs = nowMs;
            StateGuard guard(SystemState::instance());
            if (guard) {
                SystemState::instance().data().baro.isOnline = false;
            }
        }
        return false;
    }

    _failStreak   = 0;
    _temperatureC = newTemp;
    _pressureHpa  = newPress;

    {
        StateGuard guard(SystemState::instance());
        if (guard) {
            auto& baro        = SystemState::instance().data().baro;
            baro.isOnline     = true;
            baro.temperatureC = _temperatureC;
            baro.pressureHpa  = _pressureHpa;
        }
    }
    return true;
}
