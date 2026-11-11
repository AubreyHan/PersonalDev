#include "AltitudeFusion.h"

#include <Preferences.h>
#include <math.h>
#include "../model/SystemState.h"

namespace {
    constexpr float MIN_VALID_P0_HPA       = 850.0f;
    constexpr float MAX_VALID_P0_HPA       = 1085.0f;
    constexpr float CLIMB_HYSTERESIS_M     = 1.5f;   // Ignore < 1.5m baro micro-ripples
    constexpr uint32_t P0_NVS_SAVE_MIN_MS  = 600000UL; // 10 min wear-leveling guard

    float clampf(float v, float lo, float hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }
}

AltitudeFusion& AltitudeFusion::instance() {
    static AltitudeFusion s_instance;
    return s_instance;
}

float AltitudeFusion::pressureToAltitudeM(float pressureHpa, float seaLevelHpa) {
    if (pressureHpa <= 10.0f || seaLevelHpa <= 10.0f) {
        return 0.0f;
    }
    return 44330.0f * (1.0f - powf(pressureHpa / seaLevelHpa, 0.190294957f));
}

float AltitudeFusion::invertSeaLevelPressureHpa(float pressureHpa, float altitudeM) {
    const float clampedAlt = clampf(altitudeM, -500.0f, 9000.0f);
    const float denom      = powf(1.0f - (clampedAlt / 44330.0f), 5.255f);
    if (denom <= 1e-4f) {
        return BaroFusionConfig::DEFAULT_SEA_LEVEL_HPA;
    }
    return clampf(pressureHpa / denom, MIN_VALID_P0_HPA, MAX_VALID_P0_HPA);
}

void AltitudeFusion::saveSeaLevelToNvs(float p0Hpa) {
    Preferences prefs;
    if (prefs.begin(NvsConfig::NAMESPACE, false)) {
        prefs.putFloat(NvsConfig::KEY_SEA_P0_HPA, p0Hpa);
        prefs.end();
        _lastSavedP0Hpa = p0Hpa;
    }
}

bool AltitudeFusion::begin() {
    float savedP0   = 0.0f;
    int32_t lastAltCm = 0;

    Preferences prefs;
    if (prefs.begin(NvsConfig::NAMESPACE, true)) {
        savedP0   = prefs.getFloat(NvsConfig::KEY_SEA_P0_HPA, 0.0f);
        lastAltCm = prefs.getInt(NvsConfig::KEY_LAST_ALT_CM, 0);
        prefs.end();
    }

    bool baroOnline   = false;
    float ambientHpa  = BaroFusionConfig::DEFAULT_SEA_LEVEL_HPA;
    {
        StateGuard guard(SystemState::instance());
        if (guard) {
            const auto& st   = SystemState::instance().data();
            baroOnline       = st.baro.isOnline;
            ambientHpa       = st.baro.pressureHpa;
            _seenResetVersion = st.tripControl.tripResetVersion;
            _seenLocalDayYmd  = st.tripControl.lastLocalDayYmd;
            _seenManualSubIdx = st.tripControl.manualSubIndex;
        }
    }

    if (isfinite(savedP0) && savedP0 >= MIN_VALID_P0_HPA && savedP0 <= MAX_VALID_P0_HPA) {
        _seaLevelHpa    = savedP0;
        _lastSavedP0Hpa = savedP0;
        _p0Calibrated   = true;
    } else if (baroOnline && lastAltCm != 0) {
        _seaLevelHpa    = invertSeaLevelPressureHpa(ambientHpa, static_cast<float>(lastAltCm) * 0.01f);
        _lastSavedP0Hpa = _seaLevelHpa;
        _p0Calibrated   = true;
    } else {
        _seaLevelHpa    = BaroFusionConfig::DEFAULT_SEA_LEVEL_HPA;
        _lastSavedP0Hpa = _seaLevelHpa;
        _p0Calibrated   = false;
    }

    if (baroOnline) {
        const float rawAlt = pressureToAltitudeM(ambientHpa, _seaLevelHpa);
        _prevRawBaroAltM   = rawAlt;
        _fusedAltM         = rawAlt;
        _baroSeeded        = true;
    } else if (lastAltCm != 0) {
        _fusedAltM = static_cast<float>(lastAltCm) * 0.01f;
    }

    {
        StateGuard guard(SystemState::instance());
        if (guard) {
            auto& baro               = SystemState::instance().data().baro;
            baro.seaLevelPressureHpa = _seaLevelHpa;
            baro.isP0Calibrated      = _p0Calibrated;
            baro.rawBaroAltitudeM    = _prevRawBaroAltM;
            baro.fusedAltitudeM      = _fusedAltM;
        }
    }
    return true;
}

