#include "ImuDriver.h"
#include "CompassDriver.h"

#include <M5Unified.h>
#include <utility/imu/BMI270_Class.hpp>
#include <Preferences.h>
#include <math.h>
#include "../model/SystemState.h"

namespace {
    constexpr float GRAVITY_MPS2    = 9.80665f;
    constexpr float DEG_PER_RAD     = 57.2957795f;
    constexpr float RAD_PER_DEG     = 0.0174532925f;
    constexpr float ACCEL_LSB_TO_G  = 8.0f / 32768.0f;     // ACC_RANGE = 0x02 (+/-8g)
    constexpr float GYRO_LSB_TO_DPS = 2000.0f / 32768.0f;  // GYR_RANGE = 0x00 (+/-2000 dps)

    // Bosch BMI270 Register Map
    constexpr uint8_t REG_CHIP_ID         = 0x00;
    constexpr uint8_t REG_ACC_X_LSB       = 0x0C;
    constexpr uint8_t REG_INTERNAL_STATUS = 0x21;
    constexpr uint8_t REG_FEAT_PAGE       = 0x2F;
    constexpr uint8_t REG_ANYMO_1         = 0x3C;
    constexpr uint8_t REG_ACC_CONF        = 0x40;
    constexpr uint8_t REG_ACC_RANGE       = 0x41;
    constexpr uint8_t REG_GYR_CONF        = 0x42;
    constexpr uint8_t REG_GYR_RANGE       = 0x43;
    constexpr uint8_t REG_INT1_IO_CTRL    = 0x53;
    constexpr uint8_t REG_INT_LATCH       = 0x55;
    constexpr uint8_t REG_INT1_MAP_FEAT   = 0x56;
    constexpr uint8_t REG_INT_MAP_DATA    = 0x58;
    constexpr uint8_t REG_IF_CONF         = 0x6B;
    constexpr uint8_t REG_PWR_CONF        = 0x7C;
    constexpr uint8_t REG_PWR_CTRL        = 0x7D;

    float wrapDeg180(float deg) {
        while (deg > 180.0f)  deg -= 360.0f;
        while (deg < -180.0f) deg += 360.0f;
        return deg;
    }

    float clampf(float v, float lo, float hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }
}

ImuDriver& ImuDriver::instance() {
    static ImuDriver s_instance;
    return s_instance;
}

void ImuDriver::loadZeroOffsetsFromNvs() {
    Preferences prefs;
    if (prefs.begin(NvsConfig::NAMESPACE, true)) {
        _pitchZeroOffsetDeg = prefs.getFloat(NvsConfig::KEY_PITCH_ZERO_DEG, 0.0f);
        _rollZeroOffsetDeg  = prefs.getFloat(NvsConfig::KEY_ROLL_ZERO_DEG, 0.0f);
        prefs.end();
    }
    if (!isfinite(_pitchZeroOffsetDeg)) _pitchZeroOffsetDeg = 0.0f;
    if (!isfinite(_rollZeroOffsetDeg))  _rollZeroOffsetDeg  = 0.0f;
}

void ImuDriver::saveZeroOffsetsToNvs(float pitchZeroDeg, float rollZeroDeg) {
    Preferences prefs;
    if (prefs.begin(NvsConfig::NAMESPACE, false)) {
        prefs.putFloat(NvsConfig::KEY_PITCH_ZERO_DEG, pitchZeroDeg);
        prefs.putFloat(NvsConfig::KEY_ROLL_ZERO_DEG, rollZeroDeg);
        prefs.end();
    }
}

