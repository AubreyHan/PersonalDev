#include "ui_view_altitude.h"
#include <stdio.h>

static lv_obj_t *s_lbl_current_alt = NULL;
static lv_obj_t *s_lbl_max_alt = NULL;
static lv_obj_t *s_lbl_min_alt = NULL;
static lv_obj_t *s_chart = NULL;
static lv_chart_series_t *s_ser = NULL;

lv_obj_t *ui_view_altitude_create(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x0f172a), 0); // 深空蓝黑底色
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    // 标题
    lv_obj_t *lbl_title = lv_label_create(cont);
    lv_label_set_text(lbl_title, "海拔高度 (ALTITUDE)");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 65);

    // 实时海拔数值 (大号字体)
    s_lbl_current_alt = lv_label_create(cont);
    lv_label_set_text(s_lbl_current_alt, "-- m");
    lv_obj_set_style_text_color(s_lbl_current_alt, lv_color_hex(0x38bdf8), 0); // 天蓝色
    lv_obj_set_style_text_font(s_lbl_current_alt, &lv_font_montserrat_40, 0);
    lv_obj_align(s_lbl_current_alt, LV_ALIGN_TOP_MID, 0, 95);

    // 极值卡片容器 (水平并列)
    lv_obj_t *stats_box = lv_obj_create(cont);
    lv_obj_set_size(stats_box, 360, 50);
    lv_obj_align(stats_box, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_set_style_bg_color(stats_box, lv_color_hex(0x1e293b), 0);
    lv_obj_set_style_border_width(stats_box, 0, 0);
    lv_obj_set_style_radius(stats_box, 10, 0);
    lv_obj_set_style_pad_all(stats_box, 5, 0);
    lv_obj_clear_flag(stats_box, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_max_alt = lv_label_create(stats_box);
    lv_label_set_text(s_lbl_max_alt, "最高: -- m");
    lv_obj_set_style_text_color(s_lbl_max_alt, lv_color_hex(0x4ade80), 0); // 绿色
    lv_obj_set_style_text_font(s_lbl_max_alt, &lv_font_montserrat_16, 0);
    lv_obj_align(s_lbl_max_alt, LV_ALIGN_LEFT_MID, 20, 0);

    s_lbl_min_alt = lv_label_create(stats_box);
    lv_label_set_text(s_lbl_min_alt, "最低: -- m");
    lv_obj_set_style_text_color(s_lbl_min_alt, lv_color_hex(0xf87171), 0); // 红色
    lv_obj_set_style_text_font(s_lbl_min_alt, &lv_font_montserrat_16, 0);
    lv_obj_align(s_lbl_min_alt, LV_ALIGN_RIGHT_MID, -20, 0);

    // 海拔变化走势折线图 (Chart)
    s_chart = lv_chart_create(cont);
    lv_obj_set_size(s_chart, 360, 170);
    lv_obj_align(s_chart, LV_ALIGN_TOP_MID, 0, 225);
    lv_obj_set_style_bg_color(s_chart, lv_color_hex(0x1e293b), 0);
    lv_obj_set_style_border_color(s_chart, lv_color_hex(0x334155), 0);
    lv_obj_set_style_radius(s_chart, 12, 0);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, ALT_HISTORY_POINTS);
    lv_obj_set_style_size(s_chart, 0, 0, LV_PART_INDICATOR); // 隐藏单独数据点圆圈，仅保留平滑线

    s_ser = lv_chart_add_series(s_chart, lv_color_hex(0x38bdf8), LV_CHART_AXIS_PRIMARY_Y);

    // 默认空数据初始化
    for (int i = 0; i < ALT_HISTORY_POINTS; i++) {
        lv_chart_set_next_value(s_chart, s_ser, 0);
    }

    // 底部走势说明
    lv_obj_t *lbl_chart_hint = lv_label_create(cont);
    lv_label_set_text(lbl_chart_hint, "最近行程海拔变化趋势 (每5秒更新)");
    lv_obj_set_style_text_color(lbl_chart_hint, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_text_font(lbl_chart_hint, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_chart_hint, LV_ALIGN_BOTTOM_MID, 0, -35);

    return cont;
}

void ui_view_altitude_update(const altitude_stats_t *stats)
{
    if (!s_lbl_current_alt || !stats) return;

    if (stats->history_count > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f m", stats->current_alt);
        lv_label_set_text(s_lbl_current_alt, buf);

        snprintf(buf, sizeof(buf), "最高: %.0f m", stats->max_alt);
        lv_label_set_text(s_lbl_max_alt, buf);

        snprintf(buf, sizeof(buf), "最低: %.0f m", stats->min_alt);
        lv_label_set_text(s_lbl_min_alt, buf);

        // 动态调整图表 Y 轴范围 (留出 10% 缓冲边距)
        int y_min = (int)(stats->min_alt - 10.0f);
        int y_max = (int)(stats->max_alt + 10.0f);
        if (y_max - y_min < 20) y_max = y_min + 20;
        lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);

        // 重新灌入当前环形队列的折线数据
        int32_t *y_points = lv_chart_get_series_y_array(s_chart, s_ser);
        if (y_points) {
            for (int i = 0; i < stats->history_count; i++) {
                int idx = (stats->history_head - stats->history_count + i + ALT_HISTORY_POINTS) % ALT_HISTORY_POINTS;
                y_points[i] = (int32_t)stats->history[idx];
            }
        }
        lv_chart_refresh(s_chart);
    }
}
