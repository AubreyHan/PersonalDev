#include "service/PowerMonitorService.h"
#include "service/TrackLogger.h"
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
        int16_t m5Mv = M5.Power.getBatteryVoltage();
        int32_t m5Ma = M5.Power.getBatteryCurrent();
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

    bool isCharging = (curMa > 15.0f) || (M5.Power.isCharging());
    bool hasExtPwr  = isCharging || (curMa >= -5.0f) || (M5.Power.getVBUSVoltage() > 4400);

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

    // 3. Standstill 20-Minute (1200s) Auto Stop & Deep Sleep Check (PRD 3.8)
    bool moving = (ctx.gnss.speedKmh > MotionPowerConfig::STANDSTILL_HEADING_LOCK_KMH) ||
                  (fabsf(ctx.imu.fusedGyroZ) > 0.04f) ||
                  (fabsf(ctx.imu.totalG) > 0.08f);
    if (moving) {
        _standstillStartMs = now;
    } else if (now - _standstillStartMs >= MotionPowerConfig::STANDSTILL_TIMEOUT_MS) {
        state.unlock();
        enterStandstillDeepSleep();
        return;
    }
    state.unlock();
}

void PowerMonitorService::enterStandstillDeepSleep() {
    Serial.println("[POWER] Standstill 20 minutes reached -> Closing GPX, saving NVS coordinates, entering Deep Sleep...");
    TrackLogger::instance().closeActiveStreams();

    SystemContext snap = SystemState::instance().getSnapshot();
    if (snap.gnss.isValid && snap.gnss.latE7 != 0) {
        Preferences prefs;
        if (prefs.begin("m5track", false)) {
            prefs.putInt("last_lat_e7", snap.gnss.latE7);
            prefs.putInt("last_lon_e7", snap.gnss.lonE7);
            prefs.putInt("last_alt_cm", (int32_t)(snap.gnss.altitudeMeters * 100.0f));
            prefs.end();
        }
    }

    M5.Display.setBrightness(0);
    M5.Display.sleep();
    M5.Power.deepSleep(0, true);
}
