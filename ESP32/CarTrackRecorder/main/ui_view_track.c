#include "ui_view_track.h"
#include "tile_engine.h"
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TRAIL_MAX_POINTS 128

typedef struct {
    double lat;
    double lon;
} trail_point_t;

static trail_point_t s_trail[TRAIL_MAX_POINTS];
static int s_trail_count = 0;

static lv_obj_t *s_cont = NULL;
static lv_obj_t *s_lbl_speed = NULL;
static lv_obj_t *s_lbl_zoom = NULL;
static lv_obj_t *s_lbl_compass = NULL;
static lv_obj_t *s_lbl_gps_detail = NULL;
static lv_obj_t *s_btn_view_mode = NULL;
static lv_obj_t *s_lbl_view_mode = NULL;
static lv_obj_t *s_car_arrow = NULL;
static lv_obj_t *s_trail_line = NULL;
static lv_point_precise_t s_line_points[TRAIL_MAX_POINTS];

static void view_mode_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (g_app_state.map_view_mode == VIEW_HEADING_UP) {
            g_app_state.map_view_mode = VIEW_NORTH_UP;
            lv_label_set_text(s_lbl_view_mode, "正北朝上");
        } else {
            g_app_state.map_view_mode = VIEW_HEADING_UP;
            lv_label_set_text(s_lbl_view_mode, "航向朝上");
        }
    }
}

