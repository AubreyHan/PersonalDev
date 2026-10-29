#include "ui_framework.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "UI_FRAMEWORK";

static uint64_t get_monotonic_sec_64(void)
{
    static uint32_t s_last_tick_ms = 0;
    static uint64_t s_high_ms = 0;
    uint32_t now_ms = lv_tick_get();
    if (now_ms < s_last_tick_ms) {
        s_high_ms += (1ULL << 32);
    }
    s_last_tick_ms = now_ms;
    return (s_high_ms + (uint64_t)now_ms) / 1000ULL;
}

/* Colors */
#define COLOR_BG_BLACK          lv_color_hex(0x000000)
#define COLOR_ARC_TRACK         lv_color_hex(0x16221B)
#define COLOR_SIGNAL_GREEN      lv_color_hex(0x00E676)
#define COLOR_LABEL_DIM         lv_color_hex(0x526B5E)
#define COLOR_TITLE_MUTED       lv_color_hex(0x78909C)
#define COLOR_TEXT_WHITE        lv_color_hex(0xFFFFFF)
#define COLOR_DOT_INACTIVE      lv_color_hex(0x37474F)

/* Global Top-Layer Signal & Time Widgets */
static lv_obj_t *s_gps_arc = NULL;
static lv_obj_t *s_wifi_arc = NULL;
static lv_obj_t *s_gps_tag_label = NULL;
static lv_obj_t *s_wifi_tag_label = NULL;
static lv_obj_t *s_top_time_label = NULL;
static int s_last_time_hh = -1;
static int s_last_time_mm = -1;
static bool s_last_time_valid = false;

/* Bottom Transient Page Indicator Dots (on lv_layer_top) */
static lv_obj_t *s_dots_cont = NULL;
static lv_obj_t *s_dots[UI_PAGE_COUNT] = {NULL};
static lv_timer_t *s_dots_hide_timer = NULL;

/* Multi-Page Containers */
static lv_obj_t *s_root_screen = NULL;
static lv_obj_t *s_page_wrappers[UI_PAGE_COUNT] = {NULL};
static lv_obj_t *s_page_safe_conts[UI_PAGE_COUNT] = {NULL};
static int s_current_page = 0;

/* Touch Swipe Gesture State */
static int32_t s_touch_start_x = 0;
static int32_t s_touch_start_y = 0;
static int32_t s_last_dx = 0;
static bool s_is_dragging = false;
static bool s_anim_running = false;
static int s_drag_peer_page = -1;

/* Page 1: 3-Section Altitude Display Widgets (Top: Current, Middle: 1H FIFO Polyline, Bottom: Min/Max) */
#define ALT_FIFO_WINDOW_SEC     3600U
#define ALT_FIFO_CAP            3600
#define ALT_LINE_MAX_POINTS     120

static lv_obj_t *s_p1_content_group = NULL;
static lv_obj_t *s_p1_title_label = NULL;
static lv_obj_t *s_p1_value_label = NULL;
static lv_obj_t *s_p1_unit_label = NULL;
static lv_obj_t *s_p1_chart_card = NULL;
static lv_obj_t *s_p1_chart_range_label = NULL;
static lv_obj_t *s_p1_polyline = NULL;
static lv_obj_t *s_p1_chart_dot = NULL;
static lv_obj_t *s_p1_min_label = NULL;
static lv_obj_t *s_p1_max_label = NULL;

static bool s_p1_visible = false;
static int32_t s_p1_last_alt_m = INT32_MIN;
static int32_t s_p1_last_min_m = INT32_MIN;
static int32_t s_p1_last_max_m = INT32_MIN;
static uint32_t s_p1_last_fix_seq = 0;

/* 1-Hour (3600s) FIFO ring buffer for middle altitude & speed polyline charts */
static float s_alt_fifo_val[ALT_FIFO_CAP];
static float s_spd_fifo_val[ALT_FIFO_CAP];
static uint64_t s_alt_fifo_ts[ALT_FIFO_CAP];
static int s_alt_fifo_head = 0;
static int s_alt_fifo_count = 0;
static uint64_t s_alt_last_push_sec = 0;
static bool s_p1_chart_dirty = false;
static bool s_p2_chart_dirty = false;
static lv_point_precise_t s_line_points[ALT_LINE_MAX_POINTS];
static lv_point_precise_t s_p2_line_points[ALT_LINE_MAX_POINTS];

/* Page 2: 3-Section Speed Display Widgets (Top: Current km/h, Middle: 1H FIFO Polyline, Bottom: Max Speed) */
static lv_obj_t *s_p2_content_group = NULL;
static lv_obj_t *s_p2_title_label = NULL;
static lv_obj_t *s_p2_value_label = NULL;
static lv_obj_t *s_p2_unit_label = NULL;
static lv_obj_t *s_p2_chart_card = NULL;
static lv_obj_t *s_p2_chart_range_label = NULL;
static lv_obj_t *s_p2_polyline = NULL;
static lv_obj_t *s_p2_chart_dot = NULL;
static lv_obj_t *s_p2_max_label = NULL;

static bool s_p2_visible = false;
static int32_t s_p2_last_spd_kmh = INT32_MIN;
static int32_t s_p2_last_max_kmh = INT32_MIN;

/* Page 3: 2x2 Four-Grid Tools Page Widgets */
#define P3_TOOL_COUNT           4
#define WIFI_AP_QR_PAYLOAD      "WIFI:T:nopass;S:ESP32-WIFI-CFG;;"
#define WIFI_URL_QR_PAYLOAD     "http://192.168.4.1"

static lv_obj_t *s_p3_content_group = NULL;
static lv_obj_t *s_p3_title_label = NULL;
static lv_obj_t *s_p3_tile_cards[P3_TOOL_COUNT] = {NULL};
static lv_obj_t *s_p3_tile_badges[P3_TOOL_COUNT] = {NULL};
static lv_obj_t *s_p3_tile_titles[P3_TOOL_COUNT] = {NULL};
static lv_obj_t *s_p3_tile_subtitles[P3_TOOL_COUNT] = {NULL};
static int s_p3_pressed_tile = -1;
static bool s_p3_tile_active[P3_TOOL_COUNT] = {false};
static ui_tf_status_t s_tf_status = UI_TF_STATUS_READY;
static ui_tool_action_cb_t s_tf_format_cb = NULL;
static ui_file_mgr_ctrl_cb_t s_file_mgr_cb = NULL;
static ui_track_log_ctrl_cb_t s_track_log_cb = NULL;
static ui_track_log_status_t s_track_log_status = UI_TRACK_LOG_OFF;
static uint32_t s_track_log_elapsed_sec = 0;

/* Tool 02: Wi-Fi QR Provisioning Modal Widgets */
static lv_obj_t *s_p3_qr_modal = NULL;
static lv_obj_t *s_p3_qr_step_label = NULL;
static lv_obj_t *s_p3_qrcode = NULL;
static lv_obj_t *s_p3_qr_hint_label = NULL;
static lv_obj_t *s_p3_qr_close_btn = NULL;
static bool s_p3_qr_modal_open = false;
static bool s_p3_qr_close_pressed = false;
static bool s_p3_wifi_online = false;
static ui_wifi_portal_ctrl_cb_t s_wifi_portal_cb = NULL;

/* GPS Breathing Animation State */
static bool s_gps_has_fix = false;
static int s_gps_signal_pct = 0;
static uint32_t s_breath_tick = 0;

/* Power-On GNSS Initialization Page (shown until first GPS fix is acquired) */
static lv_obj_t *s_init_overlay = NULL;
static lv_obj_t *s_init_scan_arc = NULL;
static lv_obj_t *s_init_sat_num_label = NULL;
static lv_obj_t *s_init_sat_tag_label = NULL;
static lv_obj_t *s_init_status_label = NULL;
static bool s_init_page_active = true;
static bool s_init_lock_pending = false;
static uint32_t s_init_lock_tick = 0;
static int s_init_last_sats = -1;
static int s_init_last_snr = -1;
static uint32_t s_init_scan_angle = 0;

/* Forward Declarations */
static void build_init_boot_page(void);
static void build_top_layer_signal_arcs(void);
static void build_bottom_page_dots(void);
static void build_page1_altitude(lv_obj_t *safe_cont);
static void build_page2_speed(lv_obj_t *safe_cont);
static void build_page3_tools(lv_obj_t *safe_cont);
static int hit_test_p3_tool_tile(const lv_point_t *pt);
static bool hit_test_p3_qr_close(const lv_point_t *pt);
static void update_p3_tile_visual(int idx, bool is_pressed);
static void update_p3_qr_close_visual(bool is_pressed);
static void redraw_altitude_polyline(void);
static void redraw_speed_polyline(void);

/* Helper: set opacity of the bottom indicator dots container */
static void dots_opa_anim_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void dots_hide_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_dots_cont) {
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_dots_cont);
    lv_anim_set_values(&a, lv_obj_get_style_opa(s_dots_cont, 0), LV_OPA_TRANSP);
    lv_anim_set_duration(&a, 300);
    lv_anim_set_exec_cb(&a, dots_opa_anim_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void show_page_dots_transiently(void)
{
    if (!s_dots_cont) {
        return;
    }
    lv_anim_delete(s_dots_cont, dots_opa_anim_cb);
    lv_obj_set_style_opa(s_dots_cont, LV_OPA_COVER, 0);

    for (int i = 0; i < UI_PAGE_COUNT; ++i) {
        if (i == s_current_page) {
            lv_obj_set_size(s_dots[i], 16, 6);
            lv_obj_set_style_bg_color(s_dots[i], COLOR_SIGNAL_GREEN, 0);
        } else {
            lv_obj_set_size(s_dots[i], 6, 6);
            lv_obj_set_style_bg_color(s_dots[i], COLOR_DOT_INACTIVE, 0);
        }
    }

    if (s_dots_hide_timer) {
        lv_timer_reset(s_dots_hide_timer);
        lv_timer_resume(s_dots_hide_timer);
    }
}

/* Helper: Page X-offset animation callbacks */
static void page_x_anim_cb(void *var, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)var, v);
}

