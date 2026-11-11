#include "PowerMonitorService.h"
#include "TimeService.h"
#include "TrackLogger.h"
#include "../driver/HapticDriver.h"
#include <M5Unified.h>
#include <Preferences.h>
#include <esp32-hal-cpu.h>
#include <math.h>
#include <stdio.h>

namespace {
    constexpr const char* NVS_KEY_BATT_MAH = "batt_mah";
    constexpr const char* NVS_KEY_BATT_SOC = "batt_soc";
    constexpr uint32_t    SETTLE_HANDSHAKE_TIMEOUT_MS = 5000UL;
    constexpr uint32_t    NVS_SOC_PERSIST_INTERVAL_MS = 300000UL; // Persist SoC every 5 min
    constexpr uint8_t     LOW_VOLT_DEBOUNCE_SAMPLES   = 3U;       // 3 * 500ms = 1.5s below 6.00V
} // namespace

PowerMonitorService& PowerMonitorService::instance() {
    static PowerMonitorService s_instance;
    return s_instance;
}

float PowerMonitorService::lookupOcvSocPercent(float busVoltageV) {
    const float mv = busVoltageV * 1000.0f;
    if (mv >= static_cast<float>(CoulombCounterConfig::OCV_TABLE[0].voltageMv)) {
        return 100.0f;
    }
    if (mv <= static_cast<float>(CoulombCounterConfig::OCV_TABLE[10].voltageMv)) {
        return 0.0f;
    }
    for (size_t i = 0; i < 10; ++i) {
        const float vHigh = static_cast<float>(CoulombCounterConfig::OCV_TABLE[i].voltageMv);
        const float vLow  = static_cast<float>(CoulombCounterConfig::OCV_TABLE[i + 1].voltageMv);
        if (mv <= vHigh && mv >= vLow) {
            const float sHigh = static_cast<float>(CoulombCounterConfig::OCV_TABLE[i].socPercent);
            const float sLow  = static_cast<float>(CoulombCounterConfig::OCV_TABLE[i + 1].socPercent);
            const float span  = vHigh - vLow;
            if (span <= 0.001f) {
                return sLow;
            }
            const float frac = (mv - vLow) / span;
            return sLow + frac * (sHigh - sLow);
        }
    }
    return 0.0f;
}

void PowerMonitorService::loadCoulombStateFromNvs() {
    Preferences prefs;
    if (!prefs.begin(NvsConfig::NAMESPACE, true)) {
        return;
    }
    if (prefs.isKey(NVS_KEY_BATT_MAH)) {
        _remainingMah = prefs.getFloat(NVS_KEY_BATT_MAH, 1600.0f);
        _socPercent   = prefs.getFloat(NVS_KEY_BATT_SOC, 80.0f);
        _remainingMah = constrain(_remainingMah, 0.0f, CoulombCounterConfig::BATTERY_RATED_MAH);
        _socPercent   = constrain(_socPercent, 0.0f, 100.0f);
        _seededFromBootOcv = true;
    }
    prefs.end();
}

void PowerMonitorService::saveCoulombStateToNvs() {
    Preferences prefs;
    if (!prefs.begin(NvsConfig::NAMESPACE, false)) {
        return;
    }
    prefs.putFloat(NVS_KEY_BATT_MAH, _remainingMah);
    prefs.putFloat(NVS_KEY_BATT_SOC, _socPercent);
    prefs.end();
}

bool PowerMonitorService::readHardwareChgStatActive() const {
    // Primary path: M5Unified reads Tab5 PI4IOE5V6408 U7 (0x44) P6 CHG_STAT
    const auto m5Chg = M5.Power.isCharging();
    if (m5Chg == m5::Power_Class::is_charging_t::is_charging) {
        return true;
    }
    // Direct read-only verification of U7 (0x44) REG_IN_STATE (0x0F) bit 6 (U7_CHG_STAT)
    uint8_t u7In = 0U;
    if (M5.In_I2C.readRegister(SysI2cConfig::ADDR_PI4IOE_2,
                               Pi4IoeConfig::REG_IN_STATE,
                               &u7In, 1,
                               SysI2cConfig::FREQ_HZ)) {
        return ((u7In & Pi4IoeConfig::U7_CHG_STAT) != 0U);
    }
    return false;
}

