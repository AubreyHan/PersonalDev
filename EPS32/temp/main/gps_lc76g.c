#include "gps_lc76g.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/i2c_master.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"

static const char *TAG = "GPS_LC76G";

#define LC76G_I2C_ADDR_WRITE    0x50
#define LC76G_I2C_ADDR_READ     0x54
#define LC76G_I2C_FREQ_HZ       100000
#define LC76G_MAX_READ_LEN      8192

/* Altitude accuracy parameters:
 * 1) High-VDOP / 2D-fix outlier rejection thresholds
 * 2) EGM2008/EGM96 bilinear geoid separation compensation grid
 * 3) 1D Kalman filter + 7-point sliding window moving average */
#define VDOP_MAX_VALID          3.5f
#define HDOP_MAX_VALID          3.0f
#define MIN_SATS_FOR_3D_ALT     4
#define ALT_SLIDE_WIN_SIZE      7

static SemaphoreHandle_t s_gps_mutex = NULL;
static gps_state_t s_gps_state = {
    .fix_mode = 1,
    .pdop = 99.9f,
    .hdop = 99.9f,
    .vdop = 99.9f,
};

/* 1D Kalman filter + sliding window state for altitude */
static bool s_kf_initialized = false;
static float s_kf_x = 0.0f;
static float s_kf_p = 36.0f;
static float s_alt_win[ALT_SLIDE_WIN_SIZE] = {0};
static int s_alt_win_head = 0;
static int s_alt_win_count = 0;
static uint32_t s_good_alt_streak = 0;
static uint32_t s_alt_reject_streak = 0;
static uint32_t s_alt_reject_total = 0;

static i2c_master_dev_handle_t s_write_dev = NULL;
static i2c_master_dev_handle_t s_read_dev = NULL;
static esp_io_expander_handle_t s_io_exp = NULL;

/* Rolling buffer for GSV satellite SNR values within a 1Hz epoch (deduplicated by talker+PRN) */
#define MAX_TRACKED_SNRS 64
static int s_snr_list[MAX_TRACKED_SNRS];
static uint16_t s_sat_id_list[MAX_TRACKED_SNRS];
static int s_snr_count = 0;
static int s_empty_gsv_epochs = 0;
static int s_gsv_last_alm_view = 0;
static int s_gsv_epoch_alm_view = 0;
static bool s_gsv_epoch_closed = true;
static uint32_t s_gga_last_seq = 0;
static uint32_t s_rmc_last_seq = 0;
static char s_last_gga[128] = {0};
static char s_last_rmc[128] = {0};
static int s_tz_offset_minutes = 480; /* Default UTC+8; auto-updated from GPS coordinates & saved in NVS */
static bool s_rmc_date_valid = false;

/* NVS Last-Known Fix Cache & Indoor-to-Outdoor Fast Convergence State */
static bool s_has_nav_cache = false;
static double s_cached_lat = 0.0;
static double s_cached_lon = 0.0;
static float s_cached_alt_m = 0.0f;
static float s_cached_geoid_m = 0.0f;
static bool s_nav_cache_saved_this_boot = false;
static TickType_t s_last_nav_save_tick = 0;

static uint32_t s_indoor_epochs = 0;
static bool s_outdoor_fast_converge = false;
static volatile TickType_t s_outdoor_fast_poll_until = 0;

static void load_nav_cache_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open("gps_cfg", NVS_READONLY, &h) == ESP_OK) {
        int32_t saved_tz = 480;
        if (nvs_get_i32(h, "tz_min", &saved_tz) == ESP_OK) {
            if (saved_tz >= -720 && saved_tz <= 840) {
                s_tz_offset_minutes = (int)saved_tz;
            }
        }
        int32_t lat_e7 = 0, lon_e7 = 0, alt_dm = 0, geoid_dm = 0;
        if (nvs_get_i32(h, "lat_e7", &lat_e7) == ESP_OK &&
            nvs_get_i32(h, "lon_e7", &lon_e7) == ESP_OK &&
            nvs_get_i32(h, "alt_dm", &alt_dm) == ESP_OK) {
            s_cached_lat = (double)lat_e7 * 1e-7;
            s_cached_lon = (double)lon_e7 * 1e-7;
            s_cached_alt_m = (float)alt_dm * 0.1f;
            if (nvs_get_i32(h, "geoid_dm", &geoid_dm) == ESP_OK) {
                s_cached_geoid_m = (float)geoid_dm * 0.1f;
            }
            s_has_nav_cache = true;
            ESP_LOGI(TAG, "Loaded NVS Last-Known Fix: lat=%.5f, lon=%.5f, alt=%.1fm, geoid=%.1fm, UTC%+d",
                     s_cached_lat, s_cached_lon, s_cached_alt_m, s_cached_geoid_m, s_tz_offset_minutes / 60);
        }
        nvs_close(h);
    }
}

static void save_tz_to_nvs(int tz_min)
{
    nvs_handle_t h;
    if (nvs_open("gps_cfg", NVS_READWRITE, &h) == ESP_OK) {
        int32_t old_tz = 9999;
        if (nvs_get_i32(h, "tz_min", &old_tz) != ESP_OK || old_tz != tz_min) {
            nvs_set_i32(h, "tz_min", (int32_t)tz_min);
            nvs_commit(h);
        }
        nvs_close(h);
    }
}

static void maybe_save_nav_cache_to_nvs(double lat, double lon, float alt_m, float geoid_m)
{
    TickType_t now = xTaskGetTickCount();
    bool should_save = false;
    if (!s_nav_cache_saved_this_boot) {
        should_save = true;
    } else if ((now - s_last_nav_save_tick) >= pdMS_TO_TICKS(300000)) {
        /* Save at most once every 5 minutes if moved > ~500m or altitude changed > 30m */
        if (fabs(lat - s_cached_lat) > 0.005 || fabs(lon - s_cached_lon) > 0.005 ||
            fabsf(alt_m - s_cached_alt_m) > 30.0f) {
            should_save = true;
        }
    }
    if (!should_save) {
        return;
    }

    s_nav_cache_saved_this_boot = true;
    s_last_nav_save_tick = now;
    s_cached_lat = lat;
    s_cached_lon = lon;
    s_cached_alt_m = alt_m;
    s_cached_geoid_m = geoid_m;
    s_has_nav_cache = true;

    nvs_handle_t h;
    if (nvs_open("gps_cfg", NVS_READWRITE, &h) == ESP_OK) {
        int32_t new_lat_e7 = (int32_t)llround(lat * 1e7);
        int32_t new_lon_e7 = (int32_t)llround(lon * 1e7);
        int32_t new_alt_dm = (int32_t)lroundf(alt_m * 10.0f);
        int32_t new_geoid_dm = (int32_t)lroundf(geoid_m * 10.0f);

        int32_t old_lat = 0, old_lon = 0, old_alt = 0;
        bool diff = (nvs_get_i32(h, "lat_e7", &old_lat) != ESP_OK) ||
                    (nvs_get_i32(h, "lon_e7", &old_lon) != ESP_OK) ||
                    (nvs_get_i32(h, "alt_dm", &old_alt) != ESP_OK) ||
                    (abs(new_lat_e7 - old_lat) > 5000) ||
                    (abs(new_lon_e7 - old_lon) > 5000) ||
                    (abs(new_alt_dm - old_alt) > 50);
        if (diff) {
            nvs_set_i32(h, "lat_e7", new_lat_e7);
            nvs_set_i32(h, "lon_e7", new_lon_e7);
            nvs_set_i32(h, "alt_dm", new_alt_dm);
            nvs_set_i32(h, "geoid_dm", new_geoid_dm);
            nvs_commit(h);
            ESP_LOGI(TAG, "Saved Last-Known Fix to NVS: lat=%.5f, lon=%.5f, alt=%.1fm, geoid=%.1fm",
                     lat, lon, alt_m, geoid_m);
        }
        nvs_close(h);
    }
}

