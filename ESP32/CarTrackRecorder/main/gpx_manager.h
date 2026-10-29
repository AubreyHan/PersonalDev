#pragma once

#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化 GPX 管理器
esp_err_t gpx_manager_init(void);

// 启动记录 (分段模式 / 持续模式)
esp_err_t gpx_manager_start(record_mode_t mode);

// 停止记录并安全封存
esp_err_t gpx_manager_stop(void);

// 1Hz 输入轨迹点并写入
esp_err_t gpx_manager_feed_point(const gps_data_t *gps);

// FIFO 存储空间检查与循环淘汰最旧文件
esp_err_t gpx_manager_check_fifo(void);

#ifdef __cplusplus
}
#endif
