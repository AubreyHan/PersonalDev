#pragma once

#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// 切换 AP + HTTP Web 服务的开启/关闭状态
esp_err_t web_server_toggle(void);

// 强制启动 / 停止服务
esp_err_t web_server_start(void);
esp_err_t web_server_stop(void);

#ifdef __cplusplus
}
#endif
