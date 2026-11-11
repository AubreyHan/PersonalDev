#pragma once

#include <Arduino.h>
#include <functional>
#include <stdint.h>
#include "../config.h"
#include "../driver/Ina226Driver.h"
#include "../model/SystemState.h"

// =============================================================================
// PowerMonitorService - 2Hz Hybrid Coulomb Counter & 20-Min Standby Lifecycle
// - Samples Tab5 onboard INA226 (0x41, R_SHUNT=5mOhm, CAL=10240) every 500ms:
//     * Dynamic Coulomb integration: dQ = I * dt * eta (eta=0.95 chg, 1.0 dischg)
//       clamped strictly to 0..2000 mAh (NP-F550 2S 7.4V pack).
//     * Static OCV zero-drift calibration: when |I| < 20mA continuously >= 5s,
//       interpolates 11-node 2S Li-ion OCV_TABLE (6.00V..8.40V).
// - External Power GREEN ⚡ vs Battery-Only RED 🔋 Dual-Color Determination:
//     Combines PI4IOE5V6408 (0x44) P6 CHG_STAT / M5.Power.isCharging() with
//     INA226 current direction & pack presence to drive PowerData::isExternalPower.
// - 20-Minute (1200s) Standstill Pre-Sleep Handshake & 6.00V Cutoff Protection:
//     ACTIVE -> SETTLING_TRIPS (TrackLogger GPX/CSV close)
//            -> PRE_SLEEP_SYNC (OneDrive 25s sync if online & bound)
//            -> power.log + NVS save -> SLEEPING (80MHz LCD-off / power protect)
//            -> WAKING_UP (360MHz restore + full trip reset on motion/touch/plug).
// =============================================================================

class PowerMonitorService {
public:
    using SleepStateHook = std::function<void(bool enteringSleep)>;

    static PowerMonitorService& instance();

    PowerMonitorService() = default;

    // Initialize Ina226Driver (0x41), load persisted SoC/mAh from NVS, take initial
    // voltage/current reading, and seed PowerData & BootCheckState::ina226Online.
    bool begin();

    // Periodic power & standby state machine tick (call at >= 2~10Hz; samples INA226
    // every 500ms while servicing wakeup/touch/handshake transitions promptly).
    void update();

    // Optional hook invoked right before entering standby sleep (true) and upon wake (false)
    // so integrator can link ImuDriver motion-interrupt config cleanly.
    void setSleepStateHook(SleepStateHook hook) { _sleepHook = hook; }

    // Interpolate 2S Li-ion SoC (0.0 ~ 100.0%) from CoulombCounterConfig::OCV_TABLE
    static float lookupOcvSocPercent(float busVoltageV);

    // Persist current Coulomb counter capacity & SoC to NVS
    void saveCoulombStateToNvs();

private:
    void loadCoulombStateFromNvs();
    bool readHardwareChgStatActive() const;
    void sampleAndIntegrateCoulomb(uint32_t nowMs, const SystemStateSnapshot& snap);
    void stepStandbyStateMachine(uint32_t nowMs, const SystemStateSnapshot& snap);
    void enterStandbySleep(uint32_t nowMs, const SystemStateSnapshot& snap);
    void wakeFromStandby(const char* wakeReason, const SystemStateSnapshot& snap);
    void triggerEmergencyLowVoltageCutoff();

    SleepStateHook        _sleepHook             = nullptr;
    bool                  _inaOnline             = false;
    uint32_t              _lastSampleMs          = 0;
    uint32_t              _lastNvsSaveMs         = 0;

    float                 _remainingMah          = 1600.0f;
    float                 _socPercent            = 80.0f;
    float                 _avgCurrentMa          = 0.0f;
    bool                  _avgInitialized        = false;
    bool                  _seededFromBootOcv     = false;

    bool                  _isBatteryPresent      = true;
    bool                  _isExternalPower       = true;
    bool                  _isCharging            = false;
    BatteryPowerSource    _powerSource           = POWER_SOURCE_EXTERNAL_USB;

    uint32_t              _quietCurrentAccumMs   = 0;
    bool                  _ocvCalibratedInWindow = false;
    uint8_t               _lowVoltDebounceCount  = 0;
    bool                  _thermalChargePaused   = false;

    // 20-Minute Standstill & Standby State Machine
    StandbyLifecyclePhase _phase                 = STANDBY_PHASE_ACTIVE;
    uint32_t              _lastStateTickMs       = 0;
    uint32_t              _standstillAccumMs     = 0;
    uint32_t              _phaseEnterMs          = 0;
    uint32_t              _wakeMotionAccumMs     = 0;
    bool                  _extPowerAtSleepEntry  = false;
    uint32_t              _savedActiveCpuMhz     = MotionPowerConfig::ACTIVE_CPU_FREQ_MHZ;
};
