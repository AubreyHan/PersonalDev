#pragma once

#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化 GNSS 引擎与后台轮询任务
esp_err_t gps_engine_init(void);

// 手动或按键触发录制控制
void gps_engine_request_record(record_mode_t mode);
void gps_engine_stop_record(void);

// 获取最新 GPS 数据的线程安全副本
void gps_get_latest_data(gps_data_t *out_data);

// 获取最新高度统计数据的线程安全副本
void gps_get_altitude_stats(altitude_stats_t *out_stats);

#ifdef __cplusplus
}
#endif
