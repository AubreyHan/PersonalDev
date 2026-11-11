#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "../config.h"

// =============================================================================
// ImuDriver - Dual Redundant Bosch BMI270 6-Axis Vehicle Attitude & G-Force Driver
//
// Master IMU : Tab5 Onboard BMI270  @ SYS_I2C 0x68 (CHIP_ID = 0x24)
// Slave  IMU : M135 Onboard BMI270  @ SYS_I2C 0x69 (CHIP_ID = 0x24, SDO=HIGH)
//
// Features:
// - 50Hz (20ms) dual-chip redundant sampling on M5.In_I2C (400 kHz)
// - Guaranteed Bosch 8KB microcode initialization on both 0x68 and 0x69
// - Permanent secondary AUX bypass (never touches 0x10 ES8388 or 0x40 ES7210)
// - Real-time Z-gyro & 3D accel cross-check + 0.5/0.5 mean noise-reduction fusion
// - <= 20ms seamless hot failover to 0x69 if 0x68 stalls or disconnects
// - Gravity-projected vehicle yaw rate (mount-angle invariant for MagSafe mounts)
// - 0.5s (25-pt) G-force friction circle trail, peak G memory, NVS zero-leveling
// - Low-power Any-Motion wake interrupt setup on 0x68 for 20-min standby sleep
// =============================================================================
class ImuDriver {
public:
    static ImuDriver& instance();

    // Initialize Master (0x68) & Slave (0x69) BMI270, load NVS zero offsets,
    // bypass AUX magnetometer, and publish initial boot status.
    bool begin();

    // 50Hz (20ms) non-blocking dual-IMU sampling, fusion, failover & G-force update.
    bool update(uint32_t nowMs = millis());

    // Configure Master BMI270 (0x68) into low-power Any-Motion interrupt mode on INT1
    // prior to entering 20-minute standstill standby sleep.
    bool configureWakeOnMotionInterrupt(
        float thresholdG = MotionPowerConfig::SUSTAINED_ACCEL_THRESH_G,
        bool  intActiveHigh = false
    );

    // Restore full 50Hz dual-IMU active mode upon waking from standby sleep.
    bool restoreActiveMode();

    bool  isMasterOnline() const { return _masterOnline; }
    bool  isSlaveOnline() const { return _slaveOnline; }
    bool  isFailoverActive() const { return _failoverActive; }
    float yawRateRadPerSec() const { return _vehicleYawRateRadS; }

private:
    ImuDriver() = default;
    ~ImuDriver() = default;
    ImuDriver(const ImuDriver&) = delete;
    ImuDriver& operator=(const ImuDriver&) = delete;

    struct RawSample {
        float axG    = 0.0f;
        float ayG    = 0.0f;
        float azG    = 1.0f;
        float gxRadS = 0.0f;
        float gyRadS = 0.0f;
        float gzRadS = 0.0f;
    };

    bool initSingleBmi270(uint8_t addr);
    bool configureActiveRegisters(uint8_t addr);
    bool readBurstSample(uint8_t addr, RawSample& out) const;
    void loadZeroOffsetsFromNvs();
    void saveZeroOffsetsToNvs(float pitchZeroDeg, float rollZeroDeg);

    static constexpr size_t VARIANCE_WINDOW = 25; // 0.5s @ 50Hz

    bool     _masterOnline       = false;
    bool     _slaveOnline        = false;
    bool     _failoverActive     = false;
    bool     _slaveFlipZ         = false;
    uint32_t _lastSampleMs       = 0;
    uint32_t _lastMasterOkMs     = 0;
    uint32_t _lastMasterProbeMs  = 0;

    // Stationary Gyro Bias Tracking (rad/s)
    float    _gyroBiasX          = 0.0f;
    float    _gyroBiasY          = 0.0f;
    float    _gyroBiasZ          = 0.0f;
    bool     _biasInitialized    = false;

    // Low-pass gravity unit vector in sensor frame (for mount-invariant yaw & G projection)
    float    _gravX              = 0.0f;
    float    _gravY              = 0.0f;
    float    _gravZ              = 1.0f;
    bool     _gravInitialized    = false;

    // Smoothed attitude & linear G
    float    _smoothPitchDeg     = 0.0f;
    float    _smoothRollDeg      = 0.0f;
    float    _pitchZeroOffsetDeg = 0.0f;
    float    _rollZeroOffsetDeg  = 0.0f;
    float    _smoothLatG         = 0.0f;
    float    _smoothLonG         = 0.0f;
    float    _vehicleYawRateRadS = 0.0f;

    // Rolling acceleration magnitude ring buffer for standstill variance detection
    float    _accNormRing[VARIANCE_WINDOW] = {};
    uint8_t  _accNormIdx         = 0;
    uint8_t  _accNormCount       = 0;
};