bool ImuDriver::configureActiveRegisters(uint8_t addr) {
    const uint32_t freq = SysI2cConfig::FREQ_HZ;
    // Enforce MagSafe AUX bypass (IF_CONF=0x00, PWR_CTRL=0x0E -> Temp+Gyro+Accel ON, AUX OFF)
    // and set 100Hz low-noise performance mode (+/-8g, +/-2000dps) for crisp 50Hz sampling.
    bool ok = true;
    ok = ok && M5.In_I2C.writeRegister8(addr, REG_PWR_CONF, 0x00, freq);
    delayMicroseconds(500);
    ok = ok && M5.In_I2C.writeRegister8(addr, REG_IF_CONF, 0x00, freq);
    ok = ok && M5.In_I2C.writeRegister8(addr, REG_ACC_CONF, 0xA8, freq);  // 100Hz, avg4, perf
    ok = ok && M5.In_I2C.writeRegister8(addr, REG_ACC_RANGE, 0x02, freq); // +/-8g
    ok = ok && M5.In_I2C.writeRegister8(addr, REG_GYR_CONF, 0xE8, freq);  // 100Hz, normal, low-noise perf
    ok = ok && M5.In_I2C.writeRegister8(addr, REG_GYR_RANGE, 0x00, freq); // +/-2000 dps
    ok = ok && M5.In_I2C.writeRegister8(addr, REG_INT1_MAP_FEAT, 0x00, freq);
    ok = ok && M5.In_I2C.writeRegister8(addr, REG_PWR_CTRL, 0x0E, freq);  // Temp+Gyr+Acc ON, AUX OFF
    return ok;
}

bool ImuDriver::initSingleBmi270(uint8_t addr) {
    // Safety guard: only 0x68 and 0x69 may ever be accessed here
    if (addr != DualImuConfig::I2C_ADDR_MASTER && addr != DualImuConfig::I2C_ADDR_SLAVE) {
        return false;
    }

    const uint32_t freq = SysI2cConfig::FREQ_HZ;
    if (!M5.In_I2C.scanID(addr, freq)) {
        return false;
    }

    // Dummy read + verification per BMI270 datasheet
    (void)M5.In_I2C.readRegister8(addr, REG_CHIP_ID, freq);
    if (M5.In_I2C.readRegister8(addr, REG_CHIP_ID, freq) != DualImuConfig::CHIP_ID_BMI270) {
        return false;
    }

    // Check whether M5.begin() already uploaded the 8KB BMI270 microcode (INTERNAL_STATUS[3:0] == 0x01)
    const uint8_t internalStat = M5.In_I2C.readRegister8(addr, REG_INTERNAL_STATUS, freq) & 0x0FU;
    if (internalStat != 0x01U) {
        m5::BMI270_Class bmiLoader(addr, freq, &M5.In_I2C);
        const auto spec = bmiLoader.begin(&M5.In_I2C);
        if (spec == m5::IMU_Base::imu_spec_none) {
            return false;
        }
    }

    if (!configureActiveRegisters(addr)) {
        return false;
    }

    const uint8_t verifyStat = M5.In_I2C.readRegister8(addr, REG_INTERNAL_STATUS, freq) & 0x0FU;
    return (verifyStat == 0x01U);
}

bool ImuDriver::readBurstSample(uint8_t addr, RawSample& out) const {
    uint8_t raw[12] = {};
    if (!M5.In_I2C.readRegister(addr, REG_ACC_X_LSB, raw, sizeof(raw), SysI2cConfig::FREQ_HZ)) {
        return false;
    }

    auto s16le = [&](size_t idx) -> int16_t {
        return static_cast<int16_t>(static_cast<uint16_t>(raw[idx]) |
                                    (static_cast<uint16_t>(raw[idx + 1]) << 8));
    };

    const int16_t ax = s16le(0);
    const int16_t ay = s16le(2);
    const int16_t az = s16le(4);
    const int16_t gx = s16le(6);
    const int16_t gy = s16le(8);
    const int16_t gz = s16le(10);

    // Reject all-zero uninitialized bus frames
    if (ax == 0 && ay == 0 && az == 0 && gx == 0 && gy == 0 && gz == 0) {
        return false;
    }

    out.axG    = static_cast<float>(ax) * ACCEL_LSB_TO_G;
    out.ayG    = static_cast<float>(ay) * ACCEL_LSB_TO_G;
    out.azG    = static_cast<float>(az) * ACCEL_LSB_TO_G;
    out.gxRadS = static_cast<float>(gx) * GYRO_LSB_TO_DPS * RAD_PER_DEG;
    out.gyRadS = static_cast<float>(gy) * GYRO_LSB_TO_DPS * RAD_PER_DEG;
    out.gzRadS = static_cast<float>(gz) * GYRO_LSB_TO_DPS * RAD_PER_DEG;
    return true;
}

