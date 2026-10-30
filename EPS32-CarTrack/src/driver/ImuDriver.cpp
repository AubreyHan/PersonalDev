#include "driver/ImuDriver.h"
#include "driver/I2cBusManager.h"
#include <M5Unified.h>
#include <math.h>

bool ImuDriver::init() {
    auto& bus = I2cBusManager::instance();
    uint8_t chipIdM = 0, chipIdS = 0;

    // BMI270 CHIP_ID register 0x00 == 0x24
    _status.masterOnline = (bus.readReg8(DualImuConfig::I2C_ADDR_MASTER, 0x00, &chipIdM) && chipIdM == 0x24) || M5.Imu.isEnabled();
    _status.slaveOnline  = (bus.readReg8(DualImuConfig::I2C_ADDR_SLAVE,  0x00, &chipIdS) && chipIdS == 0x24);

    if (_status.slaveOnline) {
        // Ensure Slave BMI270 (0x69) power & acc/gyr enabled (PWR_CTRL 0x7D = 0x0E, ACC_CONF 0x40 = 0xA8, GYR_CONF 0x42 = 0xA9)
        bus.writeReg8(DualImuConfig::I2C_ADDR_SLAVE, 0x7C, 0x00);
        delay(2);
        bus.writeReg8(DualImuConfig::I2C_ADDR_SLAVE, 0x40, 0xA8);
        bus.writeReg8(DualImuConfig::I2C_ADDR_SLAVE, 0x42, 0xA9);
        bus.writeReg8(DualImuConfig::I2C_ADDR_SLAVE, 0x7D, 0x0E);
    }

    _status.isDualFused    = (_status.masterOnline && _status.slaveOnline);
    _status.failoverActive = (!_status.masterOnline && _status.slaveOnline);
    _lastSampleMs = millis();

    return (_status.masterOnline || _status.slaveOnline);
}

bool ImuDriver::readBmi270Data(uint8_t addr, float& ax, float& ay, float& az, float& gx, float& gy, float& gz) {
    uint8_t raw[12] = {0};
    // BMI270 DATA_8 (ACC_X_LSB) starts at 0x0C .. 0x17 (6 bytes Accel + 6 bytes Gyro)
    if (!I2cBusManager::instance().readRegs(addr, 0x0C, raw, 12)) {
        return false;
    }

    int16_t rawAx = (int16_t)((uint16_t)raw[0]  | ((uint16_t)raw[1]  << 8));
    int16_t rawAy = (int16_t)((uint16_t)raw[2]  | ((uint16_t)raw[3]  << 8));
    int16_t rawAz = (int16_t)((uint16_t)raw[4]  | ((uint16_t)raw[5]  << 8));
    int16_t rawGx = (int16_t)((uint16_t)raw[6]  | ((uint16_t)raw[7]  << 8));
    int16_t rawGy = (int16_t)((uint16_t)raw[8]  | ((uint16_t)raw[9]  << 8));
    int16_t rawGz = (int16_t)((uint16_t)raw[10] | ((uint16_t)raw[11] << 8));

    // Default +/-8g range -> 4096 LSB/g; Gyro +/-2000 dps -> 16.384 LSB/(deg/s)
    constexpr float ACC_SCALE_MPS2 = 9.80665f / 4096.0f;
    constexpr float GYR_SCALE_RADS = (0.0174532925f) / 16.384f;

    ax = rawAx * ACC_SCALE_MPS2;
    ay = rawAy * ACC_SCALE_MPS2;
    az = rawAz * ACC_SCALE_MPS2;
    gx = rawGx * GYR_SCALE_RADS;
    gy = rawGy * GYR_SCALE_RADS;
    gz = rawGz * GYR_SCALE_RADS;
    return true;
}

