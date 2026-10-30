#include "service/PowerMonitorService.h"
#include "service/TrackLogger.h"
#include "driver/I2cBusManager.h"
#include <M5Unified.h>
#include <Preferences.h>
#include <math.h>

float PowerMonitorService::lookupOcvSocPercent(float busVoltV) {
    uint16_t mv = (uint16_t)(busVoltV * 1000.0f);
    if (mv >= OCV_TABLE[0].voltageMv) return 100.0f;
    if (mv <= OCV_TABLE[10].voltageMv) return 0.0f;

    for (int i = 0; i < 10; i++) {
        uint16_t vHigh = OCV_TABLE[i].voltageMv;
        uint16_t vLow  = OCV_TABLE[i + 1].voltageMv;
        if (mv <= vHigh && mv >= vLow) {
            float ratio = (float)(mv - vLow) / (float)(vHigh - vLow);
            return OCV_TABLE[i + 1].socPercent + ratio * (OCV_TABLE[i].socPercent - OCV_TABLE[i + 1].socPercent);
        }
    }
    return 50.0f;
}

void PowerMonitorService::init(Ina226Driver* ina) {
    _ina = ina;
    _lastSampleMs = millis();
    _standstillStartMs = millis();

    if (_ina && _ina->isAvailable()) {
        float vBus = 0, vShunt = 0, curMa = 0, pwrMw = 0;
        if (_ina->readMeasurements(vBus, vShunt, curMa, pwrMw) && vBus > 4.5f) {
            float soc = lookupOcvSocPercent(vBus);
            _remainingMah = CoulombCounterConfig::BATTERY_RATED_MAH * (soc / 100.0f);
            _ocvInitialized = true;
        }
    }
}

void PowerMonitorService::update() {
    uint32_t now = millis();
    if (now - _lastSampleMs < Ina226Config::SAMPLE_INTERVAL_MS) return;
    float dtHours = (now - _lastSampleMs) / 3600000.0f;
    _lastSampleMs = now;

    float vBus = 8.15f, vShunt = 0.0f, curMa = 120.0f, pwrMw = 980.0f;
    bool inaOk = false;

    if (_ina && _ina->isAvailable()) {
        inaOk = _ina->readMeasurements(vBus, vShunt, curMa, pwrMw);
    } else {
        // Fallback to M5.Power when INA226 is not yet probed
        I2cBusManager::instance().lockBus();
        int16_t m5Mv = M5.Power.getBatteryVoltage();
        int32_t m5Ma = M5.Power.getBatteryCurrent();
        I2cBusManager::instance().unlockBus();
        if (m5Mv > 3000) {
            vBus  = (m5Mv < 4500) ? (m5Mv * 2.0f / 1000.0f) : (m5Mv / 1000.0f);
            curMa = (float)m5Ma;
            pwrMw = vBus * fabsf(curMa);
        }
    }

    if (!_ocvInitialized && vBus > 5.0f) {
        float initSoc = lookupOcvSocPercent(vBus);
        _remainingMah   = CoulombCounterConfig::BATTERY_RATED_MAH * (initSoc / 100.0f);
        _ocvInitialized = true;
    }

    // 1. Coulomb Integration: dQ = I * dt * eta
    float eta = (curMa >= 0.0f) ? CoulombCounterConfig::CHARGE_EFFICIENCY_ETA
                                : CoulombCounterConfig::DISCHARGE_EFFICIENCY;
    float dQ = curMa * dtHours * eta;
    _remainingMah += dQ;
    if (_remainingMah > CoulombCounterConfig::BATTERY_RATED_MAH) _remainingMah = CoulombCounterConfig::BATTERY_RATED_MAH;
    if (_remainingMah < 0.0f) _remainingMah = 0.0f;

    // 2. OCV Zero-Drift Auto Calibration when |I| < 20mA for >= 5 seconds
    if (fabsf(curMa) < CoulombCounterConfig::STANDSTILL_CURRENT_MA && vBus >= 5.5f) {
        if (_lowCurrentStartMs == 0) _lowCurrentStartMs = now;
        if (now - _lowCurrentStartMs >= CoulombCounterConfig::STANDSTILL_SETTLE_MS) {
            float ocvSoc = lookupOcvSocPercent(vBus);
            float targetMah = CoulombCounterConfig::BATTERY_RATED_MAH * (ocvSoc / 100.0f);
            _remainingMah = _remainingMah * 0.8f + targetMah * 0.2f;
        }
    } else {
        _lowCurrentStartMs = 0;
    }

    _avgCurrentMa = _avgCurrentMa * 0.9f + curMa * 0.1f;
    float socPct  = (_remainingMah / CoulombCounterConfig::BATTERY_RATED_MAH) * 100.0f;
    if (socPct > 100.0f) socPct = 100.0f;
    if (socPct < 0.0f)   socPct = 0.0f;

    I2cBusManager::instance().lockBus();
    bool m5Chg  = M5.Power.isCharging();
    int  vbusMv = M5.Power.getVBUSVoltage();
    I2cBusManager::instance().unlockBus();

    bool isCharging = (curMa > 15.0f) || m5Chg;
    bool hasExtPwr  = isCharging || (curMa >= -5.0f) || (vbusMv > 4400);

    uint16_t remMin = 0;
    if (isCharging && _avgCurrentMa > 10.0f) {
        remMin = (uint16_t)(((CoulombCounterConfig::BATTERY_RATED_MAH - _remainingMah) / _avgCurrentMa) * 60.0f);
    } else if (_avgCurrentMa < -10.0f) {
        remMin = (uint16_t)((_remainingMah / fabsf(_avgCurrentMa)) * 60.0f);
    } else {
        remMin = (uint16_t)((_remainingMah / 350.0f) * 60.0f);
    }

    SystemState& state = SystemState::instance();
    state.lock();
    SystemContext& ctx = state.getContextUnsafe();
    ctx.power.isAvailable       = inaOk;
    ctx.power.hasExternalPower  = hasExtPwr;
    ctx.power.isCharging        = isCharging;
    ctx.power.powerSource       = hasExtPwr ? POWER_SOURCE_EXTERNAL_USB : POWER_SOURCE_BATTERY_NPF550;
    ctx.power.uiDisplayColor    = hasExtPwr ? PowerColorConfig::COLOR_EXTERNAL_POWER_GREEN
                                            : PowerColorConfig::COLOR_BATTERY_ONLY_RED;
    ctx.power.busVoltageV       = vBus;
    ctx.power.shuntVoltageMv    = vShunt;
    ctx.power.currentMa         = curMa;
    ctx.power.avgCurrentMa      = _avgCurrentMa;
    ctx.power.powerMw           = pwrMw;
    ctx.power.remainingMah      = _remainingMah;
    ctx.power.batterySocPercent = socPct;
    ctx.power.remainingMinutes  = remMin;

    // 3. Standby & Wakeup State Machine (Automotive Power Management)
    // Vehicle movement threshold: Sustained >0.15G or >0.10 rad/s gyro for >=1.5s
    // (Filtering out short cabin disturbances like sitting down, door slaps, or shifting in seat)
    bool instantDisturbance = (fabsf(ctx.imu.fusedGyroZ) > 0.08f) || (ctx.imu.totalG > 0.12f) ||
                             (ctx.gnss.speedKmh > MotionPowerConfig::STANDSTILL_HEADING_LOCK_KMH);

    if (instantDisturbance) {
        if (_sustainedMoveStartMs == 0) _sustainedMoveStartMs = now;
    } else {
        _sustainedMoveStartMs = 0;
    }

    bool sustainedVehicleMotion = (_sustainedMoveStartMs > 0) &&
                                  (now - _sustainedMoveStartMs >= MotionPowerConfig::SUSTAINED_ACCEL_MS);

    // Power Recovery Wakeup: External charger plugged in / vehicle engine started (Vbus rises)
    bool chargerRestored = hasExtPwr && !_hadExternalPowerBefore;
    _hadExternalPowerBefore = hasExtPwr;

    if (_isStandbySleep) {
        // In standby sleep mode: check for wake conditions
        if (chargerRestored || sustainedVehicleMotion) {
            state.unlock();
            exitStandstillStandby();
            return;
        }
    } else {
        // Normal running mode: check for standstill sleep condition
        // If external power is connected (charging in car), never sleep!
        if (hasExtPwr || sustainedVehicleMotion || instantDisturbance) {
            _standstillStartMs = now;
        } else if (now - _standstillStartMs >= MotionPowerConfig::STANDSTILL_TIMEOUT_MS) {
            state.unlock();
            enterStandstillStandby();
            return;
        }
    }
    state.unlock();
}