bool ImuDriver::begin() {
    // Ensure CompassDriver bypass contract is initialized (touches zero I2C addresses)
    (void)CompassDriver::instance().begin();

    loadZeroOffsetsFromNvs();

    _masterOnline   = initSingleBmi270(DualImuConfig::I2C_ADDR_MASTER);
    _slaveOnline    = initSingleBmi270(DualImuConfig::I2C_ADDR_SLAVE);
    _failoverActive = (!_masterOnline && _slaveOnline);
    _slaveFlipZ     = false;

    delay(15);

    const uint32_t nowMs = millis();
    if (_masterOnline) {
        _lastMasterOkMs = nowMs;
    }

    // Check relative Z-axis mounting polarity if both Master (0x68) and Slave (0x69) are online
    RawSample mSample = {};
    RawSample sSample = {};
    const bool mOk = _masterOnline && readBurstSample(DualImuConfig::I2C_ADDR_MASTER, mSample);
    const bool sOk = _slaveOnline  && readBurstSample(DualImuConfig::I2C_ADDR_SLAVE, sSample);

    if (mOk && sOk) {
        if ((fabsf(mSample.azG) > 0.35f) && (mSample.azG * sSample.azG < -0.15f)) {
            _slaveFlipZ = true;
        }
    }

    {
        StateGuard guard(SystemState::instance());
        if (guard) {
            auto& st = SystemState::instance().data();
            st.boot.imuMasterOnline   = _masterOnline;
            st.boot.imuSlaveOnline    = _slaveOnline;
            st.imu.masterOnline       = _masterOnline;
            st.imu.slaveOnline        = _slaveOnline;
            st.imu.isDualFused        = (_masterOnline && _slaveOnline);
            st.imu.failoverActive     = _failoverActive;
            st.imu.pitchZeroOffsetDeg = _pitchZeroOffsetDeg;
            st.imu.rollZeroOffsetDeg  = _rollZeroOffsetDeg;
        }
    }

    // Run one immediate sample cycle to seed gravity & attitude
    update(nowMs);
    return (_masterOnline || _slaveOnline);
}

