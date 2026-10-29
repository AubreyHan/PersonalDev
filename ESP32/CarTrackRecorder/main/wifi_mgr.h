#pragma once

#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化 Wi-Fi 管理器 (开机读取 NVS 自动重连)
esp_err_t wifi_mgr_init(void);

// 开启扫码配网模式 (弹出屏幕二维码并开启配网热点)
esp_err_t wifi_mgr_start_provisioning(void);

// 关闭扫码配网模式
esp_err_t wifi_mgr_stop_provisioning(void);

// 切换配网模式开关
esp_err_t wifi_mgr_toggle_provisioning(void);

// 保存 Wi-Fi 凭证到 NVS 并立即发起连接
esp_err_t wifi_mgr_save_and_connect(const char *ssid, const char *password);

// 断开 Wi-Fi
esp_err_t wifi_mgr_disconnect(void);

#ifdef __cplusplus
}
#endif