bool PowerMonitorService::begin() {
    const uint32_t curMhz = getCpuFrequencyMhz();
    if (curMhz >= 160UL) {
        _savedActiveCpuMhz = curMhz;
    }

    loadCoulombStateFromNvs();
    _inaOnline = Ina226Driver::instance().begin();

    const uint32_t nowMs = millis();
    _lastSampleMs    = nowMs;
    _lastStateTickMs = nowMs;
    _lastNvsSaveMs   = nowMs;

    {
        StateGuard guard(SystemState::instance());
        SystemStateSnapshot& st = SystemState::instance().data();
        st.boot.ina226Online = _inaOnline;
        st.power.isAvailable = _inaOnline;
    }

    if (_inaOnline) {
        static SystemStateSnapshot s_bootSnap;
        if (SystemState::instance().getSnapshot(s_bootSnap, pdMS_TO_TICKS(20))) {
            sampleAndIntegrateCoulomb(nowMs, s_bootSnap);
        }
    }
    return _inaOnline;
}

void PowerMonitorService::sampleAndIntegrateCoulomb(uint32_t nowMs, const SystemStateSnapshot& snap) {
    Ina226Sample sample{};
    if (!Ina226Driver::instance().readSample(sample) || !sample.valid) {
        _inaOnline = false;
        StateGuard guard(SystemState::instance());
        SystemState::instance().data().power.isAvailable = false;
        return;
    }
    _inaOnline = true;

    const uint32_t dtMs = (nowMs >= _lastSampleMs) ? (nowMs - _lastSampleMs) : Ina226Config::SAMPLE_INTERVAL_MS;
    _lastSampleMs = nowMs;
    const float dtSec   = static_cast<float>(dtMs) * 0.001f;
    const float dtHours = static_cast<float>(dtMs) / 3600000.0f;

    // 1. Pack presence & PI4IOE5V6408 (0x44) CHG_STAT + INA226 polarity evaluation
    const bool chgPinActive = readHardwareChgStatActive();
    _isBatteryPresent = (sample.busVoltageV >= CoulombCounterConfig::NO_BATTERY_VOLT_MAX_V);

    if (!_isBatteryPresent) {
        // Running without 2S battery pack -> external USB-C / HVIN MUST be powering VSYS
        _powerSource     = POWER_SOURCE_EXT_NO_BATTERY;
        _isExternalPower = true;
        _isCharging      = false;
        _remainingMah    = 0.0f;
        _socPercent      = 0.0f;
    } else {
        // Tab5 + LCD + C6 + M135 draws >= 35mA even in 80MHz LCD-off sleep and >= 180mA active.
        // Therefore, if battery discharge current is shallower than -12mA (or positive charging),
        // external USB-C / HVIN power is present and carrying system load!
        const bool currentShowsCharge = (sample.currentMa > CoulombCounterConfig::CHARGING_CURRENT_MA);
        const bool nearZeroOrPositive = (sample.currentMa > -12.0f);

        if (chgPinActive || currentShowsCharge || nearZeroOrPositive) {
            _powerSource     = POWER_SOURCE_EXTERNAL_USB;
            _isExternalPower = true;
            _isCharging      = currentShowsCharge || (chgPinActive && sample.currentMa > 3.0f);
        } else {
            _powerSource     = POWER_SOURCE_BATTERY_NPF550;
            _isExternalPower = false;
            _isCharging      = false;
        }
    }

    // 2. Thermal charge protection hysteresis (55C pause -> 45C resume)
    if (snap.baro.isOnline && _isExternalPower && _isBatteryPresent) {
        if (!_thermalChargePaused && snap.baro.temperatureC >= MotionPowerConfig::BATTERY_CHARGE_MAX_TEMP_C) {
            M5.Power.setBatteryCharge(false);
            _thermalChargePaused = true;
            TrackLogger::instance().appendPowerLog("CHG_THERMAL_PAUSE", "Paused 2S charging >= 55C");
        } else if (_thermalChargePaused && snap.baro.temperatureC <= MotionPowerConfig::BATTERY_CHARGE_RESUME_TEMP) {
            M5.Power.setBatteryCharge(true);
            _thermalChargePaused = false;
            TrackLogger::instance().appendPowerLog("CHG_THERMAL_RESUME", "Resumed 2S charging <= 45C");
        }
    }

    // 3. Dynamic Coulomb integration + Static OCV zero-drift calibration
    if (_isBatteryPresent) {
        const float ocvSoc = lookupOcvSocPercent(sample.busVoltageV);
        if (!_seededFromBootOcv) {
            _socPercent        = ocvSoc;
            _remainingMah      = (ocvSoc * 0.01f) * CoulombCounterConfig::BATTERY_RATED_MAH;
            _seededFromBootOcv = true;
        } else if (dtHours > 0.0f && dtHours < 0.1f) {
            const float eta = (sample.currentMa >= 0.0f)
                ? CoulombCounterConfig::CHARGE_EFFICIENCY_ETA
                : CoulombCounterConfig::DISCHARGE_EFFICIENCY;
            const float deltaMah = sample.currentMa * dtHours * eta;
            _remainingMah = constrain(_remainingMah + deltaMah, 0.0f, CoulombCounterConfig::BATTERY_RATED_MAH);
            _socPercent   = constrain((_remainingMah / CoulombCounterConfig::BATTERY_RATED_MAH) * 100.0f, 0.0f, 100.0f);
        }

        // CV full-charge top-off synchronization near 8.40V
        if (_isExternalPower && !_isCharging && sample.busVoltageV >= 8.34f && fabsf(sample.currentMa) < 15.0f) {
            _remainingMah = CoulombCounterConfig::BATTERY_RATED_MAH;
            _socPercent   = 100.0f;
        }

        // Static OCV zero-drift calibration when |I| < 20mA continuously for >= 5000ms
        if (fabsf(sample.currentMa) < CoulombCounterConfig::STANDSTILL_CURRENT_MA) {
            _quietCurrentAccumMs += dtMs;
            if (_quietCurrentAccumMs >= CoulombCounterConfig::STANDSTILL_SETTLE_MS && !_ocvCalibratedInWindow) {
                _socPercent            = ocvSoc;
                _remainingMah          = (ocvSoc * 0.01f) * CoulombCounterConfig::BATTERY_RATED_MAH;
                _ocvCalibratedInWindow = true;
            }
        } else {
            _quietCurrentAccumMs   = 0UL;
            _ocvCalibratedInWindow = false;
        }
    }

    // 4. 30-second smoothed current & remaining runtime / time-to-full estimation
    if (!_avgInitialized) {
        _avgCurrentMa   = sample.currentMa;
        _avgInitialized = true;
    } else {
        const float alpha = (dtSec > 0.0f) ? (dtSec / (30.0f + dtSec)) : 0.016f;
        _avgCurrentMa += alpha * (sample.currentMa - _avgCurrentMa);
    }

    uint16_t remMinutes = 0U;
    if (_isBatteryPresent) {
        if (!_isExternalPower && _avgCurrentMa < -5.0f) {
            const float mins = (_remainingMah / (-_avgCurrentMa)) * 60.0f;
            remMinutes = (mins > 9999.0f) ? 9999U : static_cast<uint16_t>(lroundf(mins));
        } else if (_isCharging && _avgCurrentMa > CoulombCounterConfig::CHARGING_CURRENT_MA) {
            const float neededMah = CoulombCounterConfig::BATTERY_RATED_MAH - _remainingMah;
            const float mins = (neededMah > 0.0f)
                ? ((neededMah / (_avgCurrentMa * CoulombCounterConfig::CHARGE_EFFICIENCY_ETA)) * 60.0f)
                : 0.0f;
            remMinutes = (mins > 9999.0f) ? 9999U : static_cast<uint16_t>(lroundf(mins));
        }
    }

    // 5. Critical 6.00V 2S battery undervoltage protection check
    if (_isBatteryPresent && !_isExternalPower &&
        sample.busVoltageV > 1.0f &&
        sample.busVoltageV < CoulombCounterConfig::CUTOFF_VOLTAGE_V) {
        if (_lowVoltDebounceCount < 255U) {
            _lowVoltDebounceCount++;
        }
    } else {
        _lowVoltDebounceCount = 0U;
    }

    // 6. Publish atomic PowerData update (GREEN ⚡ on external power, RED 🔋 on pure battery)
    const uint32_t uiColor = _isExternalPower
        ? ThemeColors::POWER_EXT_GREEN
        : ThemeColors::POWER_BATT_ONLY_RED;

    {
        StateGuard guard(SystemState::instance());
        PowerData& p = SystemState::instance().data().power;
        p.isAvailable          = true;
        p.isExternalPower      = _isExternalPower;
        p.isCharging           = _isCharging;
        p.isBatteryPresent     = _isBatteryPresent;
        p.powerSource          = _powerSource;
        p.uiDisplayColorRgb888 = uiColor;
        p.busVoltageV          = sample.busVoltageV;
        p.shuntVoltageMv       = sample.shuntVoltageMv;
        p.currentMa            = sample.currentMa;
        p.avgCurrentMa         = _avgCurrentMa;
        p.powerMw              = sample.powerMw;
        p.remainingMah         = _remainingMah;
        p.socPercent           = _socPercent;
        p.remainingMinutes     = remMinutes;
    }

    if (nowMs - _lastNvsSaveMs >= NVS_SOC_PERSIST_INTERVAL_MS) {
        saveCoulombStateToNvs();
        _lastNvsSaveMs = nowMs;
    }

    if (_lowVoltDebounceCount >= LOW_VOLT_DEBOUNCE_SAMPLES) {
        triggerEmergencyLowVoltageCutoff();
    }
}