bool ImuDriver::update(uint32_t nowMs) {
    if (_lastSampleMs != 0 && (nowMs - _lastSampleMs) < DualImuConfig::SAMPLE_INTERVAL_MS) {
        return false;
    }
    _lastSampleMs = nowMs;

    RawSample mSample = {};
    RawSample sSample = {};
    bool mOk = false;
    bool sOk = false;

    if (_masterOnline) {
        mOk = readBurstSample(DualImuConfig::I2C_ADDR_MASTER, mSample);
        if (mOk) {
            _lastMasterOkMs = nowMs;
            _failoverActive = false;
        } else if ((nowMs - _lastMasterOkMs) >= DualImuConfig::FAILOVER_TIMEOUT_MS) {
            // <= 20ms instant seamless failover to Slave (0x69)
            _masterOnline      = false;
            _lastMasterProbeMs = nowMs;
        }
    } else if ((nowMs - _lastMasterProbeMs) >= 2000UL) {
        // Non-disruptive background recovery probe for 0x68 every 2 seconds
        _lastMasterProbeMs = nowMs;
        if (M5.In_I2C.scanID(DualImuConfig::I2C_ADDR_MASTER, SysI2cConfig::FREQ_HZ) &&
            initSingleBmi270(DualImuConfig::I2C_ADDR_MASTER)) {
            _masterOnline   = true;
            _lastMasterOkMs = nowMs;
            mOk             = readBurstSample(DualImuConfig::I2C_ADDR_MASTER, mSample);
        }
    }

    if (_slaveOnline) {
        sOk = readBurstSample(DualImuConfig::I2C_ADDR_SLAVE, sSample);
        if (sOk && _slaveFlipZ) {
            sSample.ayG    = -sSample.ayG;
            sSample.azG    = -sSample.azG;
            sSample.gyRadS = -sSample.gyRadS;
            sSample.gzRadS = -sSample.gzRadS;
        }
    }

    if (!mOk && !sOk) {
        StateGuard guard(SystemState::instance());
        if (guard) {
            auto& imu          = SystemState::instance().data().imu;
            imu.masterOnline   = false;
            imu.slaveOnline    = false;
            imu.isDualFused    = false;
            imu.failoverActive = false;
        }
        return false;
    }

    // Fuse or Failover between Master (0x68) and Slave (0x69)
    RawSample fused          = {};
    bool      isDualFused    = false;
    bool      crossCheckWarn = false;

    if (mOk && sOk) {
        _failoverActive = false;

        // Cross-check Z-gyro difference and 3D acceleration vector delta
        const float gyroZDiff = fabsf(mSample.gzRadS - sSample.gzRadS);
        const float accMagDiff = fabsf(
            sqrtf(mSample.axG * mSample.axG + mSample.ayG * mSample.ayG + mSample.azG * mSample.azG) -
            sqrtf(sSample.axG * sSample.axG + sSample.ayG * sSample.ayG + sSample.azG * sSample.azG)
        );
        crossCheckWarn = (gyroZDiff > DualImuConfig::CROSS_CHECK_MAX_GYRO_DIFF) ||
                         (accMagDiff > DualImuConfig::CROSS_CHECK_MAX_ACCEL_DIFF);

        if (!crossCheckWarn) {
            const float wM = DualImuConfig::DUAL_FUSION_WEIGHT_MASTER;
            const float wS = DualImuConfig::DUAL_FUSION_WEIGHT_SLAVE;
            fused.axG    = wM * mSample.axG    + wS * sSample.axG;
            fused.ayG    = wM * mSample.ayG    + wS * sSample.ayG;
            fused.azG    = wM * mSample.azG    + wS * sSample.azG;
            fused.gxRadS = wM * mSample.gxRadS + wS * sSample.gxRadS;
            fused.gyRadS = wM * mSample.gyRadS + wS * sSample.gyRadS;
            fused.gzRadS = wM * mSample.gzRadS + wS * sSample.gzRadS;
            isDualFused  = true;
        } else {
            // Trust Tab5 rigidly-mounted onboard Master (0x68) when cross-check diverges
            fused = mSample;
        }
    } else if (mOk) {
        _failoverActive = false;
        fused           = mSample;
    } else {
        // Master (0x68) failed -> Slave (0x69) seamlessly takes over within 20ms
        _failoverActive = true;
        fused           = sSample;
    }

    // Compute 3D acceleration norm (m/s^2) and 0.5s rolling variance for ZUPT standstill detection
    const float axMps2  = fused.axG * GRAVITY_MPS2;
    const float ayMps2  = fused.ayG * GRAVITY_MPS2;
    const float azMps2  = fused.azG * GRAVITY_MPS2;
    const float accNorm = sqrtf(axMps2 * axMps2 + ayMps2 * ayMps2 + azMps2 * azMps2);

    _accNormRing[_accNormIdx] = accNorm;
    _accNormIdx = static_cast<uint8_t>((_accNormIdx + 1U) % VARIANCE_WINDOW);
    if (_accNormCount < VARIANCE_WINDOW) {
        _accNormCount++;
    }

    float meanNorm = 0.0f;
    for (uint8_t i = 0; i < _accNormCount; ++i) {
        meanNorm += _accNormRing[i];
    }
    meanNorm /= static_cast<float>(_accNormCount);

    float variance = 0.0f;
    for (uint8_t i = 0; i < _accNormCount; ++i) {
        const float d = _accNormRing[i] - meanNorm;
        variance += d * d;
    }
    variance /= static_cast<float>(_accNormCount);

    const float gyroNorm = sqrtf(fused.gxRadS * fused.gxRadS +
                                 fused.gyRadS * fused.gyRadS +
                                 fused.gzRadS * fused.gzRadS);
    const bool isStationary = (_accNormCount >= 8U) &&
                              (variance < MotionPowerConfig::STATIC_VARIANCE_THRESH) &&
                              (gyroNorm < 0.045f);

    // Adaptive zero-rate gyroscope bias tracking at standstill
    if (!_biasInitialized && gyroNorm < 0.06f) {
        _gyroBiasX       = fused.gxRadS;
        _gyroBiasY       = fused.gyRadS;
        _gyroBiasZ       = fused.gzRadS;
        _biasInitialized = true;
    } else if (isStationary) {
        constexpr float BIAS_ALPHA = 0.02f;
        _gyroBiasX += BIAS_ALPHA * (fused.gxRadS - _gyroBiasX);
        _gyroBiasY += BIAS_ALPHA * (fused.gyRadS - _gyroBiasY);
        _gyroBiasZ += BIAS_ALPHA * (fused.gzRadS - _gyroBiasZ);
    }

    const float gxClean = fused.gxRadS - _gyroBiasX;
    const float gyClean = fused.gyRadS - _gyroBiasY;
    const float gzClean = fused.gzRadS - _gyroBiasZ;

    // Update slow gravity reference vector (only when total acceleration is near 1G)
    const float normG = sqrtf(fused.axG * fused.axG + fused.ayG * fused.ayG + fused.azG * fused.azG);
    if (normG > 0.25f) {
        const float ux = fused.axG / normG;
        const float uy = fused.ayG / normG;
        const float uz = fused.azG / normG;
        if (!_gravInitialized) {
            _gravX           = ux;
            _gravY           = uy;
            _gravZ           = uz;
            _gravInitialized = true;
        } else if (fabsf(normG - 1.0f) < 0.15f) {
            const float gravAlpha = isStationary ? 0.08f : 0.015f;
            _gravX += gravAlpha * (ux - _gravX);
            _gravY += gravAlpha * (uy - _gravY);
            _gravZ += gravAlpha * (uz - _gravZ);
            const float gLen = sqrtf(_gravX * _gravX + _gravY * _gravY + _gravZ * _gravZ);
            if (gLen > 1e-4f) {
                _gravX /= gLen;
                _gravY /= gLen;
                _gravZ /= gLen;
            }
        }
    }

    // Project 3D angular velocity onto vertical gravity vector:
    // Positive clockwise compass rotation = -(omega . u_up)
    _vehicleYawRateRadS = -(gxClean * _gravX + gyClean * _gravY + gzClean * _gravZ);

    // Compute raw pitch & roll from low-pass gravity direction (supports flat & vertical mounts)
    const float rawRoll  = atan2f(_gravX, hypotf(_gravY, _gravZ)) * DEG_PER_RAD;
    const float rawPitch = atan2f(_gravY, _gravZ) * DEG_PER_RAD;

    if (!isStationary) {
        _smoothPitchDeg += 0.15f * wrapDeg180(rawPitch - _smoothPitchDeg);
        _smoothRollDeg  += 0.15f * wrapDeg180(rawRoll  - _smoothRollDeg);
    } else if (_accNormCount <= 2U) {
        _smoothPitchDeg = rawPitch;
        _smoothRollDeg  = rawRoll;
    }

    // Extract horizontal vehicle dynamic G-force by removing gravity projection along u_up
    const float dotUp = fused.axG * _gravX + fused.ayG * _gravY + fused.azG * _gravZ;
    const float linXg = fused.axG - dotUp * _gravX;
    const float linYg = fused.ayG - dotUp * _gravY;
    const float linZg = fused.azG - dotUp * _gravZ;

    // Build orthonormal horizontal forward basis perpendicular to X (right) and u_up
    float fwdX = -_gravX * _gravY;
    float fwdY = 1.0f - _gravY * _gravY;
    float fwdZ = -_gravZ * _gravY;
    float fwdLen = sqrtf(fwdX * fwdX + fwdY * fwdY + fwdZ * fwdZ);
    if (fwdLen < 0.15f) {
        // Vertical landscape mount: Y points up, Z points into/out of dashboard
        fwdX   = 0.0f;
        fwdY   = -_gravZ;
        fwdZ   = _gravY;
        fwdLen = sqrtf(fwdY * fwdY + fwdZ * fwdZ);
    }
    if (fwdLen > 1e-4f) {
        fwdX /= fwdLen;
        fwdY /= fwdLen;
        fwdZ /= fwdLen;
    }

    float instLatG = isStationary ? 0.0f : linXg;
    float instLonG = isStationary ? 0.0f : (linXg * fwdX + linYg * fwdY + linZg * fwdZ);

    _smoothLatG += 0.25f * (instLatG - _smoothLatG);
    _smoothLonG += 0.25f * (instLonG - _smoothLonG);
    if (isStationary && fabsf(_smoothLatG) < 0.015f) _smoothLatG = 0.0f;
    if (isStationary && fabsf(_smoothLonG) < 0.015f) _smoothLonG = 0.0f;

    const float combinedG = hypotf(_smoothLatG, _smoothLonG);

    // Commit telemetry, G-trail, peak G memory & zero-level calibration atomically
    bool saveCalibNeeded = false;
    float newPitchZero   = _pitchZeroOffsetDeg;
    float newRollZero    = _rollZeroOffsetDeg;

    {
        StateGuard guard(SystemState::instance());
        if (guard) {
            auto& st  = SystemState::instance().data();
            auto& imu = st.imu;

            if (imu.cmdCalibrateZeroLevel) {
                _pitchZeroOffsetDeg       = _smoothPitchDeg;
                _rollZeroOffsetDeg        = _smoothRollDeg;
                newPitchZero              = _pitchZeroOffsetDeg;
                newRollZero               = _rollZeroOffsetDeg;
                imu.pitchZeroOffsetDeg    = _pitchZeroOffsetDeg;
                imu.rollZeroOffsetDeg     = _rollZeroOffsetDeg;
                imu.cmdCalibrateZeroLevel = false;
                saveCalibNeeded           = true;
            }

            imu.masterOnline      = mOk;
            imu.slaveOnline       = sOk;
            imu.isDualFused       = isDualFused;
            imu.failoverActive    = _failoverActive;
            imu.crossCheckWarning = crossCheckWarn;

            imu.accelX            = axMps2;
            imu.accelY            = ayMps2;
            imu.accelZ            = azMps2;
            imu.gyroX             = gxClean;
            imu.gyroY             = gyClean;
            imu.gyroZ             = _vehicleYawRateRadS;

            imu.lateralG          = _smoothLatG;
            imu.longitudinalG     = _smoothLonG;
            imu.combinedG         = combinedG;
            imu.pushGTrail(_smoothLatG, _smoothLonG);

            imu.rawPitchDeg       = _smoothPitchDeg;
            imu.rawRollDeg        = _smoothRollDeg;
            imu.pitchDeg          = clampf(wrapDeg180(_smoothPitchDeg - _pitchZeroOffsetDeg), -45.0f, 45.0f);
            imu.rollDeg           = clampf(wrapDeg180(_smoothRollDeg  - _rollZeroOffsetDeg),  -45.0f, 45.0f);
            imu.isStationary      = isStationary;
            imu.accelVariance     = variance;

            // Update peak lateral & longitudinal G across active trip tiers
            const float absLatG = fabsf(_smoothLatG);
            const float absLonG = fabsf(_smoothLonG);
            for (uint8_t cat = 0; cat < TRIP_CATEGORY_COUNT; ++cat) {
                if (cat == TRIP_MANUAL && !st.tripControl.manualTripActive) {
                    continue;
                }
                if (absLatG > st.trips[cat].maxLateralG) {
                    st.trips[cat].maxLateralG = absLatG;
                }
                if (absLonG > st.trips[cat].maxLongitudinalG) {
                    st.trips[cat].maxLongitudinalG = absLonG;
                }
            }
        }
    }

    if (saveCalibNeeded) {
        saveZeroOffsetsToNvs(newPitchZero, newRollZero);
    }
    return true;
}

