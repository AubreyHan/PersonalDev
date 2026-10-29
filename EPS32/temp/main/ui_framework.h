#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "gps_lc76g.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Screen & Safe-Area Geometry Constants (466x466 Round AMOLED) */
#define UI_SCREEN_WIDTH         466
#define UI_SCREEN_HEIGHT        466
#define UI_SAFE_INSET_PX        37
#define UI_SAFE_DIAMETER_PX     392   /* 466 - 2 * 37 = 392 px (R_safe = 196 px) */

#define UI_PAGE_COUNT           3

/* Custom 112px bold anti-aliased digit font for Altitude & primary telemetry */
extern const lv_font_t ui_font_digits_112;

/**
 * @brief Initialize the multi-page UI framework, global top-layer signal arcs, and Page 1 (Altitude)
 *        Must be called while holding bsp_display_lock().
 */
void ui_framework_init(void);

/**
 * @brief Update GPS and Wi-Fi states in the UI.
 *        Must be called while holding bsp_display_lock().
 *
 * @param gps          Latest GPS state snapshot
 * @param wifi_pct     Wi-Fi signal strength mapped to 0..100% (0 if disconnected)
 * @param wifi_conn    true if Wi-Fi station is connected to an AP
 */
void ui_framework_update(const gps_state_t *gps, int wifi_pct, bool wifi_conn);

/**
 * @brief Get the 392x392 px inner safe-area circular container for a given page index
 */
lv_obj_t *ui_framework_get_page_safe_container(int page_index);

/**
 * @brief Tool 01 (TF Format) status states
 */
typedef enum {
    UI_TF_STATUS_NO_CARD = 0,
    UI_TF_STATUS_READY,
    UI_TF_STATUS_FORMATTING,
    UI_TF_STATUS_DONE,
    UI_TF_STATUS_FAILED,
} ui_tf_status_t;

typedef void (*ui_tool_action_cb_t)(void);

/**
 * @brief Register callback triggered when user taps Tool 01 (TF Format) tile on Page 3
 */
void ui_framework_set_tf_format_cb(ui_tool_action_cb_t cb);

/**
 * @brief Update Tool 01 (TF Format) tile status and visual feedback.
 *        Must be called while holding bsp_display_lock().
 */
void ui_framework_set_tf_status(ui_tf_status_t status);

/**
 * @brief Tool 02 (Wi-Fi QR Provisioning) stages inside the QR modal
 */
typedef enum {
    UI_WIFI_QR_STAGE_JOIN_AP = 0,   /* Step 1: Scan WIFI:T:nopass;S:ESP32-WIFI-CFG;; to join AP */
    UI_WIFI_QR_STAGE_OPEN_URL,      /* Step 2: Phone joined AP -> Scan http://192.168.4.1 or auto-popup */
    UI_WIFI_QR_STAGE_CONNECTING,    /* Step 3: Credentials submitted, connecting to router */
    UI_WIFI_QR_STAGE_CONNECTED,     /* Step 4: Connected to router & saved to NVS */
} ui_wifi_qr_stage_t;

typedef void (*ui_wifi_portal_ctrl_cb_t)(bool enable);

/**
 * @brief Register callback triggered when user opens (enable=true) or closes (enable=false)
 *        the Tool 02 (Wi-Fi Config) QR code modal.
 */
void ui_framework_set_wifi_portal_cb(ui_wifi_portal_ctrl_cb_t cb);

/**
 * @brief Update the QR modal stage (or close it if close_modal=true).
 *        Must be called while holding bsp_display_lock().
 */
void ui_framework_set_wifi_qr_stage(ui_wifi_qr_stage_t stage, bool close_modal);

typedef void (*ui_file_mgr_ctrl_cb_t)(bool enable);

/**
 * @brief Register callback triggered when user toggles Tool 03 (File Manager AP + HTTP server)
 */
void ui_framework_set_file_mgr_cb(ui_file_mgr_ctrl_cb_t cb);

/**
 * @brief Update Tool 03 (File Manager) active visual state
 *        Must be called while holding bsp_display_lock().
 */
void ui_framework_set_file_mgr_active(bool active);

/**
 * @brief Tool 04 (Track Logger) visual status states
 */
typedef enum {
    UI_TRACK_LOG_OFF = 0,
    UI_TRACK_LOG_WAIT_FIX,
    UI_TRACK_LOG_RECORDING,
    UI_TRACK_LOG_NO_CARD,
} ui_track_log_status_t;

typedef void (*ui_track_log_ctrl_cb_t)(bool enable);

/**
 * @brief Register callback triggered when user toggles Tool 04 (Track Logger)
 */
void ui_framework_set_track_log_cb(ui_track_log_ctrl_cb_t cb);

/**
 * @brief Update Tool 04 (Track Logger) visual state and elapsed recording time.
 *        Must be called while holding bsp_display_lock().
 */
void ui_framework_set_track_log_state(bool enabled, ui_track_log_status_t status, uint32_t elapsed_sec);

#ifdef __cplusplus
}
#endif