/* Automatically determine timezone offset (in minutes from UTC) from GPS lat/lon */
static int determine_tz_offset_minutes(double lat, double lon)
{
    /* China unified timezone: UTC+8 across mainland, HK, Macao, Taiwan */
    if (lat >= 18.0 && lat <= 54.0 && lon >= 73.0 && lon <= 135.0) {
        return 8 * 60;
    }
    /* Japan & Korea: UTC+9 */
    if (lat >= 24.0 && lat <= 46.0 && lon > 124.0 && lon <= 146.0) {
        return 9 * 60;
    }
    /* India: UTC+5:30 */
    if (lat >= 6.0 && lat < 36.0 && lon >= 68.0 && lon < 97.0) {
        return 5 * 60 + 30;
    }
    /* Standard astronomical / nautical timezone band (15 deg per hour) */
    int tz_hours = (int)lround(lon / 15.0);
    if (tz_hours < -12) tz_hours = -12;
    if (tz_hours > 14) tz_hours = 14;
    return tz_hours * 60;
}

static int days_in_month(int year, int month)
{
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 30;
    }
    if (month == 2) {
        bool leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return kDays[month - 1];
}

static void recompute_local_datetime_locked(void)
{
    int total_min = s_gps_state.utc_hour * 60 + s_gps_state.utc_minute + s_tz_offset_minutes;
    int day_shift = 0;
    while (total_min < 0) {
        total_min += 24 * 60;
        day_shift--;
    }
    while (total_min >= 24 * 60) {
        total_min -= 24 * 60;
        day_shift++;
    }

    s_gps_state.local_hour = total_min / 60;
    s_gps_state.local_minute = total_min % 60;
    s_gps_state.local_second = s_gps_state.utc_second;

    if (s_gps_state.date_valid) {
        int y = s_gps_state.utc_year;
        int m = s_gps_state.utc_month;
        int d = s_gps_state.utc_day + day_shift;
        if (d > days_in_month(y, m)) {
            d = 1;
            m++;
            if (m > 12) {
                m = 1;
                y++;
            }
        } else if (d < 1) {
            m--;
            if (m < 1) {
                m = 12;
                y--;
            }
            d = days_in_month(y, m);
        }
        s_gps_state.local_year = y;
        s_gps_state.local_month = m;
        s_gps_state.local_day = d;
    }
}

static void advance_utc_date_by_one_day_locked(void)
{
    if (!s_gps_state.date_valid) {
        return;
    }
    int y = s_gps_state.utc_year;
    int m = s_gps_state.utc_month;
    int d = s_gps_state.utc_day + 1;
    if (d > days_in_month(y, m)) {
        d = 1;
        m++;
        if (m > 12) {
            m = 1;
            y++;
        }
    }
    s_gps_state.utc_year = y;
    s_gps_state.utc_month = m;
    s_gps_state.utc_day = d;
}

static void update_utc_time_locked(const char *time_str, bool time_is_trusted)
{
    if (!time_str || strlen(time_str) < 6) {
        return;
    }
    int hh = (time_str[0] - '0') * 10 + (time_str[1] - '0');
    int mm = (time_str[2] - '0') * 10 + (time_str[3] - '0');
    int ss = (time_str[4] - '0') * 10 + (time_str[5] - '0');
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) {
        return;
    }

    if (!time_is_trusted && !s_gps_state.has_session_fix) {
        return;
    }

    /* Prevent 1-day backward jump at 00:00:00 UTC when GGA arrives 50ms before RMC */
    if (s_gps_state.time_valid && s_gps_state.date_valid &&
        s_gps_state.utc_hour == 23 && hh == 0) {
        advance_utc_date_by_one_day_locked();
    }

    s_gps_state.time_valid = true;
    s_gps_state.utc_hour = hh;
    s_gps_state.utc_minute = mm;
    s_gps_state.utc_second = ss;
    s_gps_state.tz_offset_minutes = s_tz_offset_minutes;

    recompute_local_datetime_locked();
}

/* Fallback to ESP32 system RTC (synced via indoor Wi-Fi SNTP) when GNSS date/time is not yet locked */
static void sync_time_from_system_rtc_locked(void)
{
    if (s_rmc_date_valid && s_gps_state.has_fix) {
        return;
    }
    time_t now_sec = 0;
    time(&now_sec);
    struct tm utc_tm = {0};
    gmtime_r(&now_sec, &utc_tm);
    int sys_year = utc_tm.tm_year + 1900;
    if (sys_year < 2025) {
        return;
    }
    if (!s_rmc_date_valid) {
        s_gps_state.date_valid = true;
        s_gps_state.utc_year = sys_year;
        s_gps_state.utc_month = utc_tm.tm_mon + 1;
        s_gps_state.utc_day = utc_tm.tm_mday;
    }
    if (!s_gps_state.has_fix) {
        s_gps_state.time_valid = true;
        s_gps_state.utc_hour = utc_tm.tm_hour;
        s_gps_state.utc_minute = utc_tm.tm_min;
        s_gps_state.utc_second = utc_tm.tm_sec;
    }
    s_gps_state.tz_offset_minutes = s_tz_offset_minutes;
    recompute_local_datetime_locked();
}