bool ImuDriver::configureWakeOnMotionInterrupt(float thresholdG, bool intActiveHigh) {
    const uint8_t  addr = DualImuConfig::I2C_ADDR_MASTER; // 0x68 Tab5 onboard BMI270
    const uint32_t freq = SysI2cConfig::FREQ_HZ;

    if (!M5.In_I2C.scanID(addr, freq)) {
        return false;
    }

    // Disable advanced power save before accessing feature page registers
    M5.In_I2C.writeRegister8(addr, REG_PWR_CONF, 0x00, freq);
    delayMicroseconds(500);

    // BMI270 Any-Motion threshold: 1 LSB = 1/2048 g (~0.488 mg), 11-bit [10:0]
    uint16_t threshLsb = static_cast<uint16_t>(clampf(thresholdG, 0.02f, 0.95f) * 2048.0f);
    if (threshLsb < 16U)   threshLsb = 16U;
    if (threshLsb > 2047U) threshLsb = 2047U;

    // Duration in 20ms (50Hz) ticks: 5 ticks = 100ms initial hardware trigger; X/Y/Z enabled (bits 15:13 = 0b111)
    const uint16_t anymo1 = static_cast<uint16_t>(0xE000U | 0x0005U);
    // Bit 11 enables Any-Motion feature output (out_conf = bit 6 in INT1_MAP_FEAT)
    const uint16_t anymo2 = static_cast<uint16_t>(0x0800U | (threshLsb & 0x07FFU));

    const uint8_t featCfg[4] = {
        static_cast<uint8_t>(anymo1 & 0xFFU),
        static_cast<uint8_t>((anymo1 >> 8) & 0xFFU),
        static_cast<uint8_t>(anymo2 & 0xFFU),
        static_cast<uint8_t>((anymo2 >> 8) & 0xFFU)
    };

    bool ok = true;
    ok = ok && M5.In_I2C.writeRegister8(addr, REG_FEAT_PAGE, 0x01, freq);
    ok = ok && M5.In_I2C.writeRegister(addr, REG_ANYMO_1, featCfg, sizeof(featCfg), freq);
    ok = ok && M5.In_I2C.writeRegister8(addr, REG_FEAT_PAGE, 0x00, freq);

    // Map Any-Motion (bit 6 = 0x40) to INT1 pin, non-latched pulse, push-pull output
    const uint8_t ioCtrl = intActiveHigh ? 0x0AU : 0x08U; // bit3=output_en, bit1=lvl(1=high,0=low), bit2=od(0=push-pull)
    ok = ok && M5.In_I2C.writeRegister8(addr, REG_INT_LATCH, 0x00, freq);
    ok = ok && M5.In_I2C.writeRegister8(addr, REG_INT1_IO_CTRL, ioCtrl, freq);
    ok = ok && M5.In_I2C.writeRegister8(addr, REG_INT_MAP_DATA, 0x00, freq);
    ok = ok && M5.In_I2C.writeRegister8(addr, REG_INT1_MAP_FEAT, 0x40, freq);

    // Put Slave (0x69) into low-power suspend and Master (0x68) into Accel-only 50Hz LP mode
    if (_slaveOnline) {
        M5.In_I2C.writeRegister8(DualImuConfig::I2C_ADDR_SLAVE, REG_PWR_CTRL, 0x00, freq);
        M5.In_I2C.writeRegister8(DualImuConfig::I2C_ADDR_SLAVE, REG_PWR_CONF, 0x03, freq);
    }

    ok = ok && M5.In_I2C.writeRegister8(addr, REG_ACC_CONF, 0x27, freq); // 50Hz duty-cycled LP
    ok = ok && M5.In_I2C.writeRegister8(addr, REG_PWR_CTRL, 0x04, freq); // Accel ON, Gyro/Temp/AUX OFF
    ok = ok && M5.In_I2C.writeRegister8(addr, REG_PWR_CONF, 0x03, freq); // adv_power_save ON
    return ok;
}

bool ImuDriver::restoreActiveMode() {
    bool mOk = configureActiveRegisters(DualImuConfig::I2C_ADDR_MASTER);
    bool sOk = false;
    if (M5.In_I2C.scanID(DualImuConfig::I2C_ADDR_SLAVE, SysI2cConfig::FREQ_HZ)) {
        sOk = configureActiveRegisters(DualImuConfig::I2C_ADDR_SLAVE);
    }
    _masterOnline   = mOk;
    _slaveOnline    = sOk;
    _failoverActive = (!mOk && sOk);
    _lastMasterOkMs = millis();
    return (mOk || sOk);
}