void ImuDriver::update() {
    uint32_t now = millis();
    if (now - _lastSampleMs < 20) return; // 50Hz sampling
    float dt = (now - _lastSampleMs) * 0.001f;
    _lastSampleMs = now;

    float axM = 0, ayM = 0, azM = 9.80665f, gxM = 0, gyM = 0, gzM = 0;
    float axS = 0, ayS = 0, azS = 9.80665f, gxS = 0, gyS = 0, gzS = 0;

    bool okM = false;
    if (M5.Imu.isEnabled() && M5.Imu.update()) {
        M5.Imu.getAccel(&axM, &ayM, &azM);
        M5.Imu.getGyro(&gxM, &gyM, &gzM);
        // Convert G to m/s^2 and deg/s to rad/s
        axM *= 9.80665f;
        ayM *= 9.80665f;
        azM *= 9.80665f;
        gxM *= 0.0174532925f;
        gyM *= 0.0174532925f;
        gzM *= 0.0174532925f;
        okM = true;
    } else if (_status.masterOnline) {
        okM = readBmi270Data(DualImuConfig::I2C_ADDR_MASTER, axM, ayM, azM, gxM, gyM, gzM);
    }

    bool okS = false;
    if (_status.slaveOnline) {
        okS = readBmi270Data(DualImuConfig::I2C_ADDR_SLAVE, axS, ayS, azS, gxS, gyS, gzS);
    }

    _status.masterOnline = okM;
    _status.slaveOnline  = okS;

    // Cross-Check & Failover State Machine (Option A)
    if (okM && okS) {
        _masterFailStartMs = 0;
        _status.failoverActive = false;
        _status.isDualFused = true;

        float gyroDiff  = fabsf(gzM - gzS);
        float accelDiff = fabsf(axM - axS) / 9.80665f;
        _status.crossCheckWarning = (gyroDiff > DualImuConfig::CROSS_CHECK_MAX_GYRO_DIFF) ||
                                    (accelDiff > DualImuConfig::CROSS_CHECK_MAX_ACCEL_DIFF);

        _status.fusedAccelX = DualImuConfig::DUAL_FUSION_WEIGHT_MASTER * axM + DualImuConfig::DUAL_FUSION_WEIGHT_SLAVE * axS;
        _status.fusedAccelY = DualImuConfig::DUAL_FUSION_WEIGHT_MASTER * ayM + DualImuConfig::DUAL_FUSION_WEIGHT_SLAVE * ayS;
        _status.fusedAccelZ = DualImuConfig::DUAL_FUSION_WEIGHT_MASTER * azM + DualImuConfig::DUAL_FUSION_WEIGHT_SLAVE * azS;
        _status.fusedGyroZ  = DualImuConfig::DUAL_FUSION_WEIGHT_MASTER * gzM + DualImuConfig::DUAL_FUSION_WEIGHT_SLAVE * gzS;
    } else if (!okM && okS) {
        if (_masterFailStartMs == 0) _masterFailStartMs = now;
        if (now - _masterFailStartMs >= DualImuConfig::FAILOVER_TIMEOUT_MS) {
            _status.failoverActive = true;
        }
        _status.isDualFused = false;
        _status.fusedAccelX = axS;
        _status.fusedAccelY = ayS;
        _status.fusedAccelZ = azS;
        _status.fusedGyroZ  = gzS;
    } else if (okM) {
        _masterFailStartMs = 0;
        _status.failoverActive = false;
        _status.isDualFused = false;
        _status.fusedAccelX = axM;
        _status.fusedAccelY = ayM;
        _status.fusedAccelZ = azM;
        _status.fusedGyroZ  = gzM;
    } else {
        // Hardware disconnected / offline graceful fallback
        _status.isDualFused = false;
        _status.fusedAccelX = 0.0f;
        _status.fusedAccelY = 0.0f;
        _status.fusedAccelZ = 9.80665f;
        _status.fusedGyroZ  = 0.0f;
    }

    // Compute G-Force & Pitch/Roll with Zero-Velocity Update (ZUPT) noise lock
    float latG = _status.fusedAccelX / 9.80665f;
    float lonG = _status.fusedAccelY / 9.80665f;
    if (fabsf(latG) < 0.02f) latG = 0.0f;
    if (fabsf(lonG) < 0.02f) lonG = 0.0f;

    _status.lateralG      = _status.lateralG * 0.7f + latG * 0.3f;
    _status.longitudinalG = _status.longitudinalG * 0.7f + lonG * 0.3f;
    _status.totalG        = sqrtf(_status.lateralG * _status.lateralG + _status.longitudinalG * _status.longitudinalG);

    float rawPitch = atan2f(_status.fusedAccelY, sqrtf(_status.fusedAccelX * _status.fusedAccelX + _status.fusedAccelZ * _status.fusedAccelZ)) * 57.29578f;
    float rawRoll  = atan2f(-_status.fusedAccelX, _status.fusedAccelZ) * 57.29578f;

    SystemState& state = SystemState::instance();
    state.lock();
    SystemContext& ctx = state.getContextUnsafe();
    bool standstill = (ctx.gnss.speedKmh <= MotionPowerConfig::STANDSTILL_HEADING_LOCK_KMH) && (fabsf(_status.fusedGyroZ) < 0.02f);

    if (!standstill) {
        _status.pitchDeg = _status.pitchDeg * 0.85f + rawPitch * 0.15f;
        _status.rollDeg  = _status.rollDeg  * 0.85f + rawRoll  * 0.15f;

        // MagSafe-Immune Heading Engine: Fuse GPS true course with Dual-IMU Gyro Z
        float degDelta = -_status.fusedGyroZ * 57.29578f * dt;
        _status.fusedHeadingDeg = fmodf(_status.fusedHeadingDeg + degDelta + 360.0f, 360.0f);
        if (ctx.gnss.isValid && ctx.gnss.speedKmh > 3.0f) {
            float err = ctx.gnss.headingDeg - _status.fusedHeadingDeg;
            if (err > 180.0f) err -= 360.0f;
            if (err < -180.0f) err += 360.0f;
            _status.fusedHeadingDeg = fmodf(_status.fusedHeadingDeg + err * 0.15f + 360.0f, 360.0f);
        }
    }

    // Track peak G-Force in TripStats
    if (fabsf(_status.lateralG) > ctx.trip.maxLateralG) {
        ctx.trip.maxLateralG = fabsf(_status.lateralG);
    }
    if (fabsf(_status.longitudinalG) > ctx.trip.maxLongitudinalG) {
        ctx.trip.maxLongitudinalG = fabsf(_status.longitudinalG);
    }
    ctx.imu = _status;
    state.unlock();
}
