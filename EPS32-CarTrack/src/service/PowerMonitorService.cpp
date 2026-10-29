#include "service/PowerMonitorService.h"
#include <math.h>

static constexpr uint8_t  INA226_I2C_ADDR = 0x45;
static constexpr uint32_t INA226_I2C_FREQ = 400000;

bool PowerMonitorService::init() {
    uint8_t dummy[2] = {0};
    if (!M5.In_I2C.readRegister(INA226_I2C_ADDR, 0x00, dummy, 2, INA226_I2C_FREQ)) {
        _isAvailable = false;
        return false;
    }

    _isAvailable = true;

    // Configure INA226:
    // CONFIG = 0x4127 (16-sample average, VBUS 1.1ms, VSHUNT 1.1ms, continuous)
    writeRegister16(0x00, 0x4127);

    // CALIBRATION = 2560 (0x0A00) for 20mOhm shunt resistor, 0.1mA/LSB
    writeRegister16(0x05, 2560);

    _lastSampleMs = millis();
    _standstillStartMs = millis();
    return true;
}

uint16_t PowerMonitorService::readRegister16(uint8_t reg) {
    if (!_isAvailable) return 0;
    uint8_t buf[2] = {0};
    if (M5.In_I2C.readRegister(INA226_I2C_ADDR, reg, buf, 2, INA226_I2C_FREQ)) {
        return ((uint16_t)buf[0] << 8) | buf[1];
    }
    return 0;
}

void PowerMonitorService::writeRegister16(uint8_t reg, uint16_t val) {
    if (!_isAvailable) return;
    uint8_t buf[2] = { (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
    M5.In_I2C.writeRegister(INA226_I2C_ADDR, reg, buf, 2, INA226_I2C_FREQ);
}

float PowerMonitorService::lookupOcvSoc(uint16_t mv) {
    // 11-point standard 3.7V Li-ion OCV lookup table
    static const struct { uint16_t mv; uint8_t soc; } ocvTable[11] = {
        { 4200, 100 },
        { 4100,  90 },
        { 4000,  80 },
        { 3900,  70 },
        { 3820,  60 },
        { 3760,  50 },
        { 3700,  40 },
        { 3650,  30 },
        { 3580,  20 },
        { 3450,  10 },
        { 3000,   0 }
    };

    if (mv >= ocvTable[0].mv) return 100.0f;
    if (mv <= ocvTable[10].mv) return 0.0f;

    for (int i = 0; i < 10; i++) {
        if (mv <= ocvTable[i].mv && mv >= ocvTable[i+1].mv) {
            float frac = (float)(mv - ocvTable[i+1].mv) / (float)(ocvTable[i].mv - ocvTable[i+1].mv);
            return ocvTable[i+1].soc + frac * (ocvTable[i].soc - ocvTable[i+1].soc);
        }
    }
    return 50.0f;
}

void PowerMonitorService::update() {
    unsigned long now = millis();
    if (now - _lastSampleMs < 500) return; // 500ms cycle
    float dtHours = (now - _lastSampleMs) / 3600000.0f;
    _lastSampleMs = now;

    SystemState& state = SystemState::instance();
    state.lock();
    SystemContext& ctx = state.getContextUnsafe();

    if (!_isAvailable) {
        if (now - _lastProbeMs < 5000) {
            state.unlock();
            return;
        }
        _lastProbeMs = now;
        // Probe again in case base was attached during runtime
        if (init()) {
            ctx.batteryMode = BATTERY_MODE_BASE_ATTACHED;
        } else {
            ctx.batteryMode = BATTERY_MODE_NO_BASE_EMPTY;
            ctx.batteryPercent = 0;
            ctx.isCharging = false;
            state.unlock();
            return;
        }
    }

    // Read Bus Voltage (REG 0x02, 1.25mV / LSB)
    uint16_t rawVbus = readRegister16(0x02);
    _busVoltageV = rawVbus * 0.00125f;

    // Read Current (REG 0x04, 0.1mA / LSB, signed int16)
    int16_t rawCurrent = (int16_t)readRegister16(0x04);
    _currentMa = rawCurrent * 0.1f;

    // Read Power (REG 0x03, 2.5mW / LSB)
    uint16_t rawPower = readRegister16(0x03);
    _powerMw = rawPower * 2.5f;

    // Coulomb counting integration
    if (_currentMa > 0.0f) {
        // Charging: eta = 0.95
        _remainingMah += _currentMa * dtHours * 0.95f;
    } else {
        // Discharging: eta = 1.00
        _remainingMah += _currentMa * dtHours; // currentMa is negative
    }

    if (_remainingMah > 1400.0f) _remainingMah = 1400.0f;
    if (_remainingMah < 0.0f) _remainingMah = 0.0f;

    // Static OCV Drift Calibration: if |I| < 20mA for > 5s
    if (fabs(_currentMa) < 20.0f) {
        if (now - _standstillStartMs > 5000) {
            uint16_t mv = (uint16_t)(_busVoltageV * 1000.0f);
            float ocvSoc = lookupOcvSoc(mv);
            _remainingMah = (ocvSoc / 100.0f) * 1400.0f;
        }
    } else {
        _standstillStartMs = now;
    }

    _socPercent = (_remainingMah / 1400.0f) * 100.0f;
    if (_socPercent > 100.0f) _socPercent = 100.0f;
    if (_socPercent < 0.0f) _socPercent = 0.0f;

    ctx.batteryMode = BATTERY_MODE_BASE_ATTACHED;
    ctx.batteryPercent = (uint8_t)_socPercent;
    ctx.isCharging = (_currentMa > 15.0f);

    ctx.powerMonitor.isAvailable = true;
    ctx.powerMonitor.isCharging = ctx.isCharging;
    ctx.powerMonitor.busVoltageV = _busVoltageV;
    ctx.powerMonitor.currentMa = _currentMa;
    ctx.powerMonitor.powerMw = _powerMw;
    ctx.powerMonitor.remainingMah = _remainingMah;
    ctx.powerMonitor.batterySocPercent = _socPercent;
    if (_currentMa < -20.0f) {
        ctx.powerMonitor.remainingMinutes = (uint16_t)((_remainingMah / -_currentMa) * 60.0f);
    } else {
        ctx.powerMonitor.remainingMinutes = 999;
    }

    state.unlock();
}