void PowerMonitorService::enterStandstillStandby() {
    Serial.println("[POWER] Standstill 20 minutes (battery only) reached -> Entering Low-Power Standby...");
    _isStandbySleep = true;
    _sustainedMoveStartMs = 0;

    TrackLogger::instance().closeActiveStreams();

    SystemState& state = SystemState::instance();
    state.lock();
    bool    gnssValid = state.getContextUnsafe().gnss.isValid;
    int32_t latE7     = state.getContextUnsafe().gnss.latE7;
    int32_t lonE7     = state.getContextUnsafe().gnss.lonE7;
    int32_t altCm     = (int32_t)(state.getContextUnsafe().gnss.altitudeMeters * 100.0f);
    _savedBrightness  = state.getContextUnsafe().screenBrightness;
    state.unlock();

    if (gnssValid && latE7 != 0) {
        Preferences prefs;
        if (prefs.begin("m5track", false)) {
            prefs.putInt("last_lat_e7", latE7);
            prefs.putInt("last_lon_e7", lonE7);
            prefs.putInt("last_alt_cm", altCm);
            prefs.end();
        }
    }

    // Turn off high-power screen backlight and sleep LCD
    M5.Display.setBrightness(0);
    M5.Display.sleep();

    // Throttle CPU to low frequency for sub-20mA standby while background tasks monitor IMU & Vbus
    setCpuFrequencyMhz(80);
    Serial.println("[POWER] Display turned off, CPU throttled to 80MHz (Monitoring 6-axis IMU & Charger Wakeup).");
}

void PowerMonitorService::exitStandstillStandby() {
    Serial.println("[POWER] >>> WAKEUP TRIGGERED: Car Charger Restored OR Sustained Vehicle Movement! <<<");
    _isStandbySleep = false;
    _standstillStartMs = millis();
    _sustainedMoveStartMs = 0;

    // Restore full 400MHz dual-core performance
    setCpuFrequencyMhz(400);

    // Wake display and restore backlight
    M5.Display.wakeup();
    uint8_t br = (_savedBrightness > 0) ? _savedBrightness : 200;
    M5.Display.setBrightness(br);

    Serial.printf("[POWER] Normal running mode restored. Screen brightness: %u\n", br);
}