void PowerMonitorService::triggerEmergencyLowVoltageCutoff() {
    {
        StateGuard guard(SystemState::instance());
        PowerData& p = SystemState::instance().data().power;
        p.lowBattCutoffActive = true;
        p.socPercent          = 0.0f;
        p.remainingMah        = 0.0f;
    }
    _socPercent   = 0.0f;
    _remainingMah = 0.0f;

    HapticDriver::instance().playWarning();
    TrackLogger::instance().flushAndCloseAllStreams(true);
    TrackLogger::instance().appendPowerLog("SHUTDOWN_LOW_BATT", "Bus voltage < 6.00V cutoff; GPX flushed & powering off");
    TimeService::instance().saveWarmStartPositionToNvs();
    saveCoulombStateToNvs();

    M5.Display.setBrightness(0);
    M5.Display.sleep();
    M5.Power.powerOff();
}

void PowerMonitorService::enterStandbySleep(uint32_t nowMs, const SystemStateSnapshot& snap) {
    (void)snap;
    _phase                = STANDBY_PHASE_SLEEPING;
    _phaseEnterMs         = nowMs;
    _wakeMotionAccumMs    = 0UL;
    _extPowerAtSleepEntry = _isExternalPower;

    TimeService::instance().saveWarmStartPositionToNvs();
    saveCoulombStateToNvs();
    TrackLogger::instance().appendPowerLog("STANDBY_ENTER", "20min standstill settled; display off & CPU 80MHz");

    {
        StateGuard guard(SystemState::instance());
        PowerData& p = SystemState::instance().data().power;
        p.standbyPhase         = STANDBY_PHASE_SLEEPING;
        p.preSleepSettleReq    = false;
        p.preSleepCloudSyncReq = false;
    }

    if (_sleepHook) {
        _sleepHook(true);
    }

    // If pure battery is nearly depleted (<= 5%), cut power completely to prevent deep discharge
    if (_isBatteryPresent && !_isExternalPower && _socPercent <= 5.0f) {
        TrackLogger::instance().appendPowerLog("SHUTDOWN_STANDBY", "Standby with SoC <= 5% on pure battery; powering off");
        M5.Display.setBrightness(0);
        M5.Display.sleep();
        M5.Power.powerOff();
        return;
    }

    const uint32_t curMhz = getCpuFrequencyMhz();
    if (curMhz >= 160UL) {
        _savedActiveCpuMhz = curMhz;
    }
    // Note: M5.Display.sleep() & setBrightness(0) are executed exclusively on uiTask
    // by UiManager::updateBacklightAndAudio() upon observing STANDBY_PHASE_SLEEPING,
    // avoiding cross-task MIPI-DSI & SYS_I2C bus contention.
    setCpuFrequencyMhz(MotionPowerConfig::STANDBY_CPU_FREQ_MHZ);
}