static void page_swipe_anim_ready_cb(lv_anim_t *a)
{
    (void)a;
    s_anim_running = false;
    s_drag_peer_page = -1;
    for (int i = 0; i < UI_PAGE_COUNT; ++i) {
        if (i == s_current_page) {
            lv_obj_set_x(s_page_wrappers[i], 0);
            lv_obj_remove_flag(s_page_wrappers[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_set_x(s_page_wrappers[i], UI_SCREEN_WIDTH);
            lv_obj_add_flag(s_page_wrappers[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void screen_touch_event_cb(lv_event_t *e)
{
    if (s_init_page_active) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();

    if (code == LV_EVENT_PRESSED) {
        if (s_anim_running || !indev) {
            return;
        }
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);
        s_touch_start_x = pt.x;
        s_touch_start_y = pt.y;
        s_last_dx = 0;
        s_is_dragging = false;
        s_drag_peer_page = -1;

        if (s_current_page == 2) {
            if (s_p3_qr_modal_open) {
                s_p3_pressed_tile = -1;
                s_p3_qr_close_pressed = hit_test_p3_qr_close(&pt);
                if (s_p3_qr_close_pressed) {
                    update_p3_qr_close_visual(true);
                }
            } else {
                s_p3_qr_close_pressed = false;
                s_p3_pressed_tile = hit_test_p3_tool_tile(&pt);
                if (s_p3_pressed_tile >= 0) {
                    update_p3_tile_visual(s_p3_pressed_tile, true);
                }
            }
        } else {
            s_p3_pressed_tile = -1;
            s_p3_qr_close_pressed = false;
        }
    } else if (code == LV_EVENT_PRESSING) {
        if (s_anim_running || !indev || UI_PAGE_COUNT < 2) {
            return;
        }
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);
        int32_t dx = pt.x - s_touch_start_x;
        int32_t dy = pt.y - s_touch_start_y;

        if (!s_is_dragging) {
            if (LV_ABS(dx) > 22 && LV_ABS(dx) > LV_ABS(dy)) {
                s_is_dragging = true;
                if (s_p3_pressed_tile >= 0) {
                    update_p3_tile_visual(s_p3_pressed_tile, false);
                    s_p3_pressed_tile = -1;
                }
                if (s_p3_qr_close_pressed) {
                    update_p3_qr_close_visual(false);
                    s_p3_qr_close_pressed = false;
                }
                show_page_dots_transiently();
                if (s_dots_hide_timer) {
                    lv_timer_pause(s_dots_hide_timer);
                }
            }
        }

        if (s_is_dragging) {
            s_last_dx = dx;
            lv_obj_set_x(s_page_wrappers[s_current_page], dx);

            int peer = (dx < 0)
                           ? ((s_current_page + 1) % UI_PAGE_COUNT)
                           : ((s_current_page - 1 + UI_PAGE_COUNT) % UI_PAGE_COUNT);

            if (s_drag_peer_page != -1 && s_drag_peer_page != peer && s_drag_peer_page != s_current_page) {
                lv_obj_add_flag(s_page_wrappers[s_drag_peer_page], LV_OBJ_FLAG_HIDDEN);
            }
            if (peer == 0 && s_p1_chart_dirty) {
                s_p1_chart_dirty = false;
                redraw_altitude_polyline();
            } else if (peer == 1 && s_p2_chart_dirty) {
                s_p2_chart_dirty = false;
                redraw_speed_polyline();
            }
            s_drag_peer_page = peer;
            lv_obj_remove_flag(s_page_wrappers[peer], LV_OBJ_FLAG_HIDDEN);

            int32_t peer_x = (dx < 0) ? (UI_SCREEN_WIDTH + dx) : (-UI_SCREEN_WIDTH + dx);
            lv_obj_set_x(s_page_wrappers[peer], peer_x);
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (!s_is_dragging) {
            if (s_p3_qr_close_pressed) {
                s_p3_qr_close_pressed = false;
                update_p3_qr_close_visual(false);
                if (code == LV_EVENT_RELEASED && indev && s_current_page == 2 && s_p3_qr_modal_open) {
                    lv_point_t pt;
                    lv_indev_get_point(indev, &pt);
                    if (hit_test_p3_qr_close(&pt)) {
                        ESP_LOGI(TAG, "Tools Page: closing Wi-Fi QR modal");
                        ui_framework_set_wifi_qr_stage(UI_WIFI_QR_STAGE_JOIN_AP, true);
                        if (s_wifi_portal_cb) {
                            s_wifi_portal_cb(false);
                        }
                    }
                }
                return;
            }
            if (s_p3_pressed_tile >= 0) {
                int tapped_idx = s_p3_pressed_tile;
                s_p3_pressed_tile = -1;
                if (code == LV_EVENT_RELEASED && indev && s_current_page == 2) {
                    lv_point_t pt;
                    lv_indev_get_point(indev, &pt);
                    if (hit_test_p3_tool_tile(&pt) == tapped_idx) {
                        if (tapped_idx == 0) {
                            /* Tool 01: TF Card Format */
                            if (s_tf_status != UI_TF_STATUS_FORMATTING) {
                                ESP_LOGI(TAG, "Tools Page: tapped slot 1 (TF FORMAT)");
                                s_tf_status = UI_TF_STATUS_FORMATTING;
                                update_p3_tile_visual(0, false);
                                if (s_tf_format_cb) {
                                    s_tf_format_cb();
                                }
                                return;
                            }
                        } else if (tapped_idx == 1) {
                            /* Tool 02: Wi-Fi QR Code Configuration */
                            ESP_LOGI(TAG, "Tools Page: tapped slot 2 (WIFI CFG) -> open QR modal");
                            if (s_p3_tile_active[2]) {
                                s_p3_tile_active[2] = false;
                                update_p3_tile_visual(2, false);
                            }
                            update_p3_tile_visual(1, false);
                            ui_framework_set_wifi_qr_stage(UI_WIFI_QR_STAGE_JOIN_AP, false);
                            if (s_wifi_portal_cb) {
                                s_wifi_portal_cb(true);
                            }
                            return;
                        } else if (tapped_idx == 2) {
                            /* Tool 03: TF Card HTTP File Manager (AP + Web Server) */
                            s_p3_tile_active[2] = !s_p3_tile_active[2];
                            ESP_LOGI(TAG, "Tools Page: tapped slot 3 (FILE MGR) -> active=%d",
                                     (int)s_p3_tile_active[2]);
                            update_p3_tile_visual(2, false);
                            if (s_file_mgr_cb) {
                                s_file_mgr_cb(s_p3_tile_active[2]);
                            }
                            return;
                        } else if (tapped_idx == 3) {
                            /* Tool 04: 1Hz GNSS Trajectory Logger (.gps) */
                            s_p3_tile_active[3] = !s_p3_tile_active[3];
                            s_track_log_elapsed_sec = 0;
                            s_track_log_status = s_p3_tile_active[3]
                                                     ? (s_gps_has_fix ? UI_TRACK_LOG_RECORDING : UI_TRACK_LOG_WAIT_FIX)
                                                     : UI_TRACK_LOG_OFF;
                            ESP_LOGI(TAG, "Tools Page: tapped slot 4 (TRACK LOG) -> active=%d, status=%d",
                                     (int)s_p3_tile_active[3], (int)s_track_log_status);
                            update_p3_tile_visual(3, false);
                            if (s_track_log_cb) {
                                s_track_log_cb(s_p3_tile_active[3]);
                            }
                            return;
                        } else {
                            s_p3_tile_active[tapped_idx] = !s_p3_tile_active[tapped_idx];
                            ESP_LOGI(TAG, "Tools Page: tapped slot %d (active=%d)",
                                     tapped_idx + 1, (int)s_p3_tile_active[tapped_idx]);
                        }
                    }
                }
                update_p3_tile_visual(tapped_idx, false);
            }
            return;
        }
        if (s_p3_pressed_tile >= 0) {
            update_p3_tile_visual(s_p3_pressed_tile, false);
            s_p3_pressed_tile = -1;
        }
        if (s_p3_qr_close_pressed) {
            update_p3_qr_close_visual(false);
            s_p3_qr_close_pressed = false;
        }
        s_is_dragging = false;
        int32_t dx = s_last_dx;
        int peer = s_drag_peer_page;

        if (peer != -1 && LV_ABS(dx) > 65) {
            /* Complete page switch */
            int old_page = s_current_page;
            s_current_page = peer;
            s_anim_running = true;

            if (old_page == 2 && s_p3_qr_modal_open) {
                ui_framework_set_wifi_qr_stage(UI_WIFI_QR_STAGE_JOIN_AP, true);
                if (s_wifi_portal_cb) {
                    s_wifi_portal_cb(false);
                }
            }

            int32_t old_target_x = (dx < 0) ? -UI_SCREEN_WIDTH : UI_SCREEN_WIDTH;

            lv_anim_t a_old;
            lv_anim_init(&a_old);
            lv_anim_set_var(&a_old, s_page_wrappers[old_page]);
            lv_anim_set_values(&a_old, dx, old_target_x);
            lv_anim_set_duration(&a_old, 210);
            lv_anim_set_exec_cb(&a_old, page_x_anim_cb);
            lv_anim_set_path_cb(&a_old, lv_anim_path_ease_out);
            lv_anim_start(&a_old);

            lv_anim_t a_new;
            lv_anim_init(&a_new);
            lv_anim_set_var(&a_new, s_page_wrappers[s_current_page]);
            lv_anim_set_values(&a_new, lv_obj_get_x(s_page_wrappers[s_current_page]), 0);
            lv_anim_set_duration(&a_new, 210);
            lv_anim_set_exec_cb(&a_new, page_x_anim_cb);
            lv_anim_set_path_cb(&a_new, lv_anim_path_ease_out);
            lv_anim_set_completed_cb(&a_new, page_swipe_anim_ready_cb);
            lv_anim_start(&a_new);

            ESP_LOGI(TAG, "Switched to page %d", s_current_page + 1);
        } else if (peer != -1) {
            /* Snap back to current page */
            s_anim_running = true;
            int32_t peer_target_x = (dx < 0) ? UI_SCREEN_WIDTH : -UI_SCREEN_WIDTH;

            lv_anim_t a_cur;
            lv_anim_init(&a_cur);
            lv_anim_set_var(&a_cur, s_page_wrappers[s_current_page]);
            lv_anim_set_values(&a_cur, dx, 0);
            lv_anim_set_duration(&a_cur, 180);
            lv_anim_set_exec_cb(&a_cur, page_x_anim_cb);
            lv_anim_set_path_cb(&a_cur, lv_anim_path_ease_out);
            lv_anim_start(&a_cur);

            lv_anim_t a_peer;
            lv_anim_init(&a_peer);
            lv_anim_set_var(&a_peer, s_page_wrappers[peer]);
            lv_anim_set_values(&a_peer, lv_obj_get_x(s_page_wrappers[peer]), peer_target_x);
            lv_anim_set_duration(&a_peer, 180);
            lv_anim_set_exec_cb(&a_peer, page_x_anim_cb);
            lv_anim_set_path_cb(&a_peer, lv_anim_path_ease_out);
            lv_anim_set_completed_cb(&a_peer, page_swipe_anim_ready_cb);
            lv_anim_start(&a_peer);
        }

        show_page_dots_transiently();
    }
}

/* Periodic 50ms LVGL timer for smooth GPS breathing when searching & init page orbital sweep */
static void signal_breath_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    /* 1. Animate power-on GNSS Initialization Page orbital scanner & post-fix transition */
    if (s_init_page_active && s_init_overlay) {
        if (s_init_lock_pending) {
            if ((uint32_t)(lv_tick_get() - s_init_lock_tick) >= 450U) {
                s_init_page_active = false;
                s_init_lock_pending = false;
                lv_obj_add_flag(s_init_overlay, LV_OBJ_FLAG_HIDDEN);
                if (s_page_wrappers[s_current_page]) {
                    lv_obj_set_x(s_page_wrappers[s_current_page], 0);
                    lv_obj_remove_flag(s_page_wrappers[s_current_page], LV_OBJ_FLAG_HIDDEN);
                }
                if (s_current_page == 0 && s_p1_chart_dirty) {
                    s_p1_chart_dirty = false;
                    redraw_altitude_polyline();
                } else if (s_current_page == 1 && s_p2_chart_dirty) {
                    s_p2_chart_dirty = false;
                    redraw_speed_polyline();
                }
                show_page_dots_transiently();
                ESP_LOGI(TAG, "GNSS fix acquired -> exited initialization page into Page %d", s_current_page + 1);
            }
        } else if (s_init_scan_arc) {
            s_init_scan_angle = (s_init_scan_angle + 9U) % 360U;
            int32_t span = (s_init_last_sats > 0) ? 90 : 65;
            lv_arc_set_angles(s_init_scan_arc,
                              (int32_t)s_init_scan_angle,
                              (int32_t)((s_init_scan_angle + (uint32_t)span) % 360U));
        }
    }

    /* 2. Animate outer left GPS signal arc breathing */
    if (!s_gps_arc) {
        return;
    }

    if (s_gps_signal_pct <= 0) {
        lv_obj_set_style_arc_opa(s_gps_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
        return;
    }

    if (s_gps_has_fix) {
        lv_obj_set_style_arc_opa(s_gps_arc, LV_OPA_COVER, LV_PART_INDICATOR);
    } else {
        /* Smooth sinusoidal breathing between 35% (89) and 80% (204) opacity */
        s_breath_tick += 1;
        float phase = (float)s_breath_tick * 0.18f;
        float norm = (sinf(phase) + 1.0f) * 0.5f;
        lv_opa_t opa = (lv_opa_t)(89 + (int)(norm * 115.0f));
        lv_obj_set_style_arc_opa(s_gps_arc, opa, LV_PART_INDICATOR);
    }
}

static void build_init_boot_page(void)
{
    s_init_overlay = lv_obj_create(s_root_screen);
    lv_obj_remove_style_all(s_init_overlay);
    lv_obj_set_size(s_init_overlay, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT);
    lv_obj_center(s_init_overlay);
    lv_obj_set_style_bg_color(s_init_overlay, COLOR_BG_BLACK, 0);
    lv_obj_set_style_bg_opa(s_init_overlay, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_init_overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *safe = lv_obj_create(s_init_overlay);
    lv_obj_remove_style_all(safe);
    lv_obj_set_size(safe, UI_SAFE_DIAMETER_PX, UI_SAFE_DIAMETER_PX);
    lv_obj_center(safe);
    lv_obj_remove_flag(safe, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* Top Title: "GNSS INITIALIZING" */
    lv_obj_t *hdr = lv_label_create(safe);
    lv_label_set_text(hdr, "GNSS INITIALIZING");
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hdr, COLOR_TITLE_MUTED, 0);
    lv_obj_set_style_text_letter_space(hdr, 3, 0);
    lv_obj_align(hdr, LV_ALIGN_CENTER, 0, -146);

    /* Center Orbital Radar Ring (210x210 px) */
    s_init_scan_arc = lv_arc_create(safe);
    lv_obj_set_size(s_init_scan_arc, 210, 210);
    lv_obj_align(s_init_scan_arc, LV_ALIGN_CENTER, 0, -8);
    lv_arc_set_bg_angles(s_init_scan_arc, 0, 360);
    lv_arc_set_angles(s_init_scan_arc, 0, 65);
    lv_obj_remove_style(s_init_scan_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_init_scan_arc, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_arc_width(s_init_scan_arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_init_scan_arc, lv_color_hex(0x122219), LV_PART_MAIN);

    lv_obj_set_style_arc_width(s_init_scan_arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_init_scan_arc, COLOR_SIGNAL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_init_scan_arc, true, LV_PART_INDICATOR);

    /* Giant Satellite Count inside the orbital ring */
    s_init_sat_num_label = lv_label_create(safe);
    lv_label_set_text(s_init_sat_num_label, "0");
    lv_obj_set_style_text_font(s_init_sat_num_label, &ui_font_digits_112, 0);
    lv_obj_set_style_text_color(s_init_sat_num_label, COLOR_LABEL_DIM, 0);
    lv_obj_align(s_init_sat_num_label, LV_ALIGN_CENTER, 0, -24);

    /* "SATELLITES" sub-caption inside bottom of orbital ring */
    s_init_sat_tag_label = lv_label_create(safe);
    lv_label_set_text(s_init_sat_tag_label, "SATELLITES");
    lv_obj_set_style_text_font(s_init_sat_tag_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_init_sat_tag_label, COLOR_LABEL_DIM, 0);
    lv_obj_set_style_text_letter_space(s_init_sat_tag_label, 3, 0);
    lv_obj_align(s_init_sat_tag_label, LV_ALIGN_CENTER, 0, 48);

    /* Bottom Status Pill Card */
    lv_obj_t *pill = lv_obj_create(safe);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, 248, 34);
    lv_obj_align(pill, LV_ALIGN_CENTER, 0, 128);
    lv_obj_set_style_bg_color(pill, lv_color_hex(0x0A130E), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_border_color(pill, lv_color_hex(0x1B3024), 0);
    lv_obj_set_style_radius(pill, 17, 0);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_init_status_label = lv_label_create(pill);
    lv_label_set_text(s_init_status_label, "SEARCHING SKY...");
    lv_obj_set_style_text_font(s_init_status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_init_status_label, COLOR_LABEL_DIM, 0);
    lv_obj_set_style_text_letter_space(s_init_status_label, 2, 0);
    lv_obj_center(s_init_status_label);
}

static void build_top_layer_signal_arcs(void)
{
    lv_obj_t *top = lv_layer_top();

    /*
     * Arc Geometry on 466x466 round screen:
     * Outer diameter: 446 x 446 px (centered, 10px inset from physical rim)
     * Width: 10 px -> occupies r in [213..223 px], well outside the 392x392 (r<=196) safe zone.
     */
    const int32_t arc_diameter = 446;
    const int32_t arc_width = 10;

    /* 1. Left GPS Signal Arc (125 deg bottom-left -> 235 deg top-left, 110 deg span) */
    s_gps_arc = lv_arc_create(top);
    lv_obj_set_size(s_gps_arc, arc_diameter, arc_diameter);
    lv_obj_center(s_gps_arc);
    lv_arc_set_bg_angles(s_gps_arc, 125, 235);
    lv_arc_set_angles(s_gps_arc, 125, 125);
    lv_obj_remove_style(s_gps_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_gps_arc, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_arc_width(s_gps_arc, arc_width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_gps_arc, COLOR_ARC_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(s_gps_arc, true, LV_PART_MAIN);

    lv_obj_set_style_arc_width(s_gps_arc, arc_width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_gps_arc, COLOR_SIGNAL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_gps_arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_gps_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);

    /* Micro "GPS" tag near bottom-left end of arc (angle ~118 deg) */
    s_gps_tag_label = lv_label_create(top);
    lv_label_set_text(s_gps_tag_label, "GPS");
    lv_obj_set_style_text_font(s_gps_tag_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_gps_tag_label, COLOR_LABEL_DIM, 0);
    lv_obj_set_pos(s_gps_tag_label, 108, 405);
    lv_obj_remove_flag(s_gps_tag_label, LV_OBJ_FLAG_CLICKABLE);

    /* 2. Right Wi-Fi Signal Arc (305 deg top-right -> 55 deg bottom-right, 110 deg span) */
    s_wifi_arc = lv_arc_create(top);
    lv_obj_set_size(s_wifi_arc, arc_diameter, arc_diameter);
    lv_obj_center(s_wifi_arc);
    lv_arc_set_bg_angles(s_wifi_arc, 305, 55);
    lv_arc_set_angles(s_wifi_arc, 55, 55);
    lv_obj_remove_style(s_wifi_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_wifi_arc, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_arc_width(s_wifi_arc, arc_width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_wifi_arc, COLOR_ARC_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(s_wifi_arc, true, LV_PART_MAIN);

    lv_obj_set_style_arc_width(s_wifi_arc, arc_width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_wifi_arc, COLOR_SIGNAL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_wifi_arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_wifi_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);

    /* Micro "WiFi" tag near bottom-right end of arc (angle ~62 deg) */
    s_wifi_tag_label = lv_label_create(top);
    lv_label_set_text(s_wifi_tag_label, "WiFi");
    lv_obj_set_style_text_font(s_wifi_tag_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_wifi_tag_label, COLOR_LABEL_DIM, 0);
    lv_obj_set_pos(s_wifi_tag_label, 330, 405);
    lv_obj_remove_flag(s_wifi_tag_label, LV_OBJ_FLAG_CLICKABLE);

    /* 3. Top-Center 24-Hour Clock (stationary on lv_layer_top across all pages) */
    s_top_time_label = lv_label_create(top);
    lv_label_set_text(s_top_time_label, "--:--");
    lv_obj_set_style_text_font(s_top_time_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_top_time_label, COLOR_LABEL_DIM, 0);
    lv_obj_set_style_text_letter_space(s_top_time_label, 2, 0);
    lv_obj_align(s_top_time_label, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_remove_flag(s_top_time_label, LV_OBJ_FLAG_CLICKABLE);

    /* Start 50ms breathing animation timer for GPS search state */
    lv_timer_create(signal_breath_timer_cb, 50, NULL);
}

static void build_bottom_page_dots(void)
{
    lv_obj_t *top = lv_layer_top();

    s_dots_cont = lv_obj_create(top);
    lv_obj_remove_style_all(s_dots_cont);
    lv_obj_set_size(s_dots_cont, 100, 14);
    lv_obj_align(s_dots_cont, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_flex_flow(s_dots_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_dots_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_dots_cont, 8, 0);
    lv_obj_set_style_opa(s_dots_cont, LV_OPA_TRANSP, 0); /* Hidden by default */
    lv_obj_remove_flag(s_dots_cont, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < UI_PAGE_COUNT; ++i) {
        s_dots[i] = lv_obj_create(s_dots_cont);
        lv_obj_remove_style_all(s_dots[i]);
        lv_obj_set_size(s_dots[i], (i == 0) ? 16 : 6, 6);
        lv_obj_set_style_radius(s_dots[i], 3, 0);
        lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_dots[i], (i == 0) ? COLOR_SIGNAL_GREEN : COLOR_DOT_INACTIVE, 0);
        lv_obj_remove_flag(s_dots[i], LV_OBJ_FLAG_CLICKABLE);
    }

    s_dots_hide_timer = lv_timer_create(dots_hide_timer_cb, 1000, NULL);
    lv_timer_pause(s_dots_hide_timer);
}

static void redraw_altitude_polyline(void)
{
    if (!s_p1_polyline || !s_p1_chart_dot || !s_p1_chart_range_label) {
        return;
    }

    const float plot_x0 = 14.0f;
    const float plot_w  = 288.0f;
    const float plot_y0 = 24.0f;
    const float plot_h  = 66.0f;

    if (s_alt_fifo_count <= 0) {
        s_line_points[0].x = plot_x0;
        s_line_points[0].y = plot_y0 + plot_h * 0.5f;
        s_line_points[1].x = plot_x0 + plot_w;
        s_line_points[1].y = plot_y0 + plot_h * 0.5f;
        lv_line_set_points(s_p1_polyline, s_line_points, 2);
        lv_obj_set_style_line_color(s_p1_polyline, COLOR_ARC_TRACK, 0);
        lv_obj_add_flag(s_p1_chart_dot, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_p1_chart_range_label, "WAITING...");
        return;
    }

    /* Find min/max across the 1-hour FIFO window */
    float first_v = s_alt_fifo_val[s_alt_fifo_head];
    float fifo_min = first_v;
    float fifo_max = first_v;
    for (int i = 1; i < s_alt_fifo_count; ++i) {
        float v = s_alt_fifo_val[(s_alt_fifo_head + i) % ALT_FIFO_CAP];
        if (v < fifo_min) fifo_min = v;
        if (v > fifo_max) fifo_max = v;
    }

    /* Update range header inside chart with 1-hour FIFO min ~ max */
    char rbuf[32];
    snprintf(rbuf, sizeof(rbuf), "%ld ~ %ld m",
             (long)lroundf(fifo_min), (long)lroundf(fifo_max));
    lv_label_set_text(s_p1_chart_range_label, rbuf);

    /* Enforce minimum 10m vertical span to prevent static jitter amplification */
    float min_v = fifo_min;
    float max_v = fifo_max;
    float span = max_v - min_v;
    if (span < 10.0f) {
        float mid = 0.5f * (min_v + max_v);
        min_v = mid - 5.0f;
        max_v = mid + 5.0f;
        span = 10.0f;
    } else {
        float pad = span * 0.12f;
        min_v -= pad;
        max_v += pad;
        span = max_v - min_v;
    }

    if (s_alt_fifo_count == 1) {
        float y = plot_y0 + plot_h * 0.5f;
        s_line_points[0].x = plot_x0;
        s_line_points[0].y = y;
        s_line_points[1].x = plot_x0 + plot_w;
        s_line_points[1].y = y;
        lv_line_set_points(s_p1_polyline, s_line_points, 2);
        lv_obj_set_style_line_color(s_p1_polyline, COLOR_SIGNAL_GREEN, 0);
        lv_obj_remove_flag(s_p1_chart_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_p1_chart_dot, (int32_t)(plot_x0 + plot_w) - 4, (int32_t)y - 4);
        return;
    }

    int n = (s_alt_fifo_count <= ALT_LINE_MAX_POINTS) ? s_alt_fifo_count : ALT_LINE_MAX_POINTS;
    for (int i = 0; i < n; ++i) {
        float sample_v;
        if (s_alt_fifo_count <= ALT_LINE_MAX_POINTS) {
            sample_v = s_alt_fifo_val[(s_alt_fifo_head + i) % ALT_FIFO_CAP];
        } else if (i == n - 1) {
            /* Always pin rightmost vertex to the newest FIFO sample */
            sample_v = s_alt_fifo_val[(s_alt_fifo_head + s_alt_fifo_count - 1) % ALT_FIFO_CAP];
        } else {
            int start_idx = (i * s_alt_fifo_count) / n;
            int end_idx   = ((i + 1) * s_alt_fifo_count) / n;
            if (end_idx <= start_idx) {
                end_idx = start_idx + 1;
            }
            float sum = 0.0f;
            for (int k = start_idx; k < end_idx; ++k) {
                sum += s_alt_fifo_val[(s_alt_fifo_head + k) % ALT_FIFO_CAP];
            }
            sample_v = sum / (float)(end_idx - start_idx);
        }

        float t = (float)i / (float)(n - 1);
        float norm = (sample_v - min_v) / span;
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        s_line_points[i].x = plot_x0 + t * plot_w;
        s_line_points[i].y = plot_y0 + (1.0f - norm) * plot_h;
    }

    lv_line_set_points(s_p1_polyline, s_line_points, n);
    lv_obj_set_style_line_color(s_p1_polyline, COLOR_SIGNAL_GREEN, 0);
    lv_obj_remove_flag(s_p1_chart_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_p1_chart_dot,
                   (int32_t)s_line_points[n - 1].x - 4,
                   (int32_t)s_line_points[n - 1].y - 4);
}

static void build_page1_altitude(lv_obj_t *safe_cont)
{
    /*
     * Page 1: 3-Section Altitude Dashboard inside the 392x392 safe circular container:
     *   - Top Section    (dy = -168 .. -54): Current Altitude ("ALTITUDE" + value + unit)
     *   - Middle Section (dy =  -46 .. +60): Power-on Altitude Polyline Chart (316x106 card)
     *   - Bottom Section (dy =  +68 .. +165): Left = Min Altitude, Right = Max Altitude
     */
    s_p1_content_group = lv_obj_create(safe_cont);
    lv_obj_remove_style_all(s_p1_content_group);
    lv_obj_set_size(s_p1_content_group, UI_SAFE_DIAMETER_PX, UI_SAFE_DIAMETER_PX);
    lv_obj_center(s_p1_content_group);
    lv_obj_remove_flag(s_p1_content_group, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* ==================== 1. TOP SECTION: CURRENT ALTITUDE ==================== */
    s_p1_title_label = lv_label_create(s_p1_content_group);
    lv_label_set_text(s_p1_title_label, "ALTITUDE");
    lv_obj_set_style_text_font(s_p1_title_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_p1_title_label, COLOR_TITLE_MUTED, 0);
    lv_obj_set_style_text_letter_space(s_p1_title_label, 3, 0);
    lv_obj_align(s_p1_title_label, LV_ALIGN_CENTER, 0, -146);

    s_p1_value_label = lv_label_create(s_p1_content_group);
    lv_label_set_text(s_p1_value_label, "---");
    lv_obj_set_style_text_font(s_p1_value_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_p1_value_label, COLOR_LABEL_DIM, 0);
    lv_obj_set_style_text_letter_space(s_p1_value_label, 1, 0);
    lv_obj_align(s_p1_value_label, LV_ALIGN_CENTER, 0, -104);

    s_p1_unit_label = lv_label_create(s_p1_content_group);
    lv_label_set_text(s_p1_unit_label, "SEARCHING");
    lv_obj_set_style_text_font(s_p1_unit_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_p1_unit_label, COLOR_LABEL_DIM, 0);
    lv_obj_set_style_text_letter_space(s_p1_unit_label, 2, 0);
    lv_obj_align(s_p1_unit_label, LV_ALIGN_CENTER, 0, -66);

    /* ==================== 2. MIDDLE SECTION: POLYLINE CHART ==================== */
    s_p1_chart_card = lv_obj_create(s_p1_content_group);
    lv_obj_remove_style_all(s_p1_chart_card);
    lv_obj_set_size(s_p1_chart_card, 316, 106);
    lv_obj_align(s_p1_chart_card, LV_ALIGN_CENTER, 0, 4);
    lv_obj_set_style_bg_color(s_p1_chart_card, lv_color_hex(0x0A130E), 0);
    lv_obj_set_style_bg_opa(s_p1_chart_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_p1_chart_card, 1, 0);
    lv_obj_set_style_border_color(s_p1_chart_card, lv_color_hex(0x1B3024), 0);
    lv_obj_set_style_radius(s_p1_chart_card, 12, 0);
    lv_obj_remove_flag(s_p1_chart_card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* 3 subtle horizontal reference grid lines inside chart card */
    const int32_t grid_ys[3] = {24, 57, 90};
    for (int g = 0; g < 3; ++g) {
        lv_obj_t *gline = lv_obj_create(s_p1_chart_card);
        lv_obj_remove_style_all(gline);
        lv_obj_set_size(gline, 288, 1);
        lv_obj_set_pos(gline, 14, grid_ys[g]);
        lv_obj_set_style_bg_color(gline, lv_color_hex(0x14241B), 0);
        lv_obj_set_style_bg_opa(gline, LV_OPA_COVER, 0);
        lv_obj_remove_flag(gline, LV_OBJ_FLAG_CLICKABLE);
    }

    lv_obj_t *chart_title = lv_label_create(s_p1_chart_card);
    lv_label_set_text(chart_title, "TREND 1H");
    lv_obj_set_style_text_font(chart_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(chart_title, COLOR_LABEL_DIM, 0);
    lv_obj_set_style_text_letter_space(chart_title, 2, 0);
    lv_obj_set_pos(chart_title, 14, 5);

    s_p1_chart_range_label = lv_label_create(s_p1_chart_card);
    lv_label_set_text(s_p1_chart_range_label, "WAITING...");
    lv_obj_set_style_text_font(s_p1_chart_range_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_p1_chart_range_label, COLOR_TITLE_MUTED, 0);
    lv_obj_align(s_p1_chart_range_label, LV_ALIGN_TOP_RIGHT, -14, 5);

    s_p1_polyline = lv_line_create(s_p1_chart_card);
    lv_obj_set_size(s_p1_polyline, 316, 106);
    lv_obj_set_pos(s_p1_polyline, 0, 0);
    lv_obj_set_style_line_width(s_p1_polyline, 3, 0);
    lv_obj_set_style_line_color(s_p1_polyline, COLOR_ARC_TRACK, 0);
    lv_obj_set_style_line_rounded(s_p1_polyline, true, 0);
    lv_obj_remove_flag(s_p1_polyline, LV_OBJ_FLAG_CLICKABLE);

    s_p1_chart_dot = lv_obj_create(s_p1_chart_card);
    lv_obj_remove_style_all(s_p1_chart_dot);
    lv_obj_set_size(s_p1_chart_dot, 8, 8);
    lv_obj_set_style_radius(s_p1_chart_dot, 4, 0);
    lv_obj_set_style_bg_color(s_p1_chart_dot, COLOR_TEXT_WHITE, 0);
    lv_obj_set_style_bg_opa(s_p1_chart_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_p1_chart_dot, 2, 0);
    lv_obj_set_style_border_color(s_p1_chart_dot, COLOR_SIGNAL_GREEN, 0);
    lv_obj_add_flag(s_p1_chart_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_p1_chart_dot, LV_OBJ_FLAG_CLICKABLE);

    redraw_altitude_polyline();

    /* ==================== 3. BOTTOM SECTION: MIN (LEFT) & MAX (RIGHT) ==================== */
    lv_obj_t *v_div = lv_obj_create(s_p1_content_group);
    lv_obj_remove_style_all(v_div);
    lv_obj_set_size(v_div, 1, 52);
    lv_obj_align(v_div, LV_ALIGN_CENTER, 0, 106);
    lv_obj_set_style_bg_color(v_div, lv_color_hex(0x1B3024), 0);
    lv_obj_set_style_bg_opa(v_div, LV_OPA_COVER, 0);
    lv_obj_remove_flag(v_div, LV_OBJ_FLAG_CLICKABLE);

    /* Left: Minimum Altitude Since Power-On */
    lv_obj_t *min_hdr = lv_label_create(s_p1_content_group);
    lv_label_set_text(min_hdr, "MIN ALT");
    lv_obj_set_style_text_font(min_hdr, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(min_hdr, COLOR_TITLE_MUTED, 0);
    lv_obj_set_style_text_letter_space(min_hdr, 2, 0);
    lv_obj_align(min_hdr, LV_ALIGN_CENTER, -70, 82);

    s_p1_min_label = lv_label_create(s_p1_content_group);
    lv_label_set_text(s_p1_min_label, "---");
    lv_obj_set_style_text_font(s_p1_min_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_p1_min_label, COLOR_LABEL_DIM, 0);
    lv_obj_align(s_p1_min_label, LV_ALIGN_CENTER, -70, 114);

    /* Right: Maximum Altitude Since Power-On */
    lv_obj_t *max_hdr = lv_label_create(s_p1_content_group);
    lv_label_set_text(max_hdr, "MAX ALT");
    lv_obj_set_style_text_font(max_hdr, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(max_hdr, COLOR_TITLE_MUTED, 0);
    lv_obj_set_style_text_letter_space(max_hdr, 2, 0);
    lv_obj_align(max_hdr, LV_ALIGN_CENTER, 70, 82);

    s_p1_max_label = lv_label_create(s_p1_content_group);
    lv_label_set_text(s_p1_max_label, "---");
    lv_obj_set_style_text_font(s_p1_max_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_p1_max_label, COLOR_LABEL_DIM, 0);
    lv_obj_align(s_p1_max_label, LV_ALIGN_CENTER, 70, 114);
}

static void redraw_speed_polyline(void)
{
    if (!s_p2_polyline || !s_p2_chart_dot || !s_p2_chart_range_label) {
        return;
    }

    const float plot_x0 = 14.0f;
    const float plot_w  = 288.0f;
    const float plot_y0 = 24.0f;
    const float plot_h  = 66.0f;

    if (s_alt_fifo_count <= 0) {
        s_p2_line_points[0].x = plot_x0;
        s_p2_line_points[0].y = plot_y0 + plot_h * 0.5f;
        s_p2_line_points[1].x = plot_x0 + plot_w;
        s_p2_line_points[1].y = plot_y0 + plot_h * 0.5f;
        lv_line_set_points(s_p2_polyline, s_p2_line_points, 2);
        lv_obj_set_style_line_color(s_p2_polyline, COLOR_ARC_TRACK, 0);
        lv_obj_add_flag(s_p2_chart_dot, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_p2_chart_range_label, "WAITING...");
        return;
    }

    /* Find min/max across the 1-hour FIFO window */
    float first_v = s_spd_fifo_val[s_alt_fifo_head];
    float fifo_min = first_v;
    float fifo_max = first_v;
    for (int i = 1; i < s_alt_fifo_count; ++i) {
        float v = s_spd_fifo_val[(s_alt_fifo_head + i) % ALT_FIFO_CAP];
        if (v < fifo_min) fifo_min = v;
        if (v > fifo_max) fifo_max = v;
    }

    /* Update range header inside chart with 1-hour FIFO min ~ max */
    char rbuf[32];
    snprintf(rbuf, sizeof(rbuf), "%ld ~ %ld km/h",
             (long)lroundf(fifo_min), (long)lroundf(fifo_max));
    lv_label_set_text(s_p2_chart_range_label, rbuf);

    /* Enforce minimum 10 km/h vertical span (non-negative) to prevent static jitter amplification */
    float min_v = fifo_min;
    float max_v = fifo_max;
    float span = max_v - min_v;
    if (span < 10.0f) {
        float mid = 0.5f * (min_v + max_v);
        min_v = mid - 5.0f;
        max_v = mid + 5.0f;
        if (min_v < 0.0f) {
            min_v = 0.0f;
            max_v = 10.0f;
        }
        span = 10.0f;
    } else {
        float pad = span * 0.12f;
        min_v -= pad;
        if (min_v < 0.0f) {
            min_v = 0.0f;
        }
        max_v += pad;
        span = max_v - min_v;
    }

    if (s_alt_fifo_count == 1) {
        float norm = (first_v - min_v) / span;
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        float y = plot_y0 + (1.0f - norm) * plot_h;
        s_p2_line_points[0].x = plot_x0;
        s_p2_line_points[0].y = y;
        s_p2_line_points[1].x = plot_x0 + plot_w;
        s_p2_line_points[1].y = y;
        lv_line_set_points(s_p2_polyline, s_p2_line_points, 2);
        lv_obj_set_style_line_color(s_p2_polyline, COLOR_SIGNAL_GREEN, 0);
        lv_obj_remove_flag(s_p2_chart_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_p2_chart_dot, (int32_t)(plot_x0 + plot_w) - 4, (int32_t)y - 4);
        return;
    }

    int n = (s_alt_fifo_count <= ALT_LINE_MAX_POINTS) ? s_alt_fifo_count : ALT_LINE_MAX_POINTS;
    for (int i = 0; i < n; ++i) {
        float sample_v;
        if (s_alt_fifo_count <= ALT_LINE_MAX_POINTS) {
            sample_v = s_spd_fifo_val[(s_alt_fifo_head + i) % ALT_FIFO_CAP];
        } else if (i == n - 1) {
            /* Always pin rightmost vertex to the newest FIFO sample */
            sample_v = s_spd_fifo_val[(s_alt_fifo_head + s_alt_fifo_count - 1) % ALT_FIFO_CAP];
        } else {
            int start_idx = (i * s_alt_fifo_count) / n;
            int end_idx   = ((i + 1) * s_alt_fifo_count) / n;
            if (end_idx <= start_idx) {
                end_idx = start_idx + 1;
            }
            float sum = 0.0f;
            for (int k = start_idx; k < end_idx; ++k) {
                sum += s_spd_fifo_val[(s_alt_fifo_head + k) % ALT_FIFO_CAP];
            }
            sample_v = sum / (float)(end_idx - start_idx);
        }

        float t = (float)i / (float)(n - 1);
        float norm = (sample_v - min_v) / span;
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        s_p2_line_points[i].x = plot_x0 + t * plot_w;
        s_p2_line_points[i].y = plot_y0 + (1.0f - norm) * plot_h;
    }

    lv_line_set_points(s_p2_polyline, s_p2_line_points, n);
    lv_obj_set_style_line_color(s_p2_polyline, COLOR_SIGNAL_GREEN, 0);
    lv_obj_remove_flag(s_p2_chart_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_p2_chart_dot,
                   (int32_t)s_p2_line_points[n - 1].x - 4,
                   (int32_t)s_p2_line_points[n - 1].y - 4);
}

static void build_page2_speed(lv_obj_t *safe_cont)
{
    /*
     * Page 2: 3-Section Speed Dashboard inside the 392x392 safe circular container:
     *   - Top Section    (dy = -168 .. -54): Current Speed ("SPEED" + value + "km/h")
     *   - Middle Section (dy =  -46 .. +60): 1-Hour Speed Polyline Chart (316x106 card)
     *   - Bottom Section (dy =  +68 .. +165): Centered Max Speed Since Power-On ("MAX SPEED")
     */
    s_p2_content_group = lv_obj_create(safe_cont);
    lv_obj_remove_style_all(s_p2_content_group);
    lv_obj_set_size(s_p2_content_group, UI_SAFE_DIAMETER_PX, UI_SAFE_DIAMETER_PX);
    lv_obj_center(s_p2_content_group);
    lv_obj_remove_flag(s_p2_content_group, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* ==================== 1. TOP SECTION: CURRENT SPEED ==================== */
    s_p2_title_label = lv_label_create(s_p2_content_group);
    lv_label_set_text(s_p2_title_label, "SPEED");
    lv_obj_set_style_text_font(s_p2_title_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_p2_title_label, COLOR_TITLE_MUTED, 0);
    lv_obj_set_style_text_letter_space(s_p2_title_label, 3, 0);
    lv_obj_align(s_p2_title_label, LV_ALIGN_CENTER, 0, -146);

    s_p2_value_label = lv_label_create(s_p2_content_group);
    lv_label_set_text(s_p2_value_label, "---");
    lv_obj_set_style_text_font(s_p2_value_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_p2_value_label, COLOR_LABEL_DIM, 0);
    lv_obj_set_style_text_letter_space(s_p2_value_label, 1, 0);
    lv_obj_align(s_p2_value_label, LV_ALIGN_CENTER, 0, -104);

    s_p2_unit_label = lv_label_create(s_p2_content_group);
    lv_label_set_text(s_p2_unit_label, "SEARCHING");
    lv_obj_set_style_text_font(s_p2_unit_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_p2_unit_label, COLOR_LABEL_DIM, 0);
    lv_obj_set_style_text_letter_space(s_p2_unit_label, 2, 0);
    lv_obj_align(s_p2_unit_label, LV_ALIGN_CENTER, 0, -66);

    /* ==================== 2. MIDDLE SECTION: POLYLINE CHART ==================== */
    s_p2_chart_card = lv_obj_create(s_p2_content_group);
    lv_obj_remove_style_all(s_p2_chart_card);
    lv_obj_set_size(s_p2_chart_card, 316, 106);
    lv_obj_align(s_p2_chart_card, LV_ALIGN_CENTER, 0, 4);
    lv_obj_set_style_bg_color(s_p2_chart_card, lv_color_hex(0x0A130E), 0);
    lv_obj_set_style_bg_opa(s_p2_chart_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_p2_chart_card, 1, 0);
    lv_obj_set_style_border_color(s_p2_chart_card, lv_color_hex(0x1B3024), 0);
    lv_obj_set_style_radius(s_p2_chart_card, 12, 0);
    lv_obj_remove_flag(s_p2_chart_card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* 3 subtle horizontal reference grid lines inside chart card */
    const int32_t grid_ys[3] = {24, 57, 90};
    for (int g = 0; g < 3; ++g) {
        lv_obj_t *gline = lv_obj_create(s_p2_chart_card);
        lv_obj_remove_style_all(gline);
        lv_obj_set_size(gline, 288, 1);
        lv_obj_set_pos(gline, 14, grid_ys[g]);
        lv_obj_set_style_bg_color(gline, lv_color_hex(0x14241B), 0);
        lv_obj_set_style_bg_opa(gline, LV_OPA_COVER, 0);
        lv_obj_remove_flag(gline, LV_OBJ_FLAG_CLICKABLE);
    }

    lv_obj_t *chart_title = lv_label_create(s_p2_chart_card);
    lv_label_set_text(chart_title, "TREND 1H");
    lv_obj_set_style_text_font(chart_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(chart_title, COLOR_LABEL_DIM, 0);
    lv_obj_set_style_text_letter_space(chart_title, 2, 0);
    lv_obj_set_pos(chart_title, 14, 5);

    s_p2_chart_range_label = lv_label_create(s_p2_chart_card);
    lv_label_set_text(s_p2_chart_range_label, "WAITING...");
    lv_obj_set_style_text_font(s_p2_chart_range_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_p2_chart_range_label, COLOR_TITLE_MUTED, 0);
    lv_obj_align(s_p2_chart_range_label, LV_ALIGN_TOP_RIGHT, -14, 5);

    s_p2_polyline = lv_line_create(s_p2_chart_card);
    lv_obj_set_size(s_p2_polyline, 316, 106);
    lv_obj_set_pos(s_p2_polyline, 0, 0);
    lv_obj_set_style_line_width(s_p2_polyline, 3, 0);
    lv_obj_set_style_line_color(s_p2_polyline, COLOR_ARC_TRACK, 0);
    lv_obj_set_style_line_rounded(s_p2_polyline, true, 0);
    lv_obj_remove_flag(s_p2_polyline, LV_OBJ_FLAG_CLICKABLE);

    s_p2_chart_dot = lv_obj_create(s_p2_chart_card);
    lv_obj_remove_style_all(s_p2_chart_dot);
    lv_obj_set_size(s_p2_chart_dot, 8, 8);
    lv_obj_set_style_radius(s_p2_chart_dot, 4, 0);
    lv_obj_set_style_bg_color(s_p2_chart_dot, COLOR_TEXT_WHITE, 0);
    lv_obj_set_style_bg_opa(s_p2_chart_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_p2_chart_dot, 2, 0);
    lv_obj_set_style_border_color(s_p2_chart_dot, COLOR_SIGNAL_GREEN, 0);
    lv_obj_add_flag(s_p2_chart_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_p2_chart_dot, LV_OBJ_FLAG_CLICKABLE);

    redraw_speed_polyline();

    /* ==================== 3. BOTTOM SECTION: MAX SPEED SINCE POWER-ON ==================== */
    lv_obj_t *max_hdr = lv_label_create(s_p2_content_group);
    lv_label_set_text(max_hdr, "MAX SPEED");
    lv_obj_set_style_text_font(max_hdr, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(max_hdr, COLOR_TITLE_MUTED, 0);
    lv_obj_set_style_text_letter_space(max_hdr, 2, 0);
    lv_obj_align(max_hdr, LV_ALIGN_CENTER, 0, 82);

    s_p2_max_label = lv_label_create(s_p2_content_group);
    lv_label_set_text(s_p2_max_label, "---");
    lv_obj_set_style_text_font(s_p2_max_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_p2_max_label, COLOR_LABEL_DIM, 0);
    lv_obj_align(s_p2_max_label, LV_ALIGN_CENTER, 0, 114);
}

static int hit_test_p3_tool_tile(const lv_point_t *pt)
{
    if (!pt || !s_p3_content_group) {
        return -1;
    }
    for (int i = 0; i < P3_TOOL_COUNT; ++i) {
        if (!s_p3_tile_cards[i]) {
            continue;
        }
        lv_area_t coords;
        lv_obj_get_coords(s_p3_tile_cards[i], &coords);
        if (pt->x >= coords.x1 && pt->x <= coords.x2 &&
            pt->y >= coords.y1 && pt->y <= coords.y2) {
            return i;
        }
    }
    return -1;
}

static bool hit_test_p3_qr_close(const lv_point_t *pt)
{
    if (!pt || !s_p3_qr_modal_open || !s_p3_qr_close_btn) {
        return false;
    }
    lv_area_t coords;
    lv_obj_get_coords(s_p3_qr_close_btn, &coords);
    /* Expand vertical touch target slightly (+12px) for easy thumb tap */
    if (pt->x >= (coords.x1 - 12) && pt->x <= (coords.x2 + 12) &&
        pt->y >= (coords.y1 - 12) && pt->y <= (coords.y2 + 16)) {
        return true;
    }
    return false;
}

static void update_p3_qr_close_visual(bool is_pressed)
{
    if (!s_p3_qr_close_btn) {
        return;
    }
    if (is_pressed) {
        lv_obj_set_style_bg_color(s_p3_qr_close_btn, lv_color_hex(0x1E3D2B), 0);
        lv_obj_set_style_border_width(s_p3_qr_close_btn, 2, 0);
    } else {
        lv_obj_set_style_bg_color(s_p3_qr_close_btn, lv_color_hex(0x12241A), 0);
        lv_obj_set_style_border_width(s_p3_qr_close_btn, 1, 0);
    }
}

static void update_p3_tile_visual(int idx, bool is_pressed)
{
    if (idx < 0 || idx >= P3_TOOL_COUNT || !s_p3_tile_cards[idx]) {
        return;
    }

    if (idx == 0) {
        /* Tool 01: TF Format special state machine styling */
        lv_color_t bg_col = lv_color_hex(0x0A130E);
        lv_color_t border_col = lv_color_hex(0x1B3024);
        int32_t border_w = 1;
        lv_color_t dot_col = COLOR_SIGNAL_GREEN;
        const char *sub_txt = "READY";
        lv_color_t sub_col = COLOR_LABEL_DIM;

        switch (s_tf_status) {
        case UI_TF_STATUS_FORMATTING:
            bg_col = lv_color_hex(0x1E1908);
            border_col = lv_color_hex(0xFFB300);
            border_w = 2;
            dot_col = lv_color_hex(0xFFB300);
            sub_txt = "FORMAT...";
            sub_col = lv_color_hex(0xFFB300);
            break;
        case UI_TF_STATUS_DONE:
            bg_col = lv_color_hex(0x0E1D15);
            border_col = COLOR_SIGNAL_GREEN;
            border_w = 1;
            dot_col = COLOR_SIGNAL_GREEN;
            sub_txt = "DONE";
            sub_col = COLOR_SIGNAL_GREEN;
            break;
        case UI_TF_STATUS_FAILED:
            bg_col = lv_color_hex(0x220D0D);
            border_col = lv_color_hex(0xFF5252);
            border_w = 1;
            dot_col = lv_color_hex(0xFF5252);
            sub_txt = "FAILED";
            sub_col = lv_color_hex(0xFF5252);
            break;
        case UI_TF_STATUS_NO_CARD:
            bg_col = lv_color_hex(0x0A130E);
            border_col = lv_color_hex(0x1B3024);
            border_w = 1;
            dot_col = lv_color_hex(0xFF5252);
            sub_txt = "NO CARD";
            sub_col = lv_color_hex(0xFF5252);
            break;
        case UI_TF_STATUS_READY:
        default:
            bg_col = lv_color_hex(0x0A130E);
            border_col = lv_color_hex(0x1B3024);
            border_w = 1;
            dot_col = COLOR_SIGNAL_GREEN;
            sub_txt = "READY";
            sub_col = COLOR_LABEL_DIM;
            break;
        }

        if (is_pressed && s_tf_status != UI_TF_STATUS_FORMATTING) {
            bg_col = lv_color_hex(0x14281D);
            border_col = COLOR_SIGNAL_GREEN;
            border_w = 2;
        }

        lv_obj_set_style_bg_color(s_p3_tile_cards[0], bg_col, 0);
        lv_obj_set_style_border_color(s_p3_tile_cards[0], border_col, 0);
        lv_obj_set_style_border_width(s_p3_tile_cards[0], border_w, 0);
        if (s_p3_tile_badges[0]) {
            lv_obj_set_style_bg_color(s_p3_tile_badges[0], dot_col, 0);
        }
        if (s_p3_tile_subtitles[0]) {
            lv_label_set_text(s_p3_tile_subtitles[0], sub_txt);
            lv_obj_set_style_text_color(s_p3_tile_subtitles[0], sub_col, 0);
        }
        return;
    }

    if (idx == 1) {
        /* Tool 02: Wi-Fi QR Config card styling */
        lv_color_t bg_col = s_p3_wifi_online ? lv_color_hex(0x0E1D15) : lv_color_hex(0x0A130E);
        lv_color_t border_col = s_p3_wifi_online ? COLOR_SIGNAL_GREEN : lv_color_hex(0x1B3024);
        int32_t border_w = 1;
        if (is_pressed) {
            bg_col = lv_color_hex(0x14281D);
            border_col = COLOR_SIGNAL_GREEN;
            border_w = 2;
        }
        lv_obj_set_style_bg_color(s_p3_tile_cards[1], bg_col, 0);
        lv_obj_set_style_border_color(s_p3_tile_cards[1], border_col, 0);
        lv_obj_set_style_border_width(s_p3_tile_cards[1], border_w, 0);
        if (s_p3_tile_badges[1]) {
            lv_obj_set_style_bg_color(s_p3_tile_badges[1],
                                      (s_p3_wifi_online || is_pressed) ? COLOR_SIGNAL_GREEN : lv_color_hex(0x14241B), 0);
        }
        if (s_p3_tile_subtitles[1]) {
            lv_label_set_text(s_p3_tile_subtitles[1], s_p3_wifi_online ? "ONLINE" : "QR SETUP");
            lv_obj_set_style_text_color(s_p3_tile_subtitles[1],
                                        s_p3_wifi_online ? COLOR_SIGNAL_GREEN : COLOR_LABEL_DIM, 0);
        }
        return;
    }

    if (idx == 3) {
        /* Tool 04: 1Hz GNSS Trajectory Logger (W/G .gpx) */
        lv_color_t border_col = lv_color_hex(0x1B3024);
        lv_color_t bg_col = lv_color_hex(0x0A130E);
        lv_color_t dot_col = lv_color_hex(0x14241B);
        lv_color_t sub_col = COLOR_LABEL_DIM;
        char sub_buf[24] = "READY";
        int32_t lspace = 2;

        if (is_pressed) {
            bg_col = lv_color_hex(0x14281D);
            border_col = COLOR_SIGNAL_GREEN;
            dot_col = COLOR_SIGNAL_GREEN;
        } else if (s_p3_tile_active[3]) {
            if (s_track_log_status == UI_TRACK_LOG_WAIT_FIX) {
                bg_col = lv_color_hex(0x191306);
                border_col = lv_color_hex(0xFFB300);
                dot_col = lv_color_hex(0xFFB300);
                sub_col = lv_color_hex(0xFFB300);
                snprintf(sub_buf, sizeof(sub_buf), "WAIT FIX");
                lspace = 1;
            } else if (s_track_log_status == UI_TRACK_LOG_NO_CARD) {
                bg_col = lv_color_hex(0x1E0D0D);
                border_col = lv_color_hex(0xFF5252);
                dot_col = lv_color_hex(0xFF5252);
                sub_col = lv_color_hex(0xFF5252);
                snprintf(sub_buf, sizeof(sub_buf), "NO CARD");
                lspace = 1;
            } else {
                bg_col = lv_color_hex(0x0E1D15);
                border_col = COLOR_SIGNAL_GREEN;
                dot_col = COLOR_SIGNAL_GREEN;
                sub_col = COLOR_SIGNAL_GREEN;
                uint32_t hrs = s_track_log_elapsed_sec / 3600U;
                uint32_t mins = (s_track_log_elapsed_sec % 3600U) / 60U;
                uint32_t secs = s_track_log_elapsed_sec % 60U;
                if (hrs > 0) {
                    snprintf(sub_buf, sizeof(sub_buf), "REC %02lu:%02lu",
                             (unsigned long)hrs, (unsigned long)mins);
                } else {
                    snprintf(sub_buf, sizeof(sub_buf), "REC %02lu:%02lu",
                             (unsigned long)mins, (unsigned long)secs);
                }
                lspace = 1;
            }
        }

        lv_obj_set_style_bg_color(s_p3_tile_cards[3], bg_col, 0);
        lv_obj_set_style_border_color(s_p3_tile_cards[3], border_col, 0);
        lv_obj_set_style_border_width(s_p3_tile_cards[3], is_pressed ? 2 : 1, 0);

        if (s_p3_tile_badges[3]) {
            lv_obj_set_style_bg_color(s_p3_tile_badges[3], dot_col, 0);
        }
        if (s_p3_tile_subtitles[3]) {
            lv_label_set_text(s_p3_tile_subtitles[3], sub_buf);
            lv_obj_set_style_text_color(s_p3_tile_subtitles[3], sub_col, 0);
            lv_obj_set_style_text_letter_space(s_p3_tile_subtitles[3], lspace, 0);
        }
        return;
    }

    bool active = s_p3_tile_active[idx];
    if (is_pressed) {
        lv_obj_set_style_bg_color(s_p3_tile_cards[idx], lv_color_hex(0x14281D), 0);
        lv_obj_set_style_border_color(s_p3_tile_cards[idx], COLOR_SIGNAL_GREEN, 0);
        lv_obj_set_style_border_width(s_p3_tile_cards[idx], 2, 0);
    } else if (active) {
        lv_obj_set_style_bg_color(s_p3_tile_cards[idx], lv_color_hex(0x0E1D15), 0);
        lv_obj_set_style_border_color(s_p3_tile_cards[idx], COLOR_SIGNAL_GREEN, 0);
        lv_obj_set_style_border_width(s_p3_tile_cards[idx], 1, 0);
    } else {
        lv_obj_set_style_bg_color(s_p3_tile_cards[idx], lv_color_hex(0x0A130E), 0);
        lv_obj_set_style_border_color(s_p3_tile_cards[idx], lv_color_hex(0x1B3024), 0);
        lv_obj_set_style_border_width(s_p3_tile_cards[idx], 1, 0);
    }

    if (s_p3_tile_badges[idx]) {
        lv_obj_set_style_bg_color(s_p3_tile_badges[idx],
                                  (active || is_pressed) ? COLOR_SIGNAL_GREEN : lv_color_hex(0x14241B), 0);
    }
    if (s_p3_tile_subtitles[idx]) {
        if (idx == 2) {
            lv_label_set_text(s_p3_tile_subtitles[2], active ? "192.168.4.1" : "READY");
            lv_obj_set_style_text_letter_space(s_p3_tile_subtitles[2], active ? 1 : 2, 0);
        } else {
            lv_label_set_text(s_p3_tile_subtitles[idx], active ? "ACTIVE" : "READY");
        }
        lv_obj_set_style_text_color(s_p3_tile_subtitles[idx],
                                    active ? COLOR_SIGNAL_GREEN : COLOR_LABEL_DIM, 0);
    }
}

static void build_page3_tools(lv_obj_t *safe_cont)
{
    /*
     * Page 3: 2x2 Four-Grid Tools Page inside the 392x392 safe circular container:
     *   - Top Header (dy = -146): "TOOLS" matching Page 1 & Page 2 title position
     *   - 2x2 Grid of 4 rounded tool cards (126 x 112 px each, 12px gap):
     *       Tile 0 (Top-Left):     dx = -69, dy = -56  [Tool 01: TF FORMAT]
     *       Tile 1 (Top-Right):    dx = +69, dy = -56  [Tool 02: WIFI CFG]
     *       Tile 2 (Bottom-Left):  dx = -69, dy = +68  [Tool 03: FILE MGR]
     *       Tile 3 (Bottom-Right): dx = +69, dy = +68  [Tool 04: TRACK LOG]
     *     Outer corners reach sqrt(132^2 + 124^2) = 181.1 px < R_safe (196 px).
     */
    s_p3_content_group = lv_obj_create(safe_cont);
    lv_obj_remove_style_all(s_p3_content_group);
    lv_obj_set_size(s_p3_content_group, UI_SAFE_DIAMETER_PX, UI_SAFE_DIAMETER_PX);
    lv_obj_center(s_p3_content_group);
    lv_obj_remove_flag(s_p3_content_group, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* Top Title: "TOOLS" */
    s_p3_title_label = lv_label_create(s_p3_content_group);
    lv_label_set_text(s_p3_title_label, "TOOLS");
    lv_obj_set_style_text_font(s_p3_title_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_p3_title_label, COLOR_TITLE_MUTED, 0);
    lv_obj_set_style_text_letter_space(s_p3_title_label, 3, 0);
    lv_obj_align(s_p3_title_label, LV_ALIGN_CENTER, 0, -146);

    static const int32_t tile_dx[P3_TOOL_COUNT] = {-69, 69, -69, 69};
    static const int32_t tile_dy[P3_TOOL_COUNT] = {-56, -56, 68, 68};
    static const char *tile_num_str[P3_TOOL_COUNT] = {"01", "02", "03", "04"};
    static const char *tile_title_str[P3_TOOL_COUNT] = {"TF FORMAT", "WIFI CFG", "FILE MGR", "TRACK LOG"};

    for (int i = 0; i < P3_TOOL_COUNT; ++i) {
        lv_obj_t *card = lv_obj_create(s_p3_content_group);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, 126, 112);
        lv_obj_align(card, LV_ALIGN_CENTER, tile_dx[i], tile_dy[i]);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x0A130E), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0x1B3024), 0);
        lv_obj_set_style_radius(card, 16, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        s_p3_tile_cards[i] = card;

        /* Top-left slot number pill */
        lv_obj_t *pill = lv_obj_create(card);
        lv_obj_remove_style_all(pill);
        lv_obj_set_size(pill, 32, 20);
        lv_obj_align(pill, LV_ALIGN_TOP_LEFT, 12, 12);
        lv_obj_set_style_bg_color(pill, lv_color_hex(0x12241A), 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(pill, 6, 0);
        lv_obj_remove_flag(pill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *num_lbl = lv_label_create(pill);
        lv_label_set_text(num_lbl, tile_num_str[i]);
        lv_obj_set_style_text_font(num_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(num_lbl, COLOR_SIGNAL_GREEN, 0);
        lv_obj_center(num_lbl);

        /* Top-right status indicator dot */
        lv_obj_t *dot = lv_obj_create(card);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, 4, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0x14241B), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, -14, 18);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        s_p3_tile_badges[i] = dot;

        /* Main tool title */
        lv_obj_t *title_lbl = lv_label_create(card);
        lv_label_set_text(title_lbl, tile_title_str[i]);
        lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(title_lbl, COLOR_TEXT_WHITE, 0);
        lv_obj_set_style_text_letter_space(title_lbl, 0, 0);
        lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 12, 50);
        s_p3_tile_titles[i] = title_lbl;

        /* Bottom status subtitle */
        lv_obj_t *sub_lbl = lv_label_create(card);
        lv_label_set_text(sub_lbl, "READY");
        lv_obj_set_style_text_font(sub_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(sub_lbl, COLOR_LABEL_DIM, 0);
        lv_obj_set_style_text_letter_space(sub_lbl, 2, 0);
        lv_obj_align(sub_lbl, LV_ALIGN_BOTTOM_LEFT, 12, -12);
        s_p3_tile_subtitles[i] = sub_lbl;
    }

    update_p3_tile_visual(0, false);
    update_p3_tile_visual(1, false);

    /* ==================== Wi-Fi QR Code Configuration Modal (initially hidden) ==================== */
    s_p3_qr_modal = lv_obj_create(safe_cont);
    lv_obj_remove_style_all(s_p3_qr_modal);
    lv_obj_set_size(s_p3_qr_modal, UI_SAFE_DIAMETER_PX, UI_SAFE_DIAMETER_PX);
    lv_obj_center(s_p3_qr_modal);
    lv_obj_remove_flag(s_p3_qr_modal, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_p3_qr_modal, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *qr_hdr = lv_label_create(s_p3_qr_modal);
    lv_label_set_text(qr_hdr, "WIFI CONFIG");
    lv_obj_set_style_text_font(qr_hdr, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(qr_hdr, COLOR_TITLE_MUTED, 0);
    lv_obj_set_style_text_letter_space(qr_hdr, 2, 0);
    lv_obj_align(qr_hdr, LV_ALIGN_CENTER, 0, -148);

    s_p3_qr_step_label = lv_label_create(s_p3_qr_modal);
    lv_label_set_text(s_p3_qr_step_label, "1. SCAN TO JOIN AP");
    lv_obj_set_style_text_font(s_p3_qr_step_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_p3_qr_step_label, COLOR_SIGNAL_GREEN, 0);
    lv_obj_set_style_text_letter_space(s_p3_qr_step_label, 1, 0);
    lv_obj_align(s_p3_qr_step_label, LV_ALIGN_CENTER, 0, -124);

    /* White rounded card backing the QR code for crisp camera contrast */
    lv_obj_t *qr_card = lv_obj_create(s_p3_qr_modal);
    lv_obj_remove_style_all(qr_card);
    lv_obj_set_size(qr_card, 180, 180);
    lv_obj_align(qr_card, LV_ALIGN_CENTER, 0, -14);
    lv_obj_set_style_bg_color(qr_card, COLOR_TEXT_WHITE, 0);
    lv_obj_set_style_bg_opa(qr_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(qr_card, 14, 0);
    lv_obj_remove_flag(qr_card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_p3_qrcode = lv_qrcode_create(qr_card);
    lv_qrcode_set_size(s_p3_qrcode, 156);
    lv_qrcode_set_dark_color(s_p3_qrcode, COLOR_BG_BLACK);
    lv_qrcode_set_light_color(s_p3_qrcode, COLOR_TEXT_WHITE);
    lv_qrcode_update(s_p3_qrcode, WIFI_AP_QR_PAYLOAD, strlen(WIFI_AP_QR_PAYLOAD));
    lv_obj_center(s_p3_qrcode);
    lv_obj_remove_flag(s_p3_qrcode, LV_OBJ_FLAG_CLICKABLE);

    s_p3_qr_hint_label = lv_label_create(s_p3_qr_modal);
    lv_label_set_text(s_p3_qr_hint_label, "AP: ESP32-WIFI-CFG");
    lv_obj_set_style_text_font(s_p3_qr_hint_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_p3_qr_hint_label, COLOR_TEXT_WHITE, 0);
    lv_obj_set_style_text_letter_space(s_p3_qr_hint_label, 1, 0);
    lv_obj_align(s_p3_qr_hint_label, LV_ALIGN_CENTER, 0, 94);

    s_p3_qr_close_btn = lv_obj_create(s_p3_qr_modal);
    lv_obj_remove_style_all(s_p3_qr_close_btn);
    lv_obj_set_size(s_p3_qr_close_btn, 116, 32);
    lv_obj_align(s_p3_qr_close_btn, LV_ALIGN_CENTER, 0, 132);
    lv_obj_set_style_bg_color(s_p3_qr_close_btn, lv_color_hex(0x12241A), 0);
    lv_obj_set_style_bg_opa(s_p3_qr_close_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_p3_qr_close_btn, 1, 0);
    lv_obj_set_style_border_color(s_p3_qr_close_btn, COLOR_SIGNAL_GREEN, 0);
    lv_obj_set_style_radius(s_p3_qr_close_btn, 16, 0);
    lv_obj_remove_flag(s_p3_qr_close_btn, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *close_lbl = lv_label_create(s_p3_qr_close_btn);
    lv_label_set_text(close_lbl, "CLOSE");
    lv_obj_set_style_text_font(close_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(close_lbl, COLOR_SIGNAL_GREEN, 0);
    lv_obj_set_style_text_letter_space(close_lbl, 2, 0);
    lv_obj_center(close_lbl);
}

void ui_framework_init(void)
{
    s_root_screen = lv_screen_active();
    lv_obj_set_style_bg_color(s_root_screen, COLOR_BG_BLACK, 0);
    lv_obj_set_style_bg_opa(s_root_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_root_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root_screen, LV_OBJ_FLAG_CLICKABLE);

    /* Create page wrappers and 392x392 safe-area inner circular containers */
    for (int i = 0; i < UI_PAGE_COUNT; ++i) {
        s_page_wrappers[i] = lv_obj_create(s_root_screen);
        lv_obj_remove_style_all(s_page_wrappers[i]);
        lv_obj_set_size(s_page_wrappers[i], UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT);
        lv_obj_set_pos(s_page_wrappers[i], (i == 0) ? 0 : UI_SCREEN_WIDTH, 0);
        lv_obj_remove_flag(s_page_wrappers[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        /* Hide all formal pages initially while power-on GNSS Initialization Page is active */
        lv_obj_add_flag(s_page_wrappers[i], LV_OBJ_FLAG_HIDDEN);

        /* Strict 392x392 px safe area container (inset 37px from all edges) */
        s_page_safe_conts[i] = lv_obj_create(s_page_wrappers[i]);
        lv_obj_remove_style_all(s_page_safe_conts[i]);
        lv_obj_set_size(s_page_safe_conts[i], UI_SAFE_DIAMETER_PX, UI_SAFE_DIAMETER_PX);
        lv_obj_center(s_page_safe_conts[i]);
        lv_obj_remove_flag(s_page_safe_conts[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    /* Populate Page 1 (Altitude), Page 2 (Speed), and Page 3 (Tools 2x2 Grid) */
    build_page1_altitude(s_page_safe_conts[0]);
    build_page2_speed(s_page_safe_conts[1]);
    build_page3_tools(s_page_safe_conts[2]);

    /* Build Power-On GNSS Satellite Search Initialization Page */
    build_init_boot_page();

    /* Build stationary top-layer signal arcs and transient bottom page dots */
    build_top_layer_signal_arcs();
    build_bottom_page_dots();

    /* Attach full-screen horizontal swipe gesture handler */
    lv_obj_add_event_cb(s_root_screen, screen_touch_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_root_screen, screen_touch_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_root_screen, screen_touch_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_root_screen, screen_touch_event_cb, LV_EVENT_PRESS_LOST, NULL);

    ESP_LOGI(TAG, "Multi-page UI framework initialized (Safe Area: %dx%d px)",
             UI_SAFE_DIAMETER_PX, UI_SAFE_DIAMETER_PX);
}

void ui_framework_update(const gps_state_t *gps, int wifi_pct, bool wifi_conn)
{
    /* 1. Update Left GPS Signal Arc (125 deg bottom -> 235 deg top) */
    if (gps && s_gps_arc) {
        s_gps_has_fix = gps->has_fix;
        int pct = gps->signal_percent;
        if (gps->has_fix && pct < 15) {
            pct = 15;
        }
        s_gps_signal_pct = pct;

        if (pct <= 0) {
            lv_arc_set_angles(s_gps_arc, 125, 125);
            lv_obj_set_style_arc_opa(s_gps_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
            lv_obj_set_style_text_color(s_gps_tag_label, COLOR_LABEL_DIM, 0);
        } else {
            int32_t span = (pct * 110) / 100;
            if (span < 3) span = 3;
            lv_arc_set_angles(s_gps_arc, 125, 125 + span);
            lv_obj_set_style_text_color(s_gps_tag_label,
                                        gps->has_fix ? COLOR_SIGNAL_GREEN : COLOR_LABEL_DIM, 0);
        }
    }

    /* 2. Update Right Wi-Fi Signal Arc (55 deg bottom -> 305 deg top, counter-clockwise) */
    if (wifi_conn != s_p3_wifi_online) {
        s_p3_wifi_online = wifi_conn;
        update_p3_tile_visual(1, s_p3_pressed_tile == 1);
    }

    if (s_wifi_arc) {
        int pct = wifi_conn ? wifi_pct : 0;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;

        if (pct <= 0) {
            lv_arc_set_angles(s_wifi_arc, 55, 55);
            lv_obj_set_style_arc_opa(s_wifi_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
            lv_obj_set_style_text_color(s_wifi_tag_label, COLOR_LABEL_DIM, 0);
        } else {
            int32_t span = (pct * 110) / 100;
            if (span < 3) span = 3;
            int32_t start_angle = (55 - span + 360) % 360;
            lv_arc_set_angles(s_wifi_arc, start_angle, 55);
            lv_obj_set_style_arc_opa(s_wifi_arc, LV_OPA_COVER, LV_PART_INDICATOR);
            lv_obj_set_style_text_color(s_wifi_tag_label, COLOR_SIGNAL_GREEN, 0);
        }
    }

    /* 3. Update Top-Center 24-Hour Clock (across all pages) */
    if (gps && s_top_time_label) {
        if (gps->time_valid) {
            if (!s_last_time_valid || gps->local_hour != s_last_time_hh || gps->local_minute != s_last_time_mm) {
                s_last_time_valid = true;
                s_last_time_hh = gps->local_hour;
                s_last_time_mm = gps->local_minute;
                char tbuf[16];
                snprintf(tbuf, sizeof(tbuf), "%02d:%02d", gps->local_hour, gps->local_minute);
                lv_label_set_text(s_top_time_label, tbuf);
                lv_obj_set_style_text_color(s_top_time_label, COLOR_TEXT_WHITE, 0);
            }
        } else if (s_last_time_valid) {
            s_last_time_valid = false;
            s_last_time_hh = -1;
            s_last_time_mm = -1;
            lv_label_set_text(s_top_time_label, "--:--");
            lv_obj_set_style_text_color(s_top_time_label, COLOR_LABEL_DIM, 0);
        }
    }

    /* 3b. Update Power-On GNSS Initialization Page (satellite count & auto-enter upon fix) */
    if (gps && s_init_page_active && s_init_overlay) {
        int sats = (gps->satellites_in_view > gps->satellites_used)
                       ? gps->satellites_in_view
                       : gps->satellites_used;
        if (sats < 0) {
            sats = 0;
        }

        bool sats_changed = (sats != s_init_last_sats);
        if (sats_changed && s_init_sat_num_label) {
            s_init_last_sats = sats;
            char sbuf[16];
            snprintf(sbuf, sizeof(sbuf), "%d", sats);
            lv_label_set_text(s_init_sat_num_label, sbuf);
            lv_obj_align(s_init_sat_num_label, LV_ALIGN_CENTER, 0, -24);
        }

        if (gps->has_fix) {
            if (!s_init_lock_pending) {
                s_init_lock_pending = true;
                s_init_lock_tick = lv_tick_get();
                if (s_init_scan_arc) {
                    lv_arc_set_angles(s_init_scan_arc, 0, 360);
                    lv_obj_set_style_arc_color(s_init_scan_arc, COLOR_SIGNAL_GREEN, LV_PART_INDICATOR);
                }
                if (s_init_sat_num_label) {
                    lv_obj_set_style_text_color(s_init_sat_num_label, COLOR_SIGNAL_GREEN, 0);
                }
                if (s_init_sat_tag_label) {
                    lv_obj_set_style_text_color(s_init_sat_tag_label, COLOR_SIGNAL_GREEN, 0);
                }
                if (s_init_status_label) {
                    lv_label_set_text(s_init_status_label, "POSITION LOCKED");
                    lv_obj_set_style_text_color(s_init_status_label, COLOR_SIGNAL_GREEN, 0);
                    lv_obj_center(s_init_status_label);
                }
            }
        } else if (!s_init_lock_pending) {
            if (sats > 0) {
                if (s_init_sat_num_label) {
                    lv_obj_set_style_text_color(s_init_sat_num_label, COLOR_TEXT_WHITE, 0);
                }
                if (s_init_sat_tag_label) {
                    lv_obj_set_style_text_color(s_init_sat_tag_label, COLOR_SIGNAL_GREEN, 0);
                }
                if (s_init_status_label && (sats_changed || gps->avg_snr_db != s_init_last_snr)) {
                    s_init_last_snr = gps->avg_snr_db;
                    char st_buf[48];
                    snprintf(st_buf, sizeof(st_buf), "SEARCHING  |  SNR %d dB", gps->avg_snr_db);
                    lv_label_set_text(s_init_status_label, st_buf);
                    lv_obj_set_style_text_color(s_init_status_label, lv_color_hex(0xFFB300), 0);
                    lv_obj_center(s_init_status_label);
                }
            } else {
                if (s_init_sat_num_label) {
                    lv_obj_set_style_text_color(s_init_sat_num_label, COLOR_LABEL_DIM, 0);
                }
                if (s_init_sat_tag_label) {
                    lv_obj_set_style_text_color(s_init_sat_tag_label, COLOR_LABEL_DIM, 0);
                }
                if (s_init_status_label && s_init_last_snr != 0) {
                    s_init_last_snr = 0;
                    lv_label_set_text(s_init_status_label, "SEARCHING SKY...");
                    lv_obj_set_style_text_color(s_init_status_label, COLOR_LABEL_DIM, 0);
                    lv_obj_center(s_init_status_label);
                }
            }
        }
    }

    /* 4. Update Page 1: 3-Section Altitude Dashboard */
    if (gps && s_p1_content_group) {
        /* Top Section: Current Altitude */
        if (gps->has_fix) {
            if (gps->altitude_int_m != s_p1_last_alt_m || !s_p1_visible) {
                s_p1_last_alt_m = gps->altitude_int_m;
                char buf[24];
                snprintf(buf, sizeof(buf), "%ld", (long)gps->altitude_int_m);
                lv_label_set_text(s_p1_value_label, buf);
                lv_obj_set_style_text_color(s_p1_value_label, COLOR_TEXT_WHITE, 0);
                lv_obj_align(s_p1_value_label, LV_ALIGN_CENTER, -12, -100);

                lv_label_set_text(s_p1_unit_label, "m");
                lv_obj_set_style_text_font(s_p1_unit_label, &lv_font_montserrat_24, 0);
                lv_obj_set_style_text_color(s_p1_unit_label, COLOR_SIGNAL_GREEN, 0);
                lv_obj_set_style_text_letter_space(s_p1_unit_label, 0, 0);
                lv_obj_align_to(s_p1_unit_label, s_p1_value_label, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -6);
            }
            s_p1_visible = true;

            /* Push new 1Hz fix sample into 1-hour (3600s) FIFO and evict >1h old samples */
            if (gps->fix_epoch_seq != s_p1_last_fix_seq) {
                s_p1_last_fix_seq = gps->fix_epoch_seq;
                uint64_t now_sec = get_monotonic_sec_64();

                /* 1. FIFO pop: evict any samples older than 1 hour (3600s) from head */
                while (s_alt_fifo_count > 0) {
                    uint64_t oldest_ts = s_alt_fifo_ts[s_alt_fifo_head];
                    if (now_sec >= oldest_ts && (now_sec - oldest_ts) > ALT_FIFO_WINDOW_SEC) {
                        s_alt_fifo_head = (s_alt_fifo_head + 1) % ALT_FIFO_CAP;
                        s_alt_fifo_count--;
                    } else {
                        break;
                    }
                }

                /* 2. FIFO push: append newest 1Hz sample to tail */
                if (s_alt_fifo_count == 0 || now_sec != s_alt_last_push_sec) {
                    if (s_alt_fifo_count >= ALT_FIFO_CAP) {
                        s_alt_fifo_head = (s_alt_fifo_head + 1) % ALT_FIFO_CAP;
                        s_alt_fifo_count--;
                    }
                    int tail = (s_alt_fifo_head + s_alt_fifo_count) % ALT_FIFO_CAP;
                    s_alt_fifo_val[tail] = gps->smoothed_altitude_m;
                    s_spd_fifo_val[tail] = gps->speed_kmh;
                    s_alt_fifo_ts[tail]  = now_sec;
                    s_alt_fifo_count++;
                    s_alt_last_push_sec = now_sec;
                } else {
                    int newest = (s_alt_fifo_head + s_alt_fifo_count - 1) % ALT_FIFO_CAP;
                    s_alt_fifo_val[newest] = gps->smoothed_altitude_m;
                    s_spd_fifo_val[newest] = gps->speed_kmh;
                }

                if (!s_init_page_active && (s_current_page == 0 || s_drag_peer_page == 0)) {
                    s_p1_chart_dirty = false;
                    redraw_altitude_polyline();
                } else {
                    s_p1_chart_dirty = true;
                }

                if (!s_init_page_active && (s_current_page == 1 || s_drag_peer_page == 1)) {
                    s_p2_chart_dirty = false;
                    redraw_speed_polyline();
                } else {
                    s_p2_chart_dirty = true;
                }
            }
        } else {
            if (s_p1_visible || s_p1_last_alt_m != INT32_MIN) {
                s_p1_visible = false;
                s_p1_last_alt_m = INT32_MIN;
                lv_label_set_text(s_p1_value_label, "---");
                lv_obj_set_style_text_color(s_p1_value_label, COLOR_LABEL_DIM, 0);
                lv_obj_align(s_p1_value_label, LV_ALIGN_CENTER, 0, -104);

                lv_label_set_text(s_p1_unit_label, "SEARCHING");
                lv_obj_set_style_text_font(s_p1_unit_label, &lv_font_montserrat_12, 0);
                lv_obj_set_style_text_color(s_p1_unit_label, COLOR_LABEL_DIM, 0);
                lv_obj_set_style_text_letter_space(s_p1_unit_label, 2, 0);
                lv_obj_align(s_p1_unit_label, LV_ALIGN_CENTER, 0, -66);
            }
        }

        /* Bottom Section: Min Altitude (Left) & Max Altitude (Right) since power-on */
        if (gps->has_session_fix || gps->has_fix) {
            if (gps->min_altitude_int_m != s_p1_last_min_m) {
                s_p1_last_min_m = gps->min_altitude_int_m;
                char min_buf[24];
                snprintf(min_buf, sizeof(min_buf), "%ld m", (long)gps->min_altitude_int_m);
                lv_label_set_text(s_p1_min_label, min_buf);
                lv_obj_set_style_text_color(s_p1_min_label, COLOR_TEXT_WHITE, 0);
                lv_obj_align(s_p1_min_label, LV_ALIGN_CENTER, -70, 114);
            }
            if (gps->max_altitude_int_m != s_p1_last_max_m) {
                s_p1_last_max_m = gps->max_altitude_int_m;
                char max_buf[24];
                snprintf(max_buf, sizeof(max_buf), "%ld m", (long)gps->max_altitude_int_m);
                lv_label_set_text(s_p1_max_label, max_buf);
                lv_obj_set_style_text_color(s_p1_max_label, COLOR_SIGNAL_GREEN, 0);
                lv_obj_align(s_p1_max_label, LV_ALIGN_CENTER, 70, 114);
            }
        }
    }

    /* 5. Update Page 2: 3-Section Speed Dashboard */
    if (gps && s_p2_content_group) {
        /* Top Section: Current Speed (km/h) */
        if (gps->has_fix) {
            if (gps->speed_int_kmh != s_p2_last_spd_kmh || !s_p2_visible) {
                s_p2_last_spd_kmh = gps->speed_int_kmh;
                char buf[24];
                snprintf(buf, sizeof(buf), "%ld", (long)gps->speed_int_kmh);
                lv_label_set_text(s_p2_value_label, buf);
                lv_obj_set_style_text_color(s_p2_value_label, COLOR_TEXT_WHITE, 0);
                lv_obj_align(s_p2_value_label, LV_ALIGN_CENTER, -24, -100);

                lv_label_set_text(s_p2_unit_label, "km/h");
                lv_obj_set_style_text_font(s_p2_unit_label, &lv_font_montserrat_20, 0);
                lv_obj_set_style_text_color(s_p2_unit_label, COLOR_SIGNAL_GREEN, 0);
                lv_obj_set_style_text_letter_space(s_p2_unit_label, 0, 0);
                lv_obj_align_to(s_p2_unit_label, s_p2_value_label, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -6);
            }
            s_p2_visible = true;
        } else {
            if (s_p2_visible || s_p2_last_spd_kmh != INT32_MIN) {
                s_p2_visible = false;
                s_p2_last_spd_kmh = INT32_MIN;
                lv_label_set_text(s_p2_value_label, "---");
                lv_obj_set_style_text_color(s_p2_value_label, COLOR_LABEL_DIM, 0);
                lv_obj_align(s_p2_value_label, LV_ALIGN_CENTER, 0, -104);

                lv_label_set_text(s_p2_unit_label, "SEARCHING");
                lv_obj_set_style_text_font(s_p2_unit_label, &lv_font_montserrat_12, 0);
                lv_obj_set_style_text_color(s_p2_unit_label, COLOR_LABEL_DIM, 0);
                lv_obj_set_style_text_letter_space(s_p2_unit_label, 2, 0);
                lv_obj_align(s_p2_unit_label, LV_ALIGN_CENTER, 0, -66);
            }
        }

        /* Bottom Section: Only Max Speed Since Power-On */
        if (gps->has_session_fix || gps->has_fix) {
            if (gps->max_speed_int_kmh != s_p2_last_max_kmh) {
                s_p2_last_max_kmh = gps->max_speed_int_kmh;
                char max_buf[24];
                snprintf(max_buf, sizeof(max_buf), "%ld km/h", (long)gps->max_speed_int_kmh);
                lv_label_set_text(s_p2_max_label, max_buf);
                lv_obj_set_style_text_color(s_p2_max_label, COLOR_SIGNAL_GREEN, 0);
                lv_obj_align(s_p2_max_label, LV_ALIGN_CENTER, 0, 114);
            }
        }
    }
}

lv_obj_t *ui_framework_get_page_safe_container(int page_index)
{
    if (page_index < 0 || page_index >= UI_PAGE_COUNT) {
        return NULL;
    }
    return s_page_safe_conts[page_index];
}

void ui_framework_set_tf_format_cb(ui_tool_action_cb_t cb)
{
    s_tf_format_cb = cb;
}

void ui_framework_set_tf_status(ui_tf_status_t status)
{
    s_tf_status = status;
    update_p3_tile_visual(0, s_p3_pressed_tile == 0);
}

void ui_framework_set_wifi_portal_cb(ui_wifi_portal_ctrl_cb_t cb)
{
    s_wifi_portal_cb = cb;
}

void ui_framework_set_wifi_qr_stage(ui_wifi_qr_stage_t stage, bool close_modal)
{
    if (close_modal) {
        if (s_p3_qr_modal_open) {
            s_p3_qr_modal_open = false;
            s_p3_qr_close_pressed = false;
            update_p3_qr_close_visual(false);
            if (s_p3_qr_modal) {
                lv_obj_add_flag(s_p3_qr_modal, LV_OBJ_FLAG_HIDDEN);
            }
            if (s_p3_content_group) {
                lv_obj_remove_flag(s_p3_content_group, LV_OBJ_FLAG_HIDDEN);
            }
        }
        return;
    }

    if (!s_p3_qr_modal || !s_p3_qrcode || !s_p3_qr_step_label || !s_p3_qr_hint_label) {
        return;
    }

    if (!s_p3_qr_modal_open) {
        s_p3_qr_modal_open = true;
        s_p3_qr_close_pressed = false;
        update_p3_qr_close_visual(false);
        if (s_p3_content_group) {
            lv_obj_add_flag(s_p3_content_group, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_remove_flag(s_p3_qr_modal, LV_OBJ_FLAG_HIDDEN);
    }

    switch (stage) {
    case UI_WIFI_QR_STAGE_JOIN_AP:
        lv_label_set_text(s_p3_qr_step_label, "1. SCAN TO JOIN AP");
        lv_obj_set_style_text_color(s_p3_qr_step_label, COLOR_SIGNAL_GREEN, 0);
        lv_qrcode_update(s_p3_qrcode, WIFI_AP_QR_PAYLOAD, strlen(WIFI_AP_QR_PAYLOAD));
        lv_label_set_text(s_p3_qr_hint_label, "AP: ESP32-WIFI-CFG");
        lv_obj_set_style_text_color(s_p3_qr_hint_label, COLOR_TEXT_WHITE, 0);
        break;

    case UI_WIFI_QR_STAGE_OPEN_URL:
        lv_label_set_text(s_p3_qr_step_label, "2. SCAN TO OPEN PAGE");
        lv_obj_set_style_text_color(s_p3_qr_step_label, lv_color_hex(0xFFB300), 0);
        lv_qrcode_update(s_p3_qrcode, WIFI_URL_QR_PAYLOAD, strlen(WIFI_URL_QR_PAYLOAD));
        lv_label_set_text(s_p3_qr_hint_label, "URL: 192.168.4.1");
        lv_obj_set_style_text_color(s_p3_qr_hint_label, COLOR_TEXT_WHITE, 0);
        break;

    case UI_WIFI_QR_STAGE_CONNECTING:
        lv_label_set_text(s_p3_qr_step_label, "CONNECTING WIFI...");
        lv_obj_set_style_text_color(s_p3_qr_step_label, lv_color_hex(0xFFB300), 0);
        lv_label_set_text(s_p3_qr_hint_label, "SAVED TO NVS FLASH");
        lv_obj_set_style_text_color(s_p3_qr_hint_label, lv_color_hex(0xFFB300), 0);
        break;

    case UI_WIFI_QR_STAGE_CONNECTED:
        lv_label_set_text(s_p3_qr_step_label, "WIFI CONNECTED!");
        lv_obj_set_style_text_color(s_p3_qr_step_label, COLOR_SIGNAL_GREEN, 0);
        lv_label_set_text(s_p3_qr_hint_label, "AUTO-CONNECT ENABLED");
        lv_obj_set_style_text_color(s_p3_qr_hint_label, COLOR_SIGNAL_GREEN, 0);
        break;
    }
}

void ui_framework_set_file_mgr_cb(ui_file_mgr_ctrl_cb_t cb)
{
    s_file_mgr_cb = cb;
}

void ui_framework_set_file_mgr_active(bool active)
{
    s_p3_tile_active[2] = active;
    update_p3_tile_visual(2, s_p3_pressed_tile == 2);
}

void ui_framework_set_track_log_cb(ui_track_log_ctrl_cb_t cb)
{
    s_track_log_cb = cb;
}

void ui_framework_set_track_log_state(bool enabled, ui_track_log_status_t status, uint32_t elapsed_sec)
{
    s_p3_tile_active[3] = enabled;
    s_track_log_status = status;
    s_track_log_elapsed_sec = elapsed_sec;
    update_p3_tile_visual(3, s_p3_pressed_tile == 3);
}


