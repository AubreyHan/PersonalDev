#pragma once

#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTN_EVENT_SINGLE_CLICK = 1,
    BTN_EVENT_DOUBLE_CLICK,
    BTN_EVENT_LONG_PRESS,
} btn_event_t;

typedef void (*btn_event_cb_t)(btn_event_t event);

// 初始化板级基础外设 (PMIC、SDMMC、Boot按键检测)
esp_err_t bsp_board_init(void);

// 初始化并挂载 MicroSD 卡目录
esp_err_t app_sdcard_init(void);
esp_err_t bsp_sdcard_update_space(void);

// 注册 Boot 按键多态回调
void bsp_button_register_cb(btn_event_cb_t cb);

#ifdef __cplusplus
}
#endif