void PowerMonitorService::wakeFromStandby(const char* wakeReason, const SystemStateSnapshot& snap) {
    (void)snap;
    _phase             = STANDBY_PHASE_WAKING_UP;
    _standstillAccumMs = 0UL;
    _wakeMotionAccumMs = 0UL;

    setCpuFrequencyMhz(_savedActiveCpuMhz);
    if (_sleepHook) {
        _sleepHook(false);
    }

    // Note: M5.Display.wakeup() & backlight restoration are executed exclusively on uiTask
    // by UiManager::updateBacklightAndAudio() upon observing STANDBY_PHASE_ACTIVE.
    {
        StateGuard guard(SystemState::instance());
        PowerData& p = SystemState::instance().data().power;
        p.standbyPhase      = STANDBY_PHASE_ACTIVE;
        p.standstillSeconds = 0UL;
    }
    _phase = STANDBY_PHASE_ACTIVE;

    // Start a fresh Single Trip automatically after waking from >=20min parking standby
    SystemState::instance().requestFullTripReset();
    HapticDriver::instance().playConfirm();
    TrackLogger::instance().appendPowerLog("STANDBY_WAKEUP", wakeReason ? wakeReason : "WAKE");
}

void PowerMonitorService::stepStandbyStateMachine(uint32_t nowMs, const SystemStateSnapshot& snap) {
    const uint32_t dtMs = (nowMs >= _lastStateTickMs) ? (nowMs - _lastStateTickMs) : 0UL;
    _lastStateTickMs = nowMs;

    switch (_phase) {
        case STANDBY_PHASE_ACTIVE: {
            const bool speedIdle = (snap.gnss.speedKmh <= GnssConfig::STATIC_DEADBAND_KMH);
            const bool imuQuiet  = (!snap.imu.masterOnline && !snap.imu.slaveOnline) ||
                                   (snap.imu.isStationary &&
                                    snap.imu.combinedG < MotionPowerConfig::SUSTAINED_ACCEL_THRESH_G);

            if (speedIdle && imuQuiet) {
                _standstillAccumMs += dtMs;
            } else {
                _standstillAccumMs = 0UL;
            }

            const uint32_t standstillSec = _standstillAccumMs / 1000UL;
            {
                StateGuard guard(SystemState::instance());
                SystemState::instance().data().power.standstillSeconds = standstillSec;
                SystemState::instance().data().power.standbyPhase      = STANDBY_PHASE_ACTIVE;
            }

            if (_standstillAccumMs >= MotionPowerConfig::STANDSTILL_SLEEP_TIMEOUT_MS) {
                _phase        = STANDBY_PHASE_SETTLING_TRIPS;
                _phaseEnterMs = nowMs;
                StateGuard guard(SystemState::instance());
                PowerData& p = SystemState::instance().data().power;
                p.standbyPhase      = STANDBY_PHASE_SETTLING_TRIPS;
                p.preSleepSettleAck = false;
                p.preSleepSettleReq = true;
            }
            break;
        }

        case STANDBY_PHASE_SETTLING_TRIPS: {
            const bool acked    = snap.power.preSleepSettleAck;
            const bool timedOut = (nowMs - _phaseEnterMs >= SETTLE_HANDSHAKE_TIMEOUT_MS);
            if (!acked && !timedOut) {
                break;
            }
            if (timedOut && !acked) {
                // Failsafe direct flush if StorageTask was busy
                TrackLogger::instance().flushAndCloseAllStreams(true);
            }

            const bool canCloudSync = snap.network.staConnected &&
                                      snap.oneDrive.isBound &&
                                      snap.oneDrive.autoSyncEnabled &&
                                      snap.settings.oneDriveAutoSync;
            if (canCloudSync) {
                _phase        = STANDBY_PHASE_PRE_SLEEP_SYNC;
                _phaseEnterMs = nowMs;
                StateGuard guard(SystemState::instance());
                PowerData& p = SystemState::instance().data().power;
                p.preSleepSettleReq    = false;
                p.preSleepSettleAck    = false;
                p.standbyPhase         = STANDBY_PHASE_PRE_SLEEP_SYNC;
                p.preSleepCloudSyncAck = false;
                p.preSleepCloudSyncReq = true;
            } else {
                {
                    StateGuard guard(SystemState::instance());
                    PowerData& p = SystemState::instance().data().power;
                    p.preSleepSettleReq = false;
                    p.preSleepSettleAck = false;
                }
                enterStandbySleep(nowMs, snap);
            }
            break;
        }

        case STANDBY_PHASE_PRE_SLEEP_SYNC: {
            const bool syncDone = snap.power.preSleepCloudSyncAck;
            const bool timedOut = (nowMs - _phaseEnterMs >= CloudConfig::ONEDRIVE_PRE_SLEEP_TIMEOUT_MS);
            if (syncDone || timedOut) {
                {
                    StateGuard guard(SystemState::instance());
                    PowerData& p = SystemState::instance().data().power;
                    p.preSleepCloudSyncReq = false;
                    p.preSleepCloudSyncAck = false;
                }
                enterStandbySleep(nowMs, snap);
            }
            break;
        }

        case STANDBY_PHASE_SLEEPING: {
            // Wake Condition 1: Direct touch on GT911 screen
            if (M5.Touch.isEnabled() && M5.Touch.getCount() > 0) {
                wakeFromStandby("TOUCH_SCREEN", snap);
                break;
            }

            // Wake Condition 2: External vehicle USB-C / HVIN newly plugged in
            if (!_extPowerAtSleepEntry && _isExternalPower) {
                wakeFromStandby("EXTERNAL_POWER_ON", snap);
                break;
            }
            _extPowerAtSleepEntry = _isExternalPower;

            // Wake Condition 3: GNSS driving speed or sustained >=0.12G vehicle motion for 1.5s
            if (snap.gnss.isValid && snap.gnss.speedKmh > GnssConfig::RECORD_START_SPEED_KMH) {
                wakeFromStandby("GNSS_SPEED_MOVE", snap);
                break;
            }

            const bool imuMoving = (!snap.imu.isStationary &&
                                    snap.imu.accelVariance > MotionPowerConfig::STATIC_VARIANCE_THRESH * 4.0f) ||
                                   (snap.imu.combinedG >= MotionPowerConfig::SUSTAINED_ACCEL_THRESH_G);
            if (imuMoving) {
                _wakeMotionAccumMs += dtMs;
                if (_wakeMotionAccumMs >= MotionPowerConfig::SUSTAINED_ACCEL_MS) {
                    wakeFromStandby("IMU_VEHICLE_MOTION", snap);
                }
            } else if (_wakeMotionAccumMs > dtMs) {
                _wakeMotionAccumMs -= dtMs;
            } else {
                _wakeMotionAccumMs = 0UL;
            }
            break;
        }

        case STANDBY_PHASE_WAKING_UP:
        default:
            _phase = STANDBY_PHASE_ACTIVE;
            break;
    }
}

void PowerMonitorService::update() {
    const uint32_t nowMs = millis();

    static SystemStateSnapshot s_powerSnap;
    if (!SystemState::instance().getSnapshot(s_powerSnap, pdMS_TO_TICKS(20))) {
        return;
    }

    if (nowMs - _lastSampleMs >= Ina226Config::SAMPLE_INTERVAL_MS) {
        sampleAndIntegrateCoulomb(nowMs, s_powerSnap);
    }

    stepStandbyStateMachine(nowMs, s_powerSnap);
}
