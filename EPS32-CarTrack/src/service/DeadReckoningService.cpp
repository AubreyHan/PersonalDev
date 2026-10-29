#include "service/DeadReckoningService.h"
#include <math.h>

void DeadReckoningService::init() {
    _inDrMode = false;
    _drDurationSec = 0;
    _drHeadingDeg = 0.0f;
    _drSpeedKmh = 0.0f;
    _lastDrEpoch = millis();
    _lastGnssValidTime = millis();
    _lastImuUpdateMs = millis();
}

void DeadReckoningService::update(bool gnssValid, float speedKmh, float headingDeg, int32_t latE7, int32_t lonE7) {
    unsigned long now = millis();
    float dt = (now - _lastImuUpdateMs) / 1000.0f;
    if (dt <= 0.001f) return;
    _lastImuUpdateMs = now;

    // 1. Read IMU (BMI270)
    float ax = 0, ay = 0, az = 0;
    float gx = 0, gy = 0, gz = 0;
    if (M5.Imu.isEnabled()) {
        M5.Imu.getAccel(&ax, &ay, &az);
        M5.Imu.getGyro(&gx, &gy, &gz);
    }

    // 2. Compute pitch, roll, and lateral/long G
    // Core2 mounted flat:
    // ax: Lateral G (left/right centrifugal)
    // ay: Longitudinal G (push back/braking)
    // az: Vertical (1.0G gravity)
    float latG = ax;
    float lonG = ay;
    float totalG = sqrtf(latG * latG + lonG * lonG);

    // Compute Euler angles
    float pitch = atan2f(-ay, sqrtf(ax * ax + az * az)) * 57.29578f;
    float roll  = atan2f(ax, az) * 57.29578f;

    SystemState& state = SystemState::instance();
    state.lock();
    SystemContext& ctx = state.getContextUnsafe();

    ctx.dynamics.lateralG = latG;
    ctx.dynamics.longG = lonG;
    ctx.dynamics.totalG = totalG;
    if (fabs(latG) > ctx.dynamics.maxLateralG) ctx.dynamics.maxLateralG = fabs(latG);
    if (fabs(lonG) > ctx.dynamics.maxLongG) ctx.dynamics.maxLongG = fabs(lonG);
    ctx.dynamics.pitchDeg = pitch;
    ctx.dynamics.rollDeg = roll;

    // 3. Dead Reckoning Transition Logic
    if (gnssValid) {
        _lastGnssValidTime = now;
        _inDrMode = false;
        _drDurationSec = 0;
        _drHeadingDeg = headingDeg;
        _drSpeedKmh = speedKmh;
    } else {
        if (now - _lastGnssValidTime > 1500) {
            // Lost fix for > 1.5s -> Enter DR mode
            _inDrMode = true;
            _drDurationSec = (now - _lastGnssValidTime) / 1000;

            // Heading integration: theta_t = theta_{t-1} + omega_z * dt
            _drHeadingDeg += gz * dt;
            while (_drHeadingDeg < 0.0f) _drHeadingDeg += 360.0f;
            while (_drHeadingDeg >= 360.0f) _drHeadingDeg -= 360.0f;

            // Slowly decay speed if no positive longitudinal acceleration
            if (_drSpeedKmh > 0.0f) {
                _drSpeedKmh += lonG * 9.8f * 3.6f * dt;
                if (_drSpeedKmh < 0.0f) _drSpeedKmh = 0.0f;
            }

            // Project dead reckoning coordinates if under 60 seconds
            if (_drDurationSec <= 60 && _drSpeedKmh > 2.0f) {
                float distM = (_drSpeedKmh / 3.6f) * dt;
                float rad = _drHeadingDeg * 0.01745329f;
                double dLat = distM * cosf(rad) / 111139.0;
                double dLon = distM * sinf(rad) / (111139.0 * cosf(ctx.gnss.latitude * 0.01745329f));
                ctx.gnss.latitude += dLat;
                ctx.gnss.longitude += dLon;
                ctx.gnss.latE7 = (int32_t)(ctx.gnss.latitude * 1e7);
                ctx.gnss.lonE7 = (int32_t)(ctx.gnss.longitude * 1e7);
                ctx.gnss.speedKmh = _drSpeedKmh;
                ctx.gnss.headingDeg = _drHeadingDeg;
            }
        }
    }

    ctx.gnss.isDeadReckoning = _inDrMode;
    ctx.gnss.drDurationSec = _drDurationSec;

    state.unlock();
}
