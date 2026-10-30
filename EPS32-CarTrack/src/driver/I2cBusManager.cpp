#include "driver/I2cBusManager.h"
#include "model/SystemState.h"

bool I2cBusManager::probeAddress(uint8_t addr, uint32_t freq) {
    uint8_t dummy = 0;
    return M5.In_I2C.readRegister(addr, 0x00, &dummy, 1, freq);
}

bool I2cBusManager::readReg8(uint8_t addr, uint8_t reg, uint8_t* val, uint32_t freq) {
    return M5.In_I2C.readRegister(addr, reg, val, 1, freq);
}

bool I2cBusManager::readRegs(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len, uint32_t freq) {
    return M5.In_I2C.readRegister(addr, reg, buf, len, freq);
}

bool I2cBusManager::writeReg8(uint8_t addr, uint8_t reg, uint8_t val, uint32_t freq) {
    return M5.In_I2C.writeRegister8(addr, reg, val, freq);
}

bool I2cBusManager::writeReg16BE(uint8_t addr, uint8_t reg, uint16_t val, uint32_t freq) {
    uint8_t buf[2] = {
        (uint8_t)((val >> 8) & 0xFF),
        (uint8_t)(val & 0xFF)
    };
    return M5.In_I2C.writeRegister(addr, reg, buf, 2, freq);
}

bool I2cBusManager::readReg16BE(uint8_t addr, uint8_t reg, uint16_t* val, uint32_t freq) {
    uint8_t buf[2] = {0};
    if (!M5.In_I2C.readRegister(addr, reg, buf, 2, freq)) {
        return false;
    }
    *val = ((uint16_t)buf[0] << 8) | buf[1];
    return true;
}

I2cScanReport I2cBusManager::scanBus() {
    I2cScanReport report = {};
    report.bmi270Master0x68 = probeAddress(I2cAddrConfig::ADDR_BMI270_MASTER);
    report.bmi270Slave0x69  = probeAddress(I2cAddrConfig::ADDR_BMI270_SLAVE);
    report.rx8130ceRtc0x32  = probeAddress(I2cAddrConfig::ADDR_RX8130CE_RTC);
    report.ina226Fuel0x40   = probeAddress(I2cAddrConfig::ADDR_INA226_PRIMARY) ||
                              probeAddress(I2cAddrConfig::ADDR_INA226_ALT);
    report.bmp280Baro0x76   = probeAddress(I2cAddrConfig::ADDR_BMP280_BARO) ||
                              probeAddress(I2cAddrConfig::ADDR_BMP280_ALT);
    report.bmm150Mag0x10    = false; // Explicitly bypassed per MagSafe-immune spec

    Serial.println("[I2C] Orthogonal Bus Scan Report (SDA=G31, SCL=G32):");
    Serial.printf("  - 0x68 BMI270 Master (Tab5)  : %s\n", report.bmi270Master0x68 ? "ACK" : "OFFLINE");
    Serial.printf("  - 0x69 BMI270 Slave  (M135)  : %s\n", report.bmi270Slave0x69  ? "ACK" : "OFFLINE");
    Serial.printf("  - 0x32 RX8130CE RTC  (Tab5)  : %s\n", report.rx8130ceRtc0x32  ? "ACK" : "OFFLINE");
    Serial.printf("  - 0x40 INA226 Gauge  (Tab5)  : %s\n", report.ina226Fuel0x40   ? "ACK" : "OFFLINE");
    Serial.printf("  - 0x76 BMP280 Baro   (M135)  : %s\n", report.bmp280Baro0x76   ? "ACK" : "OFFLINE");
    Serial.println("  - 0x10 BMM150 Compass        : BYPASSED (MagSafe Immunity Active)");

    return report;
}