lv_obj_t *ui_view_track_create(lv_obj_t *parent)
{
    s_cont = lv_obj_create(parent);
    lv_obj_set_size(s_cont, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_color(s_cont, lv_color_hex(0x090d16), 0); // 极深雷达黑
    lv_obj_set_style_border_width(s_cont, 0, 0);
    lv_obj_set_style_pad_all(s_cont, 0, 0);
    lv_obj_clear_flag(s_cont, LV_OBJ_FLAG_SCROLLABLE);

    // 绘制雷达同心圆标尺底纹 (半径 80, 150)
    lv_obj_t *circle1 = lv_obj_create(s_cont);
    lv_obj_set_size(circle1, 160, 160);
    lv_obj_center(circle1);
    lv_obj_set_style_radius(circle1, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(circle1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(circle1, lv_color_hex(0x1e293b), 0);
    lv_obj_set_style_border_width(circle1, 1, 0);
    lv_obj_clear_flag(circle1, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *circle2 = lv_obj_create(s_cont);
    lv_obj_set_size(circle2, 300, 300);
    lv_obj_center(circle2);
    lv_obj_set_style_radius(circle2, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(circle2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(circle2, lv_color_hex(0x1e293b), 0);
    lv_obj_set_style_border_width(circle2, 1, 0);
    lv_obj_clear_flag(circle2, LV_OBJ_FLAG_SCROLLABLE);

    // 轨迹绘制线 (Line)
    s_trail_line = lv_line_create(s_cont);
    lv_obj_set_style_line_color(s_trail_line, lv_color_hex(0x10b981), 0); // 翡翠绿航迹
    lv_obj_set_style_line_width(s_trail_line, 4, 0);
    lv_obj_set_style_line_rounded(s_trail_line, true, 0);

    // 屏幕中央自车点 (466x466 中心: 233, 233)
    s_car_arrow = lv_obj_create(s_cont);
    lv_obj_set_size(s_car_arrow, 22, 22);
    lv_obj_center(s_car_arrow);
    lv_obj_set_style_radius(s_car_arrow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_car_arrow, lv_color_hex(0x38bdf8), 0); // 天蓝
    lv_obj_set_style_border_color(s_car_arrow, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_width(s_car_arrow, 3, 0);
    lv_obj_clear_flag(s_car_arrow, LV_OBJ_FLAG_SCROLLABLE);

    // 视角模式切换触摸按钮 (右上角)
    s_btn_view_mode = lv_button_create(s_cont);
    lv_obj_set_size(s_btn_view_mode, 96, 32);
    lv_obj_align(s_btn_view_mode, LV_ALIGN_TOP_RIGHT, -45, 60);
    lv_obj_set_style_bg_color(s_btn_view_mode, lv_color_hex(0x1e293b), 0);
    lv_obj_set_style_radius(s_btn_view_mode, 16, 0);
    lv_obj_add_event_cb(s_btn_view_mode, view_mode_btn_event_cb, LV_EVENT_CLICKED, NULL);

    s_lbl_view_mode = lv_label_create(s_btn_view_mode);
    lv_label_set_text(s_lbl_view_mode, "航向朝上");
    lv_obj_set_style_text_color(s_lbl_view_mode, lv_color_hex(0xe2e8f0), 0);
    lv_obj_set_style_text_font(s_lbl_view_mode, &lv_font_montserrat_12, 0);
    lv_obj_center(s_lbl_view_mode);

    // 指南针罗盘显示 (左上角)
    s_lbl_compass = lv_label_create(s_cont);
    lv_label_set_text(s_lbl_compass, "🧭 N 0°");
    lv_obj_set_style_text_color(s_lbl_compass, lv_color_hex(0xf59e0b), 0); // 琥珀黄
    lv_obj_set_style_text_font(s_lbl_compass, &lv_font_montserrat_14, 0);
    lv_obj_align(s_lbl_compass, LV_ALIGN_TOP_LEFT, 50, 65);

    // GPS 信号详细遥测 (罗盘下方: 搜星数、SNR载噪比、HDOP精度因子)
    s_lbl_gps_detail = lv_label_create(s_cont);
    lv_label_set_text(s_lbl_gps_detail, "🛰️ 搜星中...");
    lv_obj_set_style_text_color(s_lbl_gps_detail, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_text_font(s_lbl_gps_detail, &lv_font_montserrat_12, 0);
    lv_obj_align(s_lbl_gps_detail, LV_ALIGN_TOP_LEFT, 50, 90);

    // 底部速度展示
    s_lbl_speed = lv_label_create(s_cont);
    lv_label_set_text(s_lbl_speed, "0.0 km/h");
    lv_obj_set_style_text_color(s_lbl_speed, lv_color_hex(0xf8fafc), 0);
    lv_obj_set_style_text_font(s_lbl_speed, &lv_font_montserrat_28, 0);
    lv_obj_align(s_lbl_speed, LV_ALIGN_BOTTOM_MID, 0, -55);

    // 缩放级别显示
    s_lbl_zoom = lv_label_create(s_cont);
    lv_label_set_text(s_lbl_zoom, "自动缩放: Z16");
    lv_obj_set_style_text_color(s_lbl_zoom, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_text_font(s_lbl_zoom, &lv_font_montserrat_12, 0);
    lv_obj_align(s_lbl_zoom, LV_ALIGN_BOTTOM_MID, 0, -35);

    return s_cont;
}

void ui_view_track_update(const gps_data_t *gps)
{
    if (!s_cont || !gps) return;

    // 1. 刷新速度与自适应缩放
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f km/h", gps->speed_kmh);
    lv_label_set_text(s_lbl_speed, buf);

    int zoom = tile_engine_calc_auto_zoom(gps->speed_kmh);
    snprintf(buf, sizeof(buf), "自动缩放: Z%d", zoom);
    lv_label_set_text(s_lbl_zoom, buf);

    // 2. 刷新指南针指示 (8方位角) 与 GPS 信号遥测
    const char *dir_names[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int dir_idx = (int)((gps->heading_deg + 22.5f) / 45.0f) % 8;
    snprintf(buf, sizeof(buf), "🧭 %s %.0f°", dir_names[dir_idx], gps->heading_deg);
    lv_label_set_text(s_lbl_compass, buf);

    if (s_lbl_gps_detail) {
        char sig_buf[64];
        if (gps->valid_fix) {
            snprintf(sig_buf, sizeof(sig_buf), "🛰️ %d星 %ddB H:%.1f",
                     gps->satellites, gps->avg_snr, gps->hdop);
            lv_label_set_text(s_lbl_gps_detail, sig_buf);
            lv_obj_set_style_text_color(s_lbl_gps_detail, lv_color_hex(0x38bdf8), 0); // 蓝色
        } else {
            if (gps->satellites > 0) {
                snprintf(sig_buf, sizeof(sig_buf), "🛰️ 搜星:%d (待定位)", gps->satellites);
            } else {
                snprintf(sig_buf, sizeof(sig_buf), "🛰️ 搜星中...");
            }
            lv_label_set_text(s_lbl_gps_detail, sig_buf);
            lv_obj_set_style_text_color(s_lbl_gps_detail, lv_color_hex(0xf59e0b), 0); // 橙色搜星
        }
    }

    // 3. 记录航迹点
    if (gps->valid_fix) {
        if (s_trail_count == 0) {
            s_trail[0].lat = gps->latitude;
            s_trail[0].lon = gps->longitude;
            s_trail_count = 1;
        } else {
            // 距离上一个点有移动时才追加
            double dlat = gps->latitude - s_trail[s_trail_count - 1].lat;
            double dlon = gps->longitude - s_trail[s_trail_count - 1].lon;
            if (fabs(dlat) > 0.00002 || fabs(dlon) > 0.00002) {
                if (s_trail_count < TRAIL_MAX_POINTS) {
                    s_trail[s_trail_count].lat = gps->latitude;
                    s_trail[s_trail_count].lon = gps->longitude;
                    s_trail_count++;
                } else {
                    // 循环左移覆盖
                    memmove(&s_trail[0], &s_trail[1], sizeof(trail_point_t) * (TRAIL_MAX_POINTS - 1));
                    s_trail[TRAIL_MAX_POINTS - 1].lat = gps->latitude;
                    s_trail[TRAIL_MAX_POINTS - 1].lon = gps->longitude;
                }
            }
        }
    }

    // 4. 计算航迹点相对于屏幕中心 (233, 233) 的像素投影
    const int cx = LCD_WIDTH / 2;
    const int cy = LCD_HEIGHT / 2;

    // 根据当前 Zoom 级别的米/像素换算比率
    double meters_per_pixel = 156543.03392 * cos(gps->latitude * M_PI / 180.0) / pow(2.0, zoom);
    if (meters_per_pixel <= 0.001) meters_per_pixel = 1.0;

    double heading_rad = 0.0;
    if (g_app_state.map_view_mode == VIEW_HEADING_UP) {
        // 航向朝上: 轨迹整体逆旋转 heading 角度
        heading_rad = -gps->heading_deg * M_PI / 180.0;
    }

    int valid_points = 0;
    for (int i = 0; i < s_trail_count; i++) {
        // 经纬度差转为地面大致偏移米数 (1度约 111320 米)
        double dx_meters = (s_trail[i].lon - gps->longitude) * 111320.0 * cos(gps->latitude * M_PI / 180.0);
        double dy_meters = (s_trail[i].lat - gps->latitude) * 111320.0;

        // 转为像素偏移 (屏幕 Y 轴向下，因此减去 dy)
        double px = dx_meters / meters_per_pixel;
        double py = -dy_meters / meters_per_pixel;

        // 若为航向朝上，应用二维旋转变换
        if (g_app_state.map_view_mode == VIEW_HEADING_UP) {
            double rx = px * cos(heading_rad) - py * sin(heading_rad);
            double ry = px * sin(heading_rad) + py * cos(heading_rad);
            px = rx;
            py = ry;
        }

        s_line_points[valid_points].x = (lv_coord_t)(cx + px);
        s_line_points[valid_points].y = (lv_coord_t)(cy + py);
        valid_points++;
    }

    if (valid_points > 1) {
        lv_line_set_points(s_trail_line, s_line_points, valid_points);
    }
}