void AltitudeFusion::update(uint32_t nowMs) {
    if (_lastUpdateMs != 0 && (nowMs - _lastUpdateMs) < BaroFusionConfig::SAMPLE_INTERVAL_MS) {
        return;
    }
    const float dtSec = (_lastUpdateMs == 0)
                            ? (static_cast<float>(BaroFusionConfig::SAMPLE_INTERVAL_MS) * 0.001f)
                            : clampf(static_cast<float>(nowMs - _lastUpdateMs) * 0.001f, 0.02f, 1.0f);
    _lastUpdateMs = nowMs;

    // Snapshot minimal sensor inputs under lock
    bool  baroOnline  = false;
    float pressureHpa = BaroFusionConfig::DEFAULT_SEA_LEVEL_HPA;
    bool  gnss3dValid = false;
    float gnssAltM    = 0.0f;
    float gnssHdop    = 99.9f;
    float speedKmh    = 0.0f;
    bool  imuStatic   = true;

    {
        StateGuard guard(SystemState::instance());
        if (guard) {
            const auto& st = SystemState::instance().data();
            baroOnline     = st.baro.isOnline;
            pressureHpa    = st.baro.pressureHpa;
            gnss3dValid    = (st.gnss.fixType >= FIX_3D) &&
                             st.gnss.gnssFixOk &&
                             !st.gnss.isDeadReckoning &&
                             !st.gnss.isInTunnel;
            gnssAltM       = st.gnss.altitudeMeters;
            gnssHdop       = st.gnss.hdop;
            speedKmh       = st.gnss.speedKmh;
            imuStatic      = st.imu.isStationary;

            // Reset per-tier climb hysteresis anchors if trip reset / rollover occurred
            if (st.tripControl.tripResetVersion != _seenResetVersion) {
                _seenResetVersion              = st.tripControl.tripResetVersion;
                _climbAnchorInit[TRIP_SINGLE]  = false;
            }
            if (st.tripControl.lastLocalDayYmd != _seenLocalDayYmd) {
                _seenLocalDayYmd               = st.tripControl.lastLocalDayYmd;
                _climbAnchorInit[TRIP_DAILY]   = false;
            }
            if (st.tripControl.manualSubIndex != _seenManualSubIdx) {
                _seenManualSubIdx              = st.tripControl.manualSubIndex;
                _climbAnchorInit[TRIP_MANUAL]  = false;
            }
        }
    }

    const float prevFusedAltM = _fusedAltM;
    bool saveP0Now = false;

    // 1. Sea-level pressure P0 calibration from valid 3D GNSS altitude
    if (gnss3dValid && isfinite(gnssAltM)) {
        if (!_firstGnssAnchored && gnssHdop <= BaroFusionConfig::P0_INITIAL_MAX_HDOP) {
            if (baroOnline) {
                _seaLevelHpa      = invertSeaLevelPressureHpa(pressureHpa, gnssAltM);
                _prevRawBaroAltM  = pressureToAltitudeM(pressureHpa, _seaLevelHpa);
                _baroSeeded       = true;
                _p0Calibrated     = true;
                _lastP0SaveMs     = nowMs;
                saveP0Now         = true;
            }
            _fusedAltM         = gnssAltM;
            _firstGnssAnchored = true;
        } else if (baroOnline && gnssHdop <= BaroFusionConfig::P0_CALIB_MAX_HDOP) {
            const float targetP0 = invertSeaLevelPressureHpa(pressureHpa, gnssAltM);
            _seaLevelHpa += BaroFusionConfig::P0_CALIB_ALPHA * (targetP0 - _seaLevelHpa);
            _seaLevelHpa  = clampf(_seaLevelHpa, MIN_VALID_P0_HPA, MAX_VALID_P0_HPA);
            _p0Calibrated = true;

            if ((nowMs - _lastP0SaveMs) >= P0_NVS_SAVE_MIN_MS &&
                fabsf(_seaLevelHpa - _lastSavedP0Hpa) >= 0.25f) {
                _lastP0SaveMs = nowMs;
                saveP0Now     = true;
            }
        }
    }

    // 2. 10Hz Baro + GNSS Complementary Altitude Filter
    float rawBaroAltM = _prevRawBaroAltM;
    if (baroOnline) {
        rawBaroAltM = pressureToAltitudeM(pressureHpa, _seaLevelHpa);
        if (!_baroSeeded) {
            _prevRawBaroAltM = rawBaroAltM;
            _fusedAltM       = rawBaroAltM;
            _baroSeeded      = true;
        } else {
            const float deltaBaro = clampf(rawBaroAltM - _prevRawBaroAltM, -8.0f, 8.0f);
            _prevRawBaroAltM      = rawBaroAltM;

            const float predAlt = _fusedAltM + deltaBaro;
            if (gnss3dValid && isfinite(gnssAltM)) {
                // Complementary fusion: 100% instant high-pass baro delta + gentle GNSS low-pass anchor
                const float gnssAnchorGain = (1.0f - BaroFusionConfig::BARO_DIFF_WEIGHT) * 0.20f; // 0.05 @ 10Hz (~2s tau)
                _fusedAltM = predAlt + gnssAnchorGain * (gnssAltM - predAlt);
            } else {
                // Inside tunnel / blind-zone DR: pure drift-controlled barometric elevation tracking
                _fusedAltM = predAlt;
            }
        }
    } else if (gnss3dValid && isfinite(gnssAltM)) {
        if (!_firstGnssAnchored) {
            _fusedAltM         = gnssAltM;
            _firstGnssAnchored = true;
        } else {
            _fusedAltM += 0.15f * (gnssAltM - _fusedAltM);
        }
        rawBaroAltM = _fusedAltM;
    }

    // 3. Vertical speed (m/s) & Real-Time Road Grade (%)
    const float rawVertSpd = (_fusedAltM - prevFusedAltM) / dtSec;
    _verticalSpeedMps += 0.20f * (clampf(rawVertSpd, -25.0f, 25.0f) - _verticalSpeedMps);
    if (imuStatic && speedKmh <= GnssConfig::STATIC_DEADBAND_KMH && fabsf(_verticalSpeedMps) < 0.08f) {
        _verticalSpeedMps = 0.0f;
    }

    if (speedKmh > 3.6f) {
        const float stepDistM = (speedKmh / 3.6f) * dtSec;
        _cumHorizDistM += stepDistM;

        // Push anchor into distance ring every >= 1.5m traveled
        const uint8_t lastIdx = (_slopeCount == 0)
                                    ? 0
                                    : static_cast<uint8_t>((_slopeHead + SLOPE_RING_SIZE - 1U) % SLOPE_RING_SIZE);
        if (_slopeCount == 0 || (_cumHorizDistM - _slopeRing[lastIdx].distM) >= 1.5f) {
            _slopeRing[_slopeHead] = { _cumHorizDistM, _fusedAltM };
            _slopeHead = static_cast<uint8_t>((_slopeHead + 1U) % SLOPE_RING_SIZE);
            if (_slopeCount < SLOPE_RING_SIZE) {
                _slopeCount++;
            }
        }

        if (_slopeCount >= 2U) {
            const uint8_t oldestIdx = (_slopeCount < SLOPE_RING_SIZE) ? 0U : _slopeHead;
            const float dS = _cumHorizDistM - _slopeRing[oldestIdx].distM;
            if (dS >= 10.0f) {
                const float dH        = _fusedAltM - _slopeRing[oldestIdx].altM;
                const float instSlope = clampf((dH / dS) * 100.0f, -35.0f, 35.0f);
                _roadSlopePct += 0.20f * (instSlope - _roadSlopePct);
            }
        }
    } else if (speedKmh <= GnssConfig::STATIC_DEADBAND_KMH) {
        _roadSlopePct *= 0.96f;
        if (fabsf(_roadSlopePct) < 0.1f) {
            _roadSlopePct = 0.0f;
        }
    }

    const bool steepWarn = (fabsf(_roadSlopePct) >= BaroFusionConfig::STEEP_SLOPE_WARN_PERCENT);
    const bool altReliable = baroOnline || _firstGnssAnchored;

    // 4. Publish fused altitude, grade & update 3-tier TripStats
    {
        StateGuard guard(SystemState::instance());
        if (guard) {
            auto& st                 = SystemState::instance().data();
            st.baro.seaLevelPressureHpa = _seaLevelHpa;
            st.baro.isP0Calibrated   = _p0Calibrated;
            st.baro.rawBaroAltitudeM = rawBaroAltM;
            st.baro.fusedAltitudeM   = _fusedAltM;
            st.baro.verticalSpeedMps = _verticalSpeedMps;
            st.baro.roadSlopePercent = _roadSlopePct;
            st.baro.steepSlopeWarning = steepWarn;

            if (altReliable) {
                const bool vehicleMoving = (speedKmh > GnssConfig::STATIC_DEADBAND_KMH);
                for (uint8_t cat = 0; cat < TRIP_CATEGORY_COUNT; ++cat) {
                    if (cat == TRIP_MANUAL && !st.tripControl.manualTripActive) {
                        continue;
                    }
                    auto& trip            = st.trips[cat];
                    trip.currentAltitudeM = _fusedAltM;
                    trip.verticalSpeedMps = _verticalSpeedMps;

                    if (!trip.altitudeInitialized) {
                        trip.maxAltitudeM        = _fusedAltM;
                        trip.minAltitudeM        = _fusedAltM;
                        trip.altitudeInitialized = true;
                        _climbAnchorM[cat]       = _fusedAltM;
                        _climbAnchorInit[cat]    = true;
                    } else {
                        if (_fusedAltM > trip.maxAltitudeM) trip.maxAltitudeM = _fusedAltM;
                        if (_fusedAltM < trip.minAltitudeM) trip.minAltitudeM = _fusedAltM;

                        if (!_climbAnchorInit[cat]) {
                            _climbAnchorM[cat]    = _fusedAltM;
                            _climbAnchorInit[cat] = true;
                        } else if (vehicleMoving) {
                            const float diff = _fusedAltM - _climbAnchorM[cat];
                            if (diff >= CLIMB_HYSTERESIS_M) {
                                trip.cumulativeClimbM += diff;
                                _climbAnchorM[cat]     = _fusedAltM;
                            } else if (diff <= -CLIMB_HYSTERESIS_M) {
                                trip.cumulativeDescentM += (-diff);
                                _climbAnchorM[cat]       = _fusedAltM;
                            }
                        }
                    }
                }
            }
        }
    }

    if (saveP0Now) {
        saveSeaLevelToNvs(_seaLevelHpa);
    }
}
