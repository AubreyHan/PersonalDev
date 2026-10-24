#pragma once

#include "app_config.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化 LVGL 9 UI 系统与界面编排
esp_err_t ui_app_init(void);

// 显示屏幕 Toast 弹窗提示 (带自动淡出定时器)
void ui_show_toast(const char *title, const char *msg, uint32_t duration_ms);

// 显示 / 关闭 Wi-Fi 扫码配网二维码全屏浮层
void ui_show_wifi_qrcode(const char *qr_data, const char *hint);
void ui_hide_wifi_qrcode(void);

// 界面周期刷新回调 (定时更新时间、状态栏、高度及轨迹)
void ui_app_update(void);

#ifdef __cplusplus
}
#endif
