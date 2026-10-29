#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "app_config.h"
#include "bsp_board.h"
#include "gps_engine.h"
#include "gpx_manager.h"
#include "web_server.h"
#include "wifi_mgr.h"
#include "ui_app.h"

static const char *TAG = "MAIN";

// 全局单例状态定义
app_state_t g_app_state = {
    .record_mode = REC_MODE_NONE,
    .record_status = REC_STATUS_IDLE,
    .map_view_mode = VIEW_HEADING_UP,
    .ap_web_running = false,
    .sd_mounted = false,
    .sd_total_bytes = 0,
    .sd_free_bytes = 0,
    .gps = {
        .valid_fix = false,
        .satellites = 0,
        .latitude = 0.0,
        .longitude = 0.0,
        .altitude = 0.0f,
        .speed_kmh = 0.0f,
        .heading_deg = 0.0f,
        .timezone_offset = 8,
        .hdop = 0.0f,
        .avg_snr = 0,
        .signal_bars = 0,
    },
    .alt_stats = {
        .current_alt = 0.0f,
        .max_alt = 0.0f,
        .min_alt = 0.0f,
        .history_count = 0,
        .history_head = 0,
    },
    .current_gpx_path = {0}
};

// =========================================================================
// Boot 按键事件路由 (单击分段 / 双击持续 / 再次按停止 / 长按开关AP)
// =========================================================================
static void on_boot_button_event(btn_event_t event)
{
    switch (event) {
        case BTN_EVENT_SINGLE_CLICK:
            ESP_LOGI(TAG, "Button: Single Click event received.");
            if (g_app_state.record_status == REC_STATUS_ACTIVE || g_app_state.record_status == REC_STATUS_ARMED) {
                // 再次按下 Boot 键: 停止记录并封存
                gps_engine_stop_record();
            } else {
                // 单击启动分段记录
                gps_engine_request_record(REC_MODE_SEGMENTED);
            }
            break;

        case BTN_EVENT_DOUBLE_CLICK:
            ESP_LOGI(TAG, "Button: Double Click event received.");
            if (g_app_state.record_status == REC_STATUS_ACTIVE || g_app_state.record_status == REC_STATUS_ARMED) {
                // 再次按下: 停止记录
                gps_engine_stop_record();
            } else {
                // 双击启动持续记录
                gps_engine_request_record(REC_MODE_CONTINUOUS);
            }
            break;

        case BTN_EVENT_LONG_PRESS:
            ESP_LOGI(TAG, "Button: Long Press event received.");
            // 长按开关 HTTP Web 导出服务 (内部按需锁定 display 显示 Toast)
            web_server_toggle();
            break;
    }
}

// =========================================================================
// UI 渲染与状态刷新任务 (独占运行在 Core 1)
// =========================================================================
static void ui_update_task(void *pvParameters)
{
    ESP_LOGI(TAG, "UI Update Task running on Core 1...");
    while (1) {
        bsp_display_lock(-1);
        ui_app_update();
        bsp_display_unlock();

        vTaskDelay(pdMS_TO_TICKS(250)); // 4 Hz 界面遥测刷新频率
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  ESP32-S3 AMOLED Car Track Recorder Starting...  ");
    ESP_LOGI(TAG, "==================================================");

    // 1. 初始化 NVS 存储
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. 初始化网络协议栈与事件循环
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 3. 启动官方 BSP 显示屏与 CST9217 触摸驱动
    bsp_display_start();

    // 4. 初始化板载外设 (SDMMC 1-bit, Boot 按键状态机)
    bsp_board_init();
    bsp_button_register_cb(on_boot_button_event);

    // 初始化 Wi-Fi 管理 (开机读取 NVS 自动重连)
    wifi_mgr_init();

    // 5. 初始化业务引擎 (GNSS 驱动、GPX 管理器)
    gps_engine_init();
    gpx_manager_init();

    // 6. 初始化 LVGL 9 UI
    bsp_display_lock(-1);
    ui_app_init();
    bsp_display_unlock();

    // 7. 在 Core 1 创建 UI 周期性刷新任务
    xTaskCreatePinnedToCore(ui_update_task, "ui_update_task", 4096, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "System initialization complete. Ready for driving track recording.");
}
