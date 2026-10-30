#include "driver/BaroDriver.h"
#include "driver/I2cBusManager.h"
#include <math.h>

bool BaroDriver::init() {
    auto& bus = I2cBusManager::instance();
    uint8_t chipId = 0;

    if (bus.readReg8(I2cAddrConfig::ADDR_BMP280_BARO, 0xD0, &chipId) && (chipId == 0x58 || chipId == 0x60)) {
        _i2cAddr = I2cAddrConfig::ADDR_BMP280_BARO;
        _isAvailable = true;
    } else if (bus.readReg8(I2cAddrConfig::ADDR_BMP280_ALT, 0xD0, &chipId) && (chipId == 0x58 || chipId == 0x60)) {
        _i2cAddr = I2cAddrConfig::ADDR_BMP280_ALT;
        _isAvailable = true;
    } else {
        _isAvailable = false;
        return false;
    }

    uint8_t calib[24] = {0};
    if (!bus.readRegs(_i2cAddr, 0x88, calib, 24)) {
        _isAvailable = false;
        return false;
    }

    auto u16le = [&](int idx) -> uint16_t { return (uint16_t)calib[idx] | ((uint16_t)calib[idx + 1] << 8); };
    auto s16le = [&](int idx) -> int16_t  { return (int16_t)u16le(idx); };

    _dig_T1 = u16le(0);
    _dig_T2 = s16le(2);
    _dig_T3 = s16le(4);
    _dig_P1 = u16le(6);
    _dig_P2 = s16le(8);
    _dig_P3 = s16le(10);
    _dig_P4 = s16le(12);
    _dig_P5 = s16le(14);
    _dig_P6 = s16le(16);
    _dig_P7 = s16le(18);
    _dig_P8 = s16le(20);
    _dig_P9 = s16le(22);

    // SPEC 2.5: osrs_t = x2, osrs_p = x16, filter = 16
    bus.writeReg8(_i2cAddr, 0xF5, 0x30);
    bus.writeReg8(_i2cAddr, 0xF4, 0x57);

    return true;
}

void BaroDriver::update() {
    if (!_isAvailable) return;

    unsigned long now = millis();
    if (now - _lastSampleTimeMs < 100) return; // 10Hz
    _lastSampleTimeMs = now;

    uint8_t data[6] = {0};
    if (!I2cBusManager::instance().readRegs(_i2cAddr, 0xF7, data, 6)) {
        return;
    }

    int32_t adc_P = ((int32_t)data[0] << 12) | ((int32_t)data[1] << 4) | ((int32_t)data[2] >> 4);
    int32_t adc_T = ((int32_t)data[3] << 12) | ((int32_t)data[4] << 4) | ((int32_t)data[5] >> 4);
    if (adc_P == 0x80000 || adc_T == 0x80000) return;

    int32_t var1 = ((((adc_T >> 3) - ((int32_t)_dig_T1 << 1))) * ((int32_t)_dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)_dig_T1)) * ((adc_T >> 4) - ((int32_t)_dig_T1))) >> 12) * ((int32_t)_dig_T3)) >> 14;
    _tFine = var1 + var2;
    _lastTempC = ((_tFine * 5 + 128) >> 8) / 100.0f;

    int64_t pvar1 = ((int64_t)_tFine) - 128000;
    int64_t pvar2 = pvar1 * pvar1 * (int64_t)_dig_P6;
    pvar2 = pvar2 + ((pvar1 * (int64_t)_dig_P5) << 17);
    pvar2 = pvar2 + (((int64_t)_dig_P4) << 35);
    pvar1 = ((pvar1 * pvar1 * (int64_t)_dig_P3) >> 8) + ((pvar1 * (int64_t)_dig_P2) << 12);
    pvar1 = (((((int64_t)1) << 47) + pvar1)) * ((int64_t)_dig_P1) >> 33;
    if (pvar1 == 0) return;

    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - pvar2) * 3125) / pvar1;
    pvar1 = (((int64_t)_dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    pvar2 = (((int64_t)_dig_P8) * p) >> 19;
    p = ((p + pvar1 + pvar2) >> 8) + (((int64_t)_dig_P7) << 4);

    float pressurePa = (float)p / 256.0f;
    _lastPressureHpa = pressurePa / 100.0f;
}

float BaroDriver::getRawAltitudeM(float seaLevelHpa) const {
    return 44330.0f * (1.0f - powf(_lastPressureHpa / seaLevelHpa, 0.1903f));
}