static double parse_nmea_coord(const char *val_str, const char *hem_str)
{
    if (!val_str || val_str[0] == '\0') {
        return 0.0;
    }
    double raw = atof(val_str);
    int deg = (int)(raw / 100.0);
    double min = raw - (deg * 100.0);
    double dec = (double)deg + (min / 60.0);
    if (hem_str && (hem_str[0] == 'S' || hem_str[0] == 'W')) {
        dec = -dec;
    }
    return dec;
}

/* Split NMEA sentence into comma-separated fields (in-place) */
static int split_nmea_fields(char *sentence, char **fields, int max_fields)
{
    int count = 0;
    char *star = strchr(sentence, '*');
    if (star) {
        *star = '\0';
    }
    char *p = sentence;
    while (p && count < max_fields) {
        fields[count++] = p;
        char *comma = strchr(p, ',');
        if (!comma) {
            break;
        }
        *comma = '\0';
        p = comma + 1;
    }
    return count;
}

static bool verify_nmea_checksum(const char *line)
{
    if (line[0] != '$') {
        return false;
    }
    const char *star = strchr(line, '*');
    if (!star || strlen(star) < 3) {
        return false;
    }
    uint8_t calc = 0;
    for (const char *p = line + 1; p < star; ++p) {
        calc ^= (uint8_t)(*p);
    }
    uint8_t expected = (uint8_t)strtol(star + 1, NULL, 16);
    return calc == expected;
}

static void update_signal_metrics_locked(void)
{
    int pct = 0;
    if (s_gps_state.has_fix) {
        /* When fixed: map based on satellites used and SNR */
        /* Base by sats used (4..12 -> 40%..100%) combined with SNR */
        int snr_pct = 0;
        if (s_gps_state.avg_snr_db > 0) {
            int snr = s_gps_state.avg_snr_db;
            snr_pct = (snr <= 15) ? 20 : (20 + ((snr - 15) * 80) / 25);
            if (snr_pct > 100) snr_pct = 100;
        }
        int sat_pct = (s_gps_state.satellites_used >= 4) ?
                      (35 + ((s_gps_state.satellites_used - 4) * 65) / 8) : 30;
        if (sat_pct > 100) sat_pct = 100;

        pct = (snr_pct * 6 + sat_pct * 4) / 10;
        if (pct < 25) pct = 25;
    } else {
        /* When NOT fixed (searching / fix=0):
         * Keep arc at low level (max 20%) to clearly distinguish 'searching' from 'solid signal' */
        if (s_gps_state.satellites_in_view > 0 || s_gps_state.avg_snr_db > 0) {
            int view_contrib = (s_gps_state.satellites_in_view > 0) ? (s_gps_state.satellites_in_view * 3) : 5;
            if (view_contrib > 15) view_contrib = 15;
            pct = 6 + view_contrib; // around 10% ~ 20%
        } else {
            pct = 0;
        }
    }
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_gps_state.signal_percent = pct;
}

