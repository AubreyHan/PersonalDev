#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool has_fix;               /* true when fix_quality >= 1 */
    int fix_quality;            /* 0 = invalid, 1 = GPS fix, 2 = DGPS */
    int satellites_used;        /* Satellites used in fix (from GGA) */
    int satellites_in_view;     /* Satellites with SNR > 0 (from GSV) */
    int avg_snr_db;             /* Average SNR of top 4 satellites in dB-Hz (0..50) */
    int signal_percent;         /* Mapped 0..100% for left GPS arc */
    int fix_mode;               /* GSA fix mode: 1 = no fix, 2 = 2D fix, 3 = 3D fix */
    float pdop;                 /* Position dilution of precision (from GSA) */
    float hdop;                 /* Horizontal dilution of precision */
    float vdop;                 /* Vertical dilution of precision (from GSA) */
    float geoid_sep_m;          /* Geoid separation / undulation N (meters, WGS84 - MSL) */
    float ellipsoid_alt_m;      /* WGS-84 ellipsoidal height (meters) */
    float raw_altitude_m;       /* Geoid-compensated MSL altitude before filter (meters) */
    float smoothed_altitude_m;  /* 1D-Kalman + sliding-average filtered altitude (meters) */
    int32_t altitude_int_m;     /* Rounded integer altitude (meters) for display */
    bool alt_quality_ok;        /* true when current epoch passed VDOP & 3D quality filter */
    bool has_session_fix;       /* true once at least one valid 3D fix occurred since power-on */
    int32_t min_altitude_int_m; /* Minimum integer altitude (meters) since power-on */
    int32_t max_altitude_int_m; /* Maximum integer altitude (meters) since power-on */
    uint32_t fix_epoch_seq;     /* Increments on each valid GGA fix epoch (1 Hz) */
    double latitude;            /* Decimal degrees */
    double longitude;           /* Decimal degrees */
    float speed_kmh;            /* Ground speed in km/h */
    int32_t speed_int_kmh;      /* Rounded integer ground speed (km/h) for display */
    int32_t max_speed_int_kmh;  /* Maximum integer ground speed (km/h) since power-on */
    float course_deg;           /* Course over ground in degrees */
    bool time_valid;            /* true once valid UTC time is synced from GNSS */
    bool date_valid;            /* true once valid UTC date is synced from GNSS */
    int utc_year;               /* e.g. 2026 */
    int utc_month;              /* 1..12 */
    int utc_day;                /* 1..31 */
    int utc_hour;               /* 0..23 */
    int utc_minute;             /* 0..59 */
    int utc_second;             /* 0..59 */
    int tz_offset_minutes;      /* Auto-detected timezone offset from UTC in minutes (e.g. +480 for UTC+8) */
    int local_year;             /* e.g. 2026 */
    int local_month;            /* 1..12 */
    int local_day;              /* 1..31 */
    int local_hour;             /* 0..23 (24-hour format) */
    int local_minute;           /* 0..59 */
    int local_second;           /* 0..59 */
} gps_state_t;

/**
 * @brief Initialize Quectel LC76G GNSS over BSP I2C and start background polling task
 */
esp_err_t gps_lc76g_start(void);

/**
 * @brief Get latest thread-safe GPS state snapshot
 */
void gps_lc76g_get_state(gps_state_t *out_state);

#ifdef __cplusplus
}
#endif
