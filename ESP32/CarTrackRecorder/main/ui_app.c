#include "ui_app.h"
#include "ui_view_altitude.h"
#include "ui_view_track.h"
#include "gps_engine.h"
#include "wifi_mgr.h"
#include "src/libs/qrcode/lv_qrcode.h"
#include <stdio.h>
#include <time.h>

static lv_obj_t *s_tabview = NULL;
static lv_obj_t *s_status_bar = NULL;
static lv_obj_t *s_lbl_time = NULL;
static lv_obj_t *s_sig_bars[4] = {NULL};
static lv_obj_t *s_lbl_gps_status = NULL;
static lv_obj_t *s_lbl_rec_badge = NULL;
static lv_obj_t *s_lbl_wifi_badge = NULL;

static lv_obj_t *s_toast_box = NULL;
static lv_timer_t *s_toast_timer = NULL;
static lv_obj_t *s_qr_modal = NULL;

static void toast_timer_cb(lv_timer_t *timer)
{
    if (s_toast_box) {
        lv_obj_delete(s_toast_box);
        s_toast_box = NULL;
    }
    s_toast_timer = NULL;
}

void ui_show_toast(const char *title, const char *msg, uint32_t duration_ms)
{
    if (s_toast_timer) {
        lv_timer_delete(s_toast_timer);
        s_toast_timer = NULL;
    }
    if (s_toast_box) {
        lv_obj_delete(s_toast_box);
        s_toast_box = NULL;
    }

    s_toast_box = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_toast_box, 300, 110);
    lv_obj_align(s_toast_box, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_bg_color(s_toast_box, lv_color_hex(0x0f172a), 0);
    lv_obj_set_style_bg_opa(s_toast_box, LV_OPA_90, 0);
    lv_obj_set_style_border_color(s_toast_box, lv_color_hex(0x38bdf8), 0);
    lv_obj_set_style_border_width(s_toast_box, 2, 0);
    lv_obj_set_style_radius(s_toast_box, 16, 0);
    lv_obj_set_style_pad_all(s_toast_box, 10, 0);
    lv_obj_clear_flag(s_toast_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_t = lv_label_create(s_toast_box);
    lv_label_set_text(lbl_t, title);
    lv_obj_set_style_text_color(lbl_t, lv_color_hex(0x38bdf8), 0);
    lv_obj_set_style_text_font(lbl_t, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_t, LV_ALIGN_TOP_MID, 0, 5);

    lv_obj_t *lbl_m = lv_label_create(s_toast_box);
    lv_label_set_text(lbl_m, msg);
    lv_obj_set_style_text_color(lbl_m, lv_color_hex(0xf1f5f9), 0);
    lv_obj_set_style_text_font(lbl_m, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_m, LV_ALIGN_BOTTOM_MID, 0, -5);

    s_toast_timer = lv_timer_create(toast_timer_cb, duration_ms, NULL);
    lv_timer_set_repeat_count(s_toast_timer, 1);
}

// 点击二维码关闭按钮
static void close_qr_btn_cb(lv_event_t *e)
{
    ui_hide_wifi_qrcode();
    wifi_mgr_stop_provisioning();
}

// 弹出 Wi-Fi 扫码配网二维码
void ui_show_wifi_qrcode(const char *qr_data, const char *hint)
{
    if (s_qr_modal) {
        lv_obj_delete(s_qr_modal);
        s_qr_modal = NULL;
    }

    s_qr_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_qr_modal, 380, 380);
    lv_obj_center(s_qr_modal);
    lv_obj_set_style_bg_color(s_qr_modal, lv_color_hex(0x0f172a), 0);
    lv_obj_set_style_border_color(s_qr_modal, lv_color_hex(0x38bdf8), 0);
    lv_obj_set_style_border_width(s_qr_modal, 2, 0);
    lv_obj_set_style_radius(s_qr_modal, 24, 0);
    lv_obj_set_style_pad_all(s_qr_modal, 10, 0);
    lv_obj_clear_flag(s_qr_modal, LV_OBJ_FLAG_SCROLLABLE);

    // 标题
    lv_obj_t *lbl_title = lv_label_create(s_qr_modal);
    lv_label_set_text(lbl_title, "📶 手机扫码配网");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x38bdf8), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_18, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 8);

    // 关闭按钮 (右上角 X)
    lv_obj_t *btn_close = lv_button_create(s_qr_modal);
    lv_obj_set_size(btn_close, 32, 32);
    lv_obj_align(btn_close, LV_ALIGN_TOP_RIGHT, -5, 5);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x334155), 0);
    lv_obj_set_style_radius(btn_close, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(btn_close, close_qr_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, "✕");
    lv_obj_set_style_text_color(lbl_close, lv_color_hex(0xffffff), 0);
    lv_obj_center(lbl_close);

    // 二维码控件
    lv_obj_t *qr = lv_qrcode_create(s_qr_modal);
    lv_qrcode_set_size(qr, 180);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_set_data(qr, qr_data);
    lv_obj_align(qr, LV_ALIGN_CENTER, 0, -5);

    // 底部扫码说明
    lv_obj_t *lbl_hint = lv_label_create(s_qr_modal);
    lv_label_set_text(lbl_hint, hint);
    lv_obj_set_style_text_color(lbl_hint, lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_text_font(lbl_hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(lbl_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_hint, LV_ALIGN_BOTTOM_MID, 0, -10);
}

void ui_hide_wifi_qrcode(void)
{
    if (s_qr_modal) {
        lv_obj_delete(s_qr_modal);
        s_qr_modal = NULL;
    }
}

// 点击顶部 Wi-Fi 图标切换配网模式
static void wifi_badge_clicked_cb(lv_event_t *e)
{
    wifi_mgr_toggle_provisioning();
}

// 创建全局常驻状态栏 (顶部弧形区)
static void create_status_bar(void)
{
    s_status_bar = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_status_bar, LCD_WIDTH, 48);
    lv_obj_align(s_status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_status_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_status_bar, 0, 0);
    lv_obj_set_style_pad_all(s_status_bar, 0, 0);
    lv_obj_clear_flag(s_status_bar, LV_OBJ_FLAG_SCROLLABLE);

    // 1. 中间常驻时间 (GPS 授时 + RTC)
    s_lbl_time = lv_label_create(s_status_bar);
    lv_label_set_text(s_lbl_time, "--:--:--");
    lv_obj_set_style_text_color(s_lbl_time, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_lbl_time, &lv_font_montserrat_16, 0);
    lv_obj_align(s_lbl_time, LV_ALIGN_TOP_MID, 0, 16);

    // 2. 左侧 GPS 信号强度格与搜星状态
    lv_obj_t *sig_cont = lv_obj_create(s_status_bar);
    lv_obj_set_size(sig_cont, 22, 16);
    lv_obj_align(sig_cont, LV_ALIGN_TOP_LEFT, 45, 18);
    lv_obj_set_style_bg_opa(sig_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sig_cont, 0, 0);
    lv_obj_set_style_pad_all(sig_cont, 0, 0);
    lv_obj_clear_flag(sig_cont, LV_OBJ_FLAG_SCROLLABLE);

    const int bar_h[4] = {4, 7, 10, 13};
    for (int i = 0; i < 4; i++) {
        s_sig_bars[i] = lv_obj_create(sig_cont);
        lv_obj_set_size(s_sig_bars[i], 3, bar_h[i]);
        lv_obj_align(s_sig_bars[i], LV_ALIGN_BOTTOM_LEFT, i * 5, 0);
        lv_obj_set_style_bg_color(s_sig_bars[i], lv_color_hex(0x475569), 0);
        lv_obj_set_style_radius(s_sig_bars[i], 1, 0);
        lv_obj_set_style_border_width(s_sig_bars[i], 0, 0);
        lv_obj_clear_flag(s_sig_bars[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    s_lbl_gps_status = lv_label_create(s_status_bar);
    lv_label_set_text(s_lbl_gps_status, "搜星中");
    lv_obj_set_style_text_color(s_lbl_gps_status, lv_color_hex(0xf59e0b), 0); // 橙色搜星中
    lv_obj_set_style_text_font(s_lbl_gps_status, &lv_font_montserrat_12, 0);
    lv_obj_align(s_lbl_gps_status, LV_ALIGN_TOP_LEFT, 71, 18);

    // 3. 右侧 录制状态标识
    s_lbl_rec_badge = lv_label_create(s_status_bar);
    lv_label_set_text(s_lbl_rec_badge, "");
    lv_obj_set_style_text_font(s_lbl_rec_badge, &lv_font_montserrat_12, 0);
    lv_obj_align(s_lbl_rec_badge, LV_ALIGN_TOP_RIGHT, -60, 18);

    // 4. Wi-Fi 连接状态与配网入口图标 (点击即可开启/关闭扫码配网)
    s_lbl_wifi_badge = lv_label_create(s_status_bar);
    lv_label_set_text(s_lbl_wifi_badge, "📶配网");
    lv_obj_set_style_text_color(s_lbl_wifi_badge, lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_text_font(s_lbl_wifi_badge, &lv_font_montserrat_12, 0);
    lv_obj_align(s_lbl_wifi_badge, LV_ALIGN_TOP_RIGHT, -125, 18);
    lv_obj_add_flag(s_lbl_wifi_badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_lbl_wifi_badge, wifi_badge_clicked_cb, LV_EVENT_CLICKED, NULL);
}

void ui_app_update(void)
{
    // 1. 刷新时间 (由 GPS 自动判定时区并同步系统 RTC)
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    lv_label_set_text(s_lbl_time, time_str);

    // 2. 刷新 GPS 状态与信号格
    gps_data_t gps;
    gps_get_latest_data(&gps);

    // 确定信号格激活颜色
    lv_color_t active_color;
    if (gps.signal_bars >= 3) {
        active_color = lv_color_hex(0x22c55e); // 绿色 优良 (>=3格)
    } else if (gps.signal_bars == 2) {
        active_color = lv_color_hex(0x38bdf8); // 蓝色 中等 (2格)
    } else if (gps.signal_bars == 1) {
        active_color = lv_color_hex(0xf59e0b); // 橙黄 较弱 (1格)
    } else {
        active_color = lv_color_hex(0xef4444); // 红色 无信号 (0格)
    }

    for (int i = 0; i < 4; i++) {
        if (s_sig_bars[i]) {
            if (i < gps.signal_bars) {
                lv_obj_set_style_bg_color(s_sig_bars[i], active_color, 0);
            } else {
                lv_obj_set_style_bg_color(s_sig_bars[i], lv_color_hex(0x334155), 0); // 暗灰熄灭
            }
        }
    }

    if (gps.valid_fix) {
        char buf[64];
        if (gps.avg_snr > 0) {
            snprintf(buf, sizeof(buf), "%d星 %ddB", gps.satellites, gps.avg_snr);
        } else {
            snprintf(buf, sizeof(buf), "%d星", gps.satellites);
        }
        lv_label_set_text(s_lbl_gps_status, buf);
        lv_obj_set_style_text_color(s_lbl_gps_status, active_color, 0);
    } else {
        if (gps.satellites > 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), "搜星:%d", gps.satellites);
            lv_label_set_text(s_lbl_gps_status, buf);
        } else {
            lv_label_set_text(s_lbl_gps_status, "搜星中");
        }
        lv_obj_set_style_text_color(s_lbl_gps_status, lv_color_hex(0xf59e0b), 0); // 橙色搜星
    }

    // 3. 刷新录制状态标记
    if (g_app_state.record_status == REC_STATUS_ACTIVE) {
        if (g_app_state.record_mode == REC_MODE_SEGMENTED) {
            lv_label_set_text(s_lbl_rec_badge, "● REC[段]");
        } else {
            lv_label_set_text(s_lbl_rec_badge, "● REC[续]");
        }
        lv_obj_set_style_text_color(s_lbl_rec_badge, lv_color_hex(0xef4444), 0); // 红色
    } else if (g_app_state.record_status == REC_STATUS_ARMED) {
        lv_label_set_text(s_lbl_rec_badge, "● 等待定位");
        lv_obj_set_style_text_color(s_lbl_rec_badge, lv_color_hex(0xf59e0b), 0); // 橙色待命
    } else {
        lv_label_set_text(s_lbl_rec_badge, "");
    }

    // 4. 刷新 Wi-Fi 状态图标 (未连接 / 连接中 / 已连接 / 扫码配网中)
    if (g_app_state.wifi_state == WIFI_STATE_CONNECTED) {
        lv_label_set_text(s_lbl_wifi_badge, "📶已连");
        lv_obj_set_style_text_color(s_lbl_wifi_badge, lv_color_hex(0x22c55e), 0); // 绿色常亮
    } else if (g_app_state.wifi_state == WIFI_STATE_CONNECTING) {
        lv_label_set_text(s_lbl_wifi_badge, "📶连中");
        lv_obj_set_style_text_color(s_lbl_wifi_badge, lv_color_hex(0x38bdf8), 0); // 蓝色
    } else if (g_app_state.wifi_state == WIFI_STATE_PROVISIONING) {
        lv_label_set_text(s_lbl_wifi_badge, "📶配网中");
        lv_obj_set_style_text_color(s_lbl_wifi_badge, lv_color_hex(0xf59e0b), 0); // 橙色闪烁感
    } else {
        lv_label_set_text(s_lbl_wifi_badge, "📶配网");
        lv_obj_set_style_text_color(s_lbl_wifi_badge, lv_color_hex(0x94a3b8), 0); // 灰色可点
    }

    // 5. 刷新高度界面与轨迹界面
    altitude_stats_t alt_stats;
    gps_get_altitude_stats(&alt_stats);
    ui_view_altitude_update(&alt_stats);
    ui_view_track_update(&gps);
}

esp_err_t ui_app_init(void)
{
    // 创建支持水平左右滑动手势的 TabView
    s_tabview = lv_tabview_create(lv_screen_active());
    lv_tabview_set_tab_bar_position(s_tabview, LV_DIR_NONE); // 隐藏顶部标签条，由用户手势滑动
    lv_obj_set_size(s_tabview, LCD_WIDTH, LCD_HEIGHT);

    lv_obj_t *tab_track = lv_tabview_add_tab(s_tabview, "Track");
    lv_obj_t *tab_alt   = lv_tabview_add_tab(s_tabview, "Altitude");

    // 默认首页: 轨迹界面
    ui_view_track_create(tab_track);
    // 次页: 高度统计界面
    ui_view_altitude_create(tab_alt);

    // 挂载顶层常驻状态栏
    create_status_bar();

    return ESP_OK;
}