static void commit_gsv_epoch_locked(bool reset_after)
{
    if (reset_after) {
        if (s_gsv_epoch_alm_view > 0 || s_snr_count == 0) {
            s_gsv_last_alm_view = s_gsv_epoch_alm_view;
        }
        s_gsv_epoch_alm_view = 0;
        s_gsv_epoch_closed = true;
    }

    if (s_snr_count <= 0) {
        if (reset_after) {
            s_empty_gsv_epochs++;
            if (s_empty_gsv_epochs >= 2) {
                s_gps_state.satellites_in_view = 0;
                s_gps_state.avg_snr_db = 0;
            }
        }
        update_signal_metrics_locked();
        return;
    }

    s_empty_gsv_epochs = 0;

    /* Avoid overwriting a higher satellite count mid-burst before all GSV sentences arrive */
    if (!reset_after && s_snr_count < s_gps_state.satellites_in_view) {
        return;
    }

    /* Sort descending to average top 4 strongest satellites */
    int sorted[MAX_TRACKED_SNRS];
    for (int i = 0; i < s_snr_count; ++i) {
        sorted[i] = s_snr_list[i];
    }
    for (int i = 0; i < s_snr_count - 1; ++i) {
        for (int j = i + 1; j < s_snr_count; ++j) {
            if (sorted[j] > sorted[i]) {
                int tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }
    int top_n = (s_snr_count < 4) ? s_snr_count : 4;
    int sum = 0;
    for (int i = 0; i < top_n; ++i) {
        sum += sorted[i];
    }
    s_gps_state.satellites_in_view = s_snr_count;
    s_gps_state.avg_snr_db = sum / top_n;
    update_signal_metrics_locked();
    if (reset_after) {
        s_snr_count = 0;
    }
}

/* High-resolution EGM2008/EGM96 Geoid Separation (Undulation N) grid for China & East Asia:
 * Latitude: 15°N to 55°N (step 5°, 9 rows)
 * Longitude: 70°E to 135°E (step 5°, 14 cols)
 * Units: 0.1 meters (decimeters), where Orthometric MSL Altitude H = h_WGS84 - N */
#define GEOID_LAT_MIN   15.0
#define GEOID_LAT_MAX   55.0
#define GEOID_LAT_STEP  5.0
#define GEOID_LAT_ROWS  9

#define GEOID_LON_MIN   70.0
#define GEOID_LON_MAX   135.0
#define GEOID_LON_STEP  5.0
#define GEOID_LON_COLS  14

static const int16_t kEgmGeoidGridDm[GEOID_LAT_ROWS][GEOID_LON_COLS] = {
    /* 15N: 70E..135E */ { -720, -785, -880, -660, -530, -410, -305, -155,  -15,  140,  325,  480,  505,  490 },
    /* 20N: 70E..135E */ { -640, -685, -660, -575, -535, -430, -310, -225, -118,   15,  165,  340,  415,  410 },
    /* 25N: 70E..135E */ { -510, -545, -580, -605, -530, -425, -345, -272, -145,  -28,  125,  268,  335,  345 },
    /* 30N: 70E..135E */ { -395, -410, -450, -520, -602, -475, -365, -308, -182,  -32,   78,  215,  295,  340 },
    /* 35N: 70E..135E */ { -270, -305, -490, -565, -540, -445, -380, -295, -205,  -75,   42,  172,  280,  355 },
    /* 40N: 70E..135E */ { -285, -360, -575, -580, -515, -450, -415, -310, -212, -115,   12,  135,  245,  310 },
    /* 45N: 70E..135E */ { -330, -415, -450, -505, -480, -430, -385, -290, -195,  -85,   25,  128,  205,  255 },
    /* 50N: 70E..135E */ { -280, -355, -410, -415, -395, -375, -350, -295, -200,  -95,   10,  105,  168,  215 },
    /* 55N: 70E..135E */ { -220, -290, -350, -365, -350, -340, -325, -275, -190, -100,  -10,   75,  135,  180 },
};

static float calc_egm_geoid_separation_m(double lat, double lon, bool *out_in_grid)
{
    bool in_grid = (lat >= GEOID_LAT_MIN && lat <= GEOID_LAT_MAX &&
                    lon >= GEOID_LON_MIN && lon <= GEOID_LON_MAX);
    if (out_in_grid) {
        *out_in_grid = in_grid;
    }

    double clat = (lat < GEOID_LAT_MIN) ? GEOID_LAT_MIN : ((lat > GEOID_LAT_MAX) ? GEOID_LAT_MAX : lat);
    double clon = (lon < GEOID_LON_MIN) ? GEOID_LON_MIN : ((lon > GEOID_LON_MAX) ? GEOID_LON_MAX : lon);

    double r_f = (clat - GEOID_LAT_MIN) / GEOID_LAT_STEP;
    double c_f = (clon - GEOID_LON_MIN) / GEOID_LON_STEP;

    int r0 = (int)floor(r_f);
    int c0 = (int)floor(c_f);
    if (r0 < 0) r0 = 0;
    if (r0 >= GEOID_LAT_ROWS - 1) r0 = GEOID_LAT_ROWS - 2;
    if (c0 < 0) c0 = 0;
    if (c0 >= GEOID_LON_COLS - 1) c0 = GEOID_LON_COLS - 2;

    float u = (float)(r_f - (double)r0);
    float v = (float)(c_f - (double)c0);
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;

    float n00 = (float)kEgmGeoidGridDm[r0][c0] * 0.1f;
    float n01 = (float)kEgmGeoidGridDm[r0][c0 + 1] * 0.1f;
    float n10 = (float)kEgmGeoidGridDm[r0 + 1][c0] * 0.1f;
    float n11 = (float)kEgmGeoidGridDm[r0 + 1][c0 + 1] * 0.1f;

    float n0 = n00 + v * (n01 - n00);
    float n1 = n10 + v * (n11 - n10);
    return n0 + u * (n1 - n0);
}

/* Combined 1D Kalman Filter + 7-point Sliding Window Moving Average for GNSS Altitude */
static float filter_altitude_kalman_sliding_locked(float z_meas_m, float eff_vdop, int sats, bool high_confidence)
{
    if (!s_kf_initialized) {
        s_kf_initialized = true;
        s_kf_x = z_meas_m;
        s_kf_p = high_confidence ? 9.0f : 49.0f;
        s_alt_win[0] = z_meas_m;
        s_alt_win_head = 0;
        s_alt_win_count = 1;
        return z_meas_m;
    }

    /* 1. 1D Kalman Filter Prediction & Update:
     *    - Process variance Q adapts to horizontal speed and vertical innovation
     *    - Measurement variance R scales quadratically with VDOP and inversely with satellite count */
    float innov = z_meas_m - s_kf_x;
    float abs_innov = fabsf(innov);
    float spd = s_gps_state.speed_kmh;

    float q_var = 0.03f; /* Stationary: strong vertical damping against GNSS wander */
    if (spd >= 8.0f || abs_innov >= 4.5f) {
        q_var = 1.20f;   /* Fast travel / rapid climb or descent */
    } else if (spd >= 1.5f || abs_innov >= 1.8f) {
        q_var = 0.25f;   /* Walking / moderate elevation change */
    }

    float sat_clamped = (sats < 4) ? 4.0f : ((sats > 16) ? 16.0f : (float)sats);
    float vdop_clamped = (eff_vdop < 0.8f) ? 0.8f : ((eff_vdop > 6.0f) ? 6.0f : eff_vdop);
    float sigma_v = 1.5f * vdop_clamped * sqrtf(8.0f / sat_clamped);
    float r_var = sigma_v * sigma_v;
    if (!high_confidence) {
        r_var *= 4.0f;
    }
    if (r_var < 0.8f) r_var = 0.8f;
    if (r_var > 80.0f) r_var = 80.0f;

    s_kf_p += q_var;
    float k_gain = s_kf_p / (s_kf_p + r_var);
    s_kf_x += k_gain * innov;
    s_kf_p = (1.0f - k_gain) * s_kf_p;

    /* 2. Sliding Window Moving Average (up to ALT_SLIDE_WIN_SIZE = 7 samples):
     *    - On rapid genuine climb/descent (>3.5m step while moving), flush stale window tail to prevent lag
     *    - When stationary (>=5 samples), apply Trimmed Moving Average (drop min & max, average middle)
     *    - When moving, apply Linearly Weighted Moving Average favoring newer samples */
    if (spd >= 3.0f && fabsf(s_kf_x - s_gps_state.smoothed_altitude_m) > 3.5f && s_alt_win_count > 2) {
        int newest_idx = (s_alt_win_head + s_alt_win_count - 1) % ALT_SLIDE_WIN_SIZE;
        s_alt_win_head = newest_idx;
        s_alt_win_count = 1;
    }

    if (s_alt_win_count >= ALT_SLIDE_WIN_SIZE) {
        s_alt_win_head = (s_alt_win_head + 1) % ALT_SLIDE_WIN_SIZE;
        s_alt_win_count--;
    }
    int tail = (s_alt_win_head + s_alt_win_count) % ALT_SLIDE_WIN_SIZE;
    s_alt_win[tail] = s_kf_x;
    s_alt_win_count++;

    if (spd < 1.5f && s_alt_win_count >= 5) {
        /* Trimmed Moving Average: discard single min and single max in window */
        float sum = 0.0f;
        float vmin = s_alt_win[s_alt_win_head];
        float vmax = vmin;
        for (int i = 0; i < s_alt_win_count; ++i) {
            float v = s_alt_win[(s_alt_win_head + i) % ALT_SLIDE_WIN_SIZE];
            sum += v;
            if (v < vmin) vmin = v;
            if (v > vmax) vmax = v;
        }
        return (sum - vmin - vmax) / (float)(s_alt_win_count - 2);
    } else {
        /* Linearly Weighted Moving Average: weights 1..N from oldest to newest */
        float w_sum = 0.0f;
        float w_tot = 0.0f;
        for (int i = 0; i < s_alt_win_count; ++i) {
            float w = (float)(i + 1);
            float v = s_alt_win[(s_alt_win_head + i) % ALT_SLIDE_WIN_SIZE];
            w_sum += w * v;
            w_tot += w;
        }
        return (w_tot > 0.0f) ? (w_sum / w_tot) : s_kf_x;
    }
}

static void parse_nmea_line(char *line)
{
    if (!verify_nmea_checksum(line)) {
        return;
    }

    if (strncmp(line + 3, "GGA,", 4) == 0) {
        snprintf(s_last_gga, sizeof(s_last_gga), "%s", line);
    } else if (strncmp(line + 3, "RMC,", 4) == 0) {
        snprintf(s_last_rmc, sizeof(s_last_rmc), "%s", line);
    }

    char *fields[24] = {0};
    int nf = split_nmea_fields(line, fields, 24);
    if (nf < 1) {
        return;
    }

    const char *talker_msg = fields[0];
    if (strlen(talker_msg) < 6) {
        return;
    }
    const char *msg_type = talker_msg + 3;

    if (strcmp(msg_type, "GGA") == 0 && nf >= 10) {
        int fix = atoi(fields[6]);
        int sats = atoi(fields[7]);
        float hdop = (fields[8][0] != '\0') ? strtof(fields[8], NULL) : 99.9f;
        bool alt_present = (fields[9][0] != '\0');
        float gga_msl_m = alt_present ? strtof(fields[9], NULL) : 0.0f;
        bool has_gga_geoid = (nf >= 12 && fields[11][0] != '\0');
        float gga_geoid_m = has_gga_geoid ? strtof(fields[11], NULL) : 0.0f;

        xSemaphoreTake(s_gps_mutex, portMAX_DELAY);
        /* If RMC didn't close the previous GSV epoch (e.g. cold boot), commit & reset here */
        if (!s_gsv_epoch_closed) {
            commit_gsv_epoch_locked(true);
        }

        s_gps_state.fix_quality = fix;
        s_gps_state.has_fix = (fix >= 1 && alt_present);
        s_gps_state.satellites_used = sats;
        s_gps_state.hdop = hdop;

        if (s_gps_state.has_fix) {
            /* If emerging from an indoor/obstructed state (>=5 epochs without fix),
             * trigger fast outdoor convergence so Kalman & sliding window snap immediately */
            if (s_indoor_epochs >= 5) {
                s_outdoor_fast_converge = true;
                s_outdoor_fast_poll_until = xTaskGetTickCount() + pdMS_TO_TICKS(20000);
                ESP_LOGI(TAG, "Indoor->Outdoor fix transition detected (after %lu indoor epochs): fast convergence active",
                         (unsigned long)s_indoor_epochs);
            }
            s_indoor_epochs = 0;

            double lat = parse_nmea_coord(fields[2], fields[3]);
            double lon = parse_nmea_coord(fields[4], fields[5]);
            s_gps_state.latitude = lat;
            s_gps_state.longitude = lon;

            int new_tz = determine_tz_offset_minutes(lat, lon);
            if (new_tz != s_tz_offset_minutes) {
                s_tz_offset_minutes = new_tz;
                save_tz_to_nvs(new_tz);
            }

            /* Step 1: Geoid Separation Compensation (大地水准面差距补偿)
             * Recover true WGS-84 ellipsoidal height (h_WGS84 = H_GGA + N_GGA) and subtract
             * bilinear-interpolated EGM2008/EGM96 geoid undulation N(lat, lon). */
            float ellipsoid_alt_m = gga_msl_m + gga_geoid_m;
            bool in_egm_grid = false;
            float egm_geoid_m = calc_egm_geoid_separation_m(lat, lon, &in_egm_grid);
            float applied_geoid_m = (in_egm_grid || !has_gga_geoid) ? egm_geoid_m : gga_geoid_m;
            float comp_alt_m = ellipsoid_alt_m - applied_geoid_m;

            s_gps_state.ellipsoid_alt_m = ellipsoid_alt_m;
            s_gps_state.geoid_sep_m = applied_geoid_m;
            s_gps_state.raw_altitude_m = comp_alt_m;

            /* Step 2: Filter out High-VDOP / 2D-Fix / Multipath-Spike Poor Quality Points (滤除高VDOP劣质点) */
            float eff_vdop = (s_gps_state.vdop > 0.05f && s_gps_state.vdop < 90.0f)
                                 ? s_gps_state.vdop
                                 : (hdop * 1.5f);
            bool is_2d_only = (s_gps_state.fix_mode == 2);
            bool dop_ok = (!is_2d_only) &&
                          (sats >= MIN_SATS_FOR_3D_ALT) &&
                          (eff_vdop <= VDOP_MAX_VALID) &&
                          (hdop <= HDOP_MAX_VALID);
            bool is_spike = (s_kf_initialized &&
                             !s_outdoor_fast_converge &&
                             fabsf(comp_alt_m - s_kf_x) > 18.0f &&
                             eff_vdop > 1.8f &&
                             s_alt_reject_streak < 4);
            bool alt_quality_ok = dop_ok && !is_spike;
            s_gps_state.alt_quality_ok = alt_quality_ok;

            /* Step 3: 1D Kalman Filter + Sliding Window Moving Average (一阶卡尔曼滤波 + 高度滑动平均) */
            if (alt_quality_ok) {
                if (s_outdoor_fast_converge) {
                    /* Flush any indoor residual state so the first valid outdoor 3D fix converges immediately */
                    s_outdoor_fast_converge = false;
                    s_kf_p = 49.0f;
                    s_alt_win_head = 0;
                    s_alt_win_count = 0;
                }
                s_alt_reject_streak = 0;
                s_good_alt_streak++;
                s_gps_state.smoothed_altitude_m =
                    filter_altitude_kalman_sliding_locked(comp_alt_m, eff_vdop, sats, true);
                s_gps_state.altitude_int_m = (int32_t)lroundf(s_gps_state.smoothed_altitude_m);

                /* Lock session Min/Max altitude only after >=2 consecutive low-VDOP 3D epochs
                 * so initial cold-start high-VDOP spikes never corrupt session Min/Max */
                if (s_good_alt_streak >= 2) {
                    if (!s_gps_state.has_session_fix) {
                        s_gps_state.has_session_fix = true;
                        s_gps_state.min_altitude_int_m = s_gps_state.altitude_int_m;
                        s_gps_state.max_altitude_int_m = s_gps_state.altitude_int_m;
                        s_gps_state.max_speed_int_kmh = s_gps_state.speed_int_kmh;
                    } else {
                        if (s_gps_state.altitude_int_m < s_gps_state.min_altitude_int_m) {
                            s_gps_state.min_altitude_int_m = s_gps_state.altitude_int_m;
                        }
                        if (s_gps_state.altitude_int_m > s_gps_state.max_altitude_int_m) {
                            s_gps_state.max_altitude_int_m = s_gps_state.altitude_int_m;
                        }
                        if (s_gps_state.speed_int_kmh > s_gps_state.max_speed_int_kmh) {
                            s_gps_state.max_speed_int_kmh = s_gps_state.speed_int_kmh;
                        }
                    }
                }
                if (s_good_alt_streak >= 3) {
                    maybe_save_nav_cache_to_nvs(lat, lon, s_gps_state.smoothed_altitude_m, applied_geoid_m);
                }
            } else {
                s_good_alt_streak = 0;
                s_alt_reject_streak++;
                s_alt_reject_total++;
                /* If never initialized yet since boot and fix has >=4 sats with moderate VDOP,
                 * seed preliminary altitude with high uncertainty R without locking session Min/Max */
                if (!s_kf_initialized && sats >= 4 && eff_vdop <= 6.0f) {
                    s_gps_state.smoothed_altitude_m =
                        filter_altitude_kalman_sliding_locked(comp_alt_m, eff_vdop, sats, false);
                    s_gps_state.altitude_int_m = (int32_t)lroundf(s_gps_state.smoothed_altitude_m);
                }
            }

            if (s_rmc_last_seq < s_gga_last_seq) {
                s_rmc_last_seq = s_gga_last_seq;
                s_gps_state.fix_epoch_seq++;
            }
            s_gga_last_seq++;
        } else {
            s_gps_state.alt_quality_ok = false;
            s_good_alt_streak = 0;
            s_gps_state.speed_kmh = 0.0f;
            s_gps_state.speed_int_kmh = 0;
            if (s_gps_state.satellites_in_view <= 2 || s_gps_state.avg_snr_db < 18) {
                s_indoor_epochs++;
            } else if (s_indoor_epochs >= 5 &&
                       (s_gps_state.satellites_in_view >= 4 || s_gps_state.avg_snr_db >= 20)) {
                /* Emerging from indoors into open sky: boost polling rate during acquisition */
                s_outdoor_fast_converge = true;
                s_outdoor_fast_poll_until = xTaskGetTickCount() + pdMS_TO_TICKS(20000);
                s_indoor_epochs = 0;
            }
        }
        /* GGA time is trusted when fixed, when RMC date is valid, or when tracking satellites outside 1980 cold-boot epoch */
        bool trusted = s_gps_state.has_fix || s_rmc_date_valid || (s_gps_state.satellites_in_view >= 4);
        update_utc_time_locked(fields[1], trusted);
        sync_time_from_system_rtc_locked();
        update_signal_metrics_locked();
        xSemaphoreGive(s_gps_mutex);
    } else if (strcmp(msg_type, "GSA") == 0 && nf >= 18) {
        /* $xxGSA: field 2 = fix_mode (1=no, 2=2D, 3=3D), field 15 = PDOP, field 16 = HDOP, field 17 = VDOP */
        int fix_mode = (fields[2][0] != '\0') ? atoi(fields[2]) : 1;
        float pdop = (fields[15][0] != '\0') ? strtof(fields[15], NULL) : 99.9f;
        float hdop = (fields[16][0] != '\0') ? strtof(fields[16], NULL) : 99.9f;
        float vdop = (fields[17][0] != '\0') ? strtof(fields[17], NULL) : 99.9f;

        xSemaphoreTake(s_gps_mutex, portMAX_DELAY);
        if (fix_mode >= 1 && fix_mode <= 3) {
            s_gps_state.fix_mode = fix_mode;
        }
        if (pdop > 0.0f) {
            s_gps_state.pdop = pdop;
        }
        if (hdop > 0.0f) {
            s_gps_state.hdop = hdop;
        }
        if (vdop > 0.0f) {
            s_gps_state.vdop = vdop;
        }
        xSemaphoreGive(s_gps_mutex);
    } else if (strcmp(msg_type, "RMC") == 0 && nf >= 10) {
        bool valid = (fields[2][0] == 'A');
        bool date_ok = false;
        int rmc_day = 0, rmc_month = 0, rmc_year = 0;
        if (strlen(fields[9]) == 6) {
            rmc_day = (fields[9][0] - '0') * 10 + (fields[9][1] - '0');
            rmc_month = (fields[9][2] - '0') * 10 + (fields[9][3] - '0');
            int yy = (fields[9][4] - '0') * 10 + (fields[9][5] - '0');
            if (yy >= 24 && yy < 80 && rmc_month >= 1 && rmc_month <= 12 && rmc_day >= 1 && rmc_day <= 31) {
                rmc_year = 2000 + yy;
                date_ok = true;
            }
        }
        xSemaphoreTake(s_gps_mutex, portMAX_DELAY);
        /* RMC arrives at the end of the 1Hz burst after all GSV sentences: commit GSV epoch immediately */
        if (!s_gsv_epoch_closed) {
            commit_gsv_epoch_locked(true);
        }
        if (date_ok) {
            s_rmc_date_valid = true;
            s_gps_state.date_valid = true;
            s_gps_state.utc_year = rmc_year;
            s_gps_state.utc_month = rmc_month;
            s_gps_state.utc_day = rmc_day;
            recompute_local_datetime_locked();
        }
        update_utc_time_locked(fields[1], valid || s_rmc_date_valid);
        if (valid) {
            float knots = (fields[7][0] != '\0') ? strtof(fields[7], NULL) : 0.0f;
            float course = (fields[8][0] != '\0') ? strtof(fields[8], NULL) : 0.0f;
            float raw_kmh = knots * 1.852f;
            /* Suppress static GNSS zero-speed drift below 0.8 km/h */
            if (raw_kmh < 0.8f) {
                raw_kmh = 0.0f;
            }
            s_gps_state.speed_kmh = raw_kmh;
            s_gps_state.speed_int_kmh = (int32_t)lroundf(raw_kmh);
            s_gps_state.course_deg = course;
            if ((s_gps_state.has_fix || s_gps_state.has_session_fix) &&
                s_gps_state.speed_int_kmh > s_gps_state.max_speed_int_kmh) {
                s_gps_state.max_speed_int_kmh = s_gps_state.speed_int_kmh;
            }
            /* Synchronize 1Hz epoch completion once both GGA (altitude) and RMC (speed) are updated */
            if (s_gps_state.has_fix && s_rmc_last_seq < s_gga_last_seq) {
                s_rmc_last_seq = s_gga_last_seq;
                s_gps_state.fix_epoch_seq++;
            }
        } else {
            s_gps_state.speed_kmh = 0.0f;
            s_gps_state.speed_int_kmh = 0;
        }
        xSemaphoreGive(s_gps_mutex);
    } else if (strcmp(msg_type, "VTG") == 0 && nf >= 9) {
        /* Direct km/h ground speed from $GNVTG field 7 */
        if (fields[7][0] != '\0') {
            float raw_kmh = strtof(fields[7], NULL);
            if (raw_kmh < 0.8f) {
                raw_kmh = 0.0f;
            }
            xSemaphoreTake(s_gps_mutex, portMAX_DELAY);
            if (s_gps_state.has_fix) {
                s_gps_state.speed_kmh = raw_kmh;
                s_gps_state.speed_int_kmh = (int32_t)lroundf(raw_kmh);
                if (s_gps_state.speed_int_kmh > s_gps_state.max_speed_int_kmh) {
                    s_gps_state.max_speed_int_kmh = s_gps_state.speed_int_kmh;
                }
                if (s_rmc_last_seq < s_gga_last_seq) {
                    s_rmc_last_seq = s_gga_last_seq;
                    s_gps_state.fix_epoch_seq++;
                }
            }
            xSemaphoreGive(s_gps_mutex);
        }
    } else if (strcmp(msg_type, "GSV") == 0 && nf >= 8) {
        /* Fields: 0=$xxGSV, 1=total_msgs, 2=msg_num, 3=sats_in_view, then up to 4 blocks of 4: prn, el, az, snr, plus optional NMEA 4.10 signalId */
        s_gsv_epoch_closed = false;
        uint16_t talker_tag = ((uint16_t)(uint8_t)talker_msg[1] << 8) ^ ((uint16_t)(uint8_t)talker_msg[2] << 5);
        int msg_num = atoi(fields[2]);
        int sv_in_view = atoi(fields[3]);
        if (msg_num == 1 && sv_in_view > 0) {
            s_gsv_epoch_alm_view += sv_in_view;
        }
        int max_idx = ((nf - 4) % 4 == 1) ? (nf - 1) : nf;
        if (max_idx > 20) {
            max_idx = 20;
        }
        bool added = false;
        for (int idx = 7; idx < max_idx; idx += 4) {
            if (fields[idx][0] != '\0') {
                int prn = atoi(fields[idx - 3]);
                int snr = atoi(fields[idx]);
                if (snr > 0) {
                    uint16_t sat_uid = talker_tag | (uint16_t)(prn & 0x1FF);
                    bool dup = false;
                    if (prn > 0) {
                        for (int k = 0; k < s_snr_count; ++k) {
                            if (s_sat_id_list[k] == sat_uid) {
                                /* Dual-band L1+L5 report for same satellite: keep stronger SNR */
                                if (snr > s_snr_list[k]) {
                                    s_snr_list[k] = snr;
                                    added = true;
                                }
                                dup = true;
                                break;
                            }
                        }
                    }
                    if (!dup && s_snr_count < MAX_TRACKED_SNRS) {
                        s_sat_id_list[s_snr_count] = sat_uid;
                        s_snr_list[s_snr_count] = snr;
                        s_snr_count++;
                        added = true;
                    }
                }
            }
        }
        if (added) {
            xSemaphoreTake(s_gps_mutex, portMAX_DELAY);
            commit_gsv_epoch_locked(false);
            xSemaphoreGive(s_gps_mutex);
        }
    }
}

static esp_err_t lc76g_poll_nmea(char *rx_buf, size_t rx_buf_size, size_t *out_read_len, uint32_t *out_fifo_len)
{
    *out_read_len = 0;
    *out_fifo_len = 0;

    /* Step 1: Query available NMEA byte count in LC76G I2C slave TX buffer */
    const uint8_t cmd_query_len[8] = {0x08, 0x00, 0x51, 0xAA, 0x04, 0x00, 0x00, 0x00};
    esp_err_t err = i2c_master_transmit(s_write_dev, cmd_query_len, sizeof(cmd_query_len), 100);
    if (err != ESP_OK) {
        return err;
    }

    /* Official Quectel / Waveshare sample code uses 50ms between I2C protocol phases,
     * which also yields the shared I2C bus to CST9217 touch controller in lvgl_task. */
    vTaskDelay(pdMS_TO_TICKS(50));

    uint32_t data_len = 0;
    err = i2c_master_receive(s_read_dev, (uint8_t *)&data_len, sizeof(data_len), 100);
    if (err != ESP_OK) {
        return err;
    }

    *out_fifo_len = data_len;
    if (data_len == 0 || data_len > LC76G_MAX_READ_LEN) {
        return ESP_OK;
    }
    /* Match official Quectel / Waveshare sample code: clamp single-read chunk to 2048 bytes */
    if (data_len > 2048) {
        data_len = 2048;
    }
    if (data_len >= rx_buf_size) {
        data_len = rx_buf_size - 1;
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    /* Step 2: Request payload read of exact data_len bytes reported by Step 1 */
    uint8_t cmd_read_payload[8] = {
        0x00, 0x20, 0x51, 0xAA,
        (uint8_t)(data_len & 0xFF),
        (uint8_t)((data_len >> 8) & 0xFF),
        (uint8_t)((data_len >> 16) & 0xFF),
        (uint8_t)((data_len >> 24) & 0xFF),
    };
    err = i2c_master_transmit(s_write_dev, cmd_read_payload, sizeof(cmd_read_payload), 100);
    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    /* 2048 bytes at 100 kHz I2C takes ~185 ms; use 500 ms timeout */
    err = i2c_master_receive(s_read_dev, (uint8_t *)rx_buf, data_len, 500);
    if (err != ESP_OK) {
        return err;
    }

    rx_buf[data_len] = '\0';
    *out_read_len = data_len;
    return ESP_OK;
}

static void gps_lc76g_task(void *arg)
{
    ESP_LOGI(TAG, "LC76G GNSS polling task running on Core %d", xPortGetCoreID());

    char *rx_chunk = malloc(LC76G_MAX_READ_LEN + 1);
    if (!rx_chunk) {
        vTaskDelete(NULL);
        return;
    }

    char line_buf[256];
    size_t line_pos = 0;
    TickType_t last_diag_tick = xTaskGetTickCount();
    TickType_t last_rx_tick = xTaskGetTickCount();
    uint32_t total_rx_bytes = 0;
    const TickType_t task_start_tick = xTaskGetTickCount();
    bool startup_checked = false;

    while (1) {
        size_t read_len = 0;
        uint32_t fifo_len = 0;
        esp_err_t err = lc76g_poll_nmea(rx_chunk, LC76G_MAX_READ_LEN + 1, &read_len, &fifo_len);

        TickType_t now = xTaskGetTickCount();
        if (err == ESP_OK && read_len > 0) {
            last_rx_tick = now;
            total_rx_bytes += read_len;
            for (size_t i = 0; i < read_len; ++i) {
                char c = rx_chunk[i];
                if (c == '\r' || c == '\n') {
                    if (line_pos > 6) {
                        line_buf[line_pos] = '\0';
                        parse_nmea_line(line_buf);
                    }
                    line_pos = 0;
                } else {
                    if (c == '$') {
                        line_pos = 0;
                    }
                    if (line_pos < sizeof(line_buf) - 1) {
                        line_buf[line_pos++] = c;
                    }
                }
            }
        } else if (!startup_checked && total_rx_bytes == 0 &&
                   (now - task_start_tick) >= pdMS_TO_TICKS(3000)) {
            /* One-shot boot recovery: the module kept running across the ESP32 reset (hot start),
             * but its I2C slave may be out of sync with our first transaction. A single RESET_N
             * pulse re-syncs the interface while the backup-powered ephemeris/time/position
             * are retained, so the engine still hot-starts. Done at most once per boot. */
            startup_checked = true;
            ESP_LOGW(TAG, "No NMEA 3s after boot, one-shot LC76G I2C resync (hot-start data retained)");
            if (s_io_exp) {
                esp_io_expander_set_level(s_io_exp, IO_EXPANDER_PIN_NUM_7, 0);
                vTaskDelay(pdMS_TO_TICKS(50));
                esp_io_expander_set_level(s_io_exp, IO_EXPANDER_PIN_NUM_7, 1);
                vTaskDelay(pdMS_TO_TICKS(150));
            }
            last_rx_tick = xTaskGetTickCount();
        } else if ((now - last_rx_tick) >= pdMS_TO_TICKS(15000)) {
            /* Auto-recover if LC76G I2C slave FIFO ever stalls for >15s */
            ESP_LOGW(TAG, "LC76G I2C FIFO stalled (fifo=%lu), pulsing EXIO7 reset", (unsigned long)fifo_len);
            if (s_io_exp) {
                esp_io_expander_set_level(s_io_exp, IO_EXPANDER_PIN_NUM_7, 0);
                vTaskDelay(pdMS_TO_TICKS(50));
                esp_io_expander_set_level(s_io_exp, IO_EXPANDER_PIN_NUM_7, 1);
                vTaskDelay(pdMS_TO_TICKS(150));
            }
            last_rx_tick = xTaskGetTickCount();
        }

        now = xTaskGetTickCount();
        if ((now - last_diag_tick) >= pdMS_TO_TICKS(3000)) {
            last_diag_tick = now;
            gps_state_t snap;
            gps_lc76g_get_state(&snap);
            ESP_LOGI(TAG, "GNSS Diag: fix=%d(%dD), sats=%d(view=%d,alm=%d), snr=%ddB(%d%%), hdop=%.1f, vdop=%.1f(rej=%lu), alt=%ldm(geoid=%.1fm), spd=%ldkm/h, time=%02d:%02d:%02d(UTC%+d), tot=%lu",
                     snap.fix_quality, snap.fix_mode, snap.satellites_used, snap.satellites_in_view, s_gsv_last_alm_view,
                     snap.avg_snr_db, snap.signal_percent, snap.hdop, snap.vdop, (unsigned long)s_alt_reject_total,
                     (long)snap.altitude_int_m, snap.geoid_sep_m,
                     (long)snap.speed_int_kmh,
                     snap.local_hour, snap.local_minute, snap.local_second,
                     snap.tz_offset_minutes / 60,
                     (unsigned long)total_rx_bytes);
        }

        /* Drain remaining FIFO backlog immediately (30ms) so NMEA parsing never falls behind real-time;
         * also poll faster (40ms) during indoor-to-outdoor satellite acquisition window */
        if (err == ESP_OK && fifo_len > read_len && read_len > 0) {
            vTaskDelay(pdMS_TO_TICKS(30));
        } else if ((int32_t)(s_outdoor_fast_poll_until - now) > 0) {
            vTaskDelay(pdMS_TO_TICKS(40));
        } else {
            vTaskDelay(pdMS_TO_TICKS(80));
        }
    }
}

esp_err_t gps_lc76g_start(void)
{
    if (!s_gps_mutex) {
        s_gps_mutex = xSemaphoreCreateMutex();
    }

    load_nav_cache_from_nvs();
    s_gps_state.tz_offset_minutes = s_tz_offset_minutes;
    if (s_has_nav_cache) {
        s_gps_state.latitude = s_cached_lat;
        s_gps_state.longitude = s_cached_lon;
        s_gps_state.geoid_sep_m = s_cached_geoid_m;
        s_gps_state.raw_altitude_m = s_cached_alt_m;
        s_gps_state.smoothed_altitude_m = s_cached_alt_m;
        s_gps_state.altitude_int_m = (int32_t)lroundf(s_cached_alt_m);
    }

    s_io_exp = bsp_io_expander_init();
    if (s_io_exp) {
        /* HOT START: do NOT pulse the LC76G hardware reset on ESP32 boot.
         * The GNSS module stays powered across ESP32 resets/reflashes, so it still holds
         * its ephemeris, almanac, time and last position. Pulsing RESET_N here would
         * restart the engine and slow every warm boot down to a (near) cold start.
         * The TCA9554 output latch defaults to HIGH, so switching EXIO7 to output
         * never glitches the reset line low. */
        esp_io_expander_set_dir(s_io_exp, IO_EXPANDER_PIN_NUM_7, IO_EXPANDER_OUTPUT);
        esp_io_expander_set_level(s_io_exp, IO_EXPANDER_PIN_NUM_7, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    i2c_master_bus_handle_t bus_handle = bsp_i2c_get_handle();
    if (!bus_handle) {
        ESP_LOGE(TAG, "Failed to get BSP I2C bus handle");
        return ESP_FAIL;
    }

    const i2c_device_config_t write_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LC76G_I2C_ADDR_WRITE,
        .scl_speed_hz = LC76G_I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &write_cfg, &s_write_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add LC76G write device (0x50): %s", esp_err_to_name(err));
        return err;
    }

    const i2c_device_config_t read_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LC76G_I2C_ADDR_READ,
        .scl_speed_hz = LC76G_I2C_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(bus_handle, &read_cfg, &s_read_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add LC76G read device (0x54): %s", esp_err_to_name(err));
        return err;
    }

    xTaskCreatePinnedToCore(gps_lc76g_task, "gps_lc76g", 6144, NULL, 4, NULL, 0);
    return ESP_OK;
}

void gps_lc76g_get_state(gps_state_t *out_state)
{
    if (!out_state) {
        return;
    }
    if (s_gps_mutex) {
        xSemaphoreTake(s_gps_mutex, portMAX_DELAY);
        sync_time_from_system_rtc_locked();
        *out_state = s_gps_state;
        xSemaphoreGive(s_gps_mutex);
    } else {
        memset(out_state, 0, sizeof(*out_state));
    }
}
