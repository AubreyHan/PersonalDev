#include "bsp_board.h"
#include "bsp/esp-bsp.h"
#include <sys/stat.h>
#include <dirent.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

static const char *TAG = "BSP_BOARD";
static btn_event_cb_t s_btn_cb = NULL;

esp_err_t bsp_sdcard_update_space(void)
{
    if (!g_app_state.sd_mounted) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = esp_vfs_fat_info(SD_MOUNT_POINT, &g_app_state.sd_total_bytes, &g_app_state.sd_free_bytes);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "SD Free: %llu MB / %llu MB", 
                 g_app_state.sd_free_bytes / (1024 * 1024), 
                 g_app_state.sd_total_bytes / (1024 * 1024));
    }
    return ret;
}

esp_err_t app_sdcard_init(void)
{
    ESP_LOGI(TAG, "Mounting SDMMC via BSP in 1-bit mode...");
    esp_err_t ret = bsp_sdcard_mount();
    if (ret == ESP_OK) {
        g_app_state.sd_mounted = true;
        ESP_LOGI(TAG, "SD Card mounted successfully at %s", SD_MOUNT_POINT);

        // 创建专属目录
        mkdir(GPX_STORAGE_DIR, 0777);
        mkdir(MAP_STORAGE_DIR, 0777);

        bsp_sdcard_update_space();
    } else {
        g_app_state.sd_mounted = false;
        ESP_LOGW(TAG, "Failed to mount SD card (%s). Offline features disabled.", esp_err_to_name(ret));
    }
    return ret;
}

// =========================================================================
// Boot 按键多态状态机监测任务 (单击 / 双击 / 长按)
// =========================================================================
static void button_task(void *pvParameters)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOARD_BOOT_BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    typedef enum {
        BTN_IDLE,
        BTN_FIRST_PRESS,
        BTN_FIRST_RELEASE_WAIT,
        BTN_SECOND_PRESS,
        BTN_LONG_PRESS_WAIT
    } btn_state_t;

    btn_state_t state = BTN_IDLE;
    uint32_t press_start_ms = 0;
    uint32_t release_start_ms = 0;

    const uint32_t LONG_PRESS_THRESHOLD_MS = 1800; // 长按 > 1.8 秒
    const uint32_t DOUBLE_CLICK_GAP_MS     = 350;  // 双击间隔 350ms

    while (1) {
        int level = gpio_get_level(BOARD_BOOT_BTN_GPIO);
        bool is_pressed = (level == 0); // 低电平有效
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        switch (state) {
            case BTN_IDLE:
                if (is_pressed) {
                    press_start_ms = now;
                    state = BTN_FIRST_PRESS;
                }
                break;

            case BTN_FIRST_PRESS:
                if (!is_pressed) {
                    // 短暂释放，开始等待可能出现的第二次按下
                    release_start_ms = now;
                    state = BTN_FIRST_RELEASE_WAIT;
                } else if ((now - press_start_ms) >= LONG_PRESS_THRESHOLD_MS) {
                    // 超过长按阈值，触发长按
                    ESP_LOGI(TAG, "Boot button event: LONG PRESS");
                    if (s_btn_cb) s_btn_cb(BTN_EVENT_LONG_PRESS);
                    state = BTN_LONG_PRESS_WAIT;
                }
                break;

            case BTN_LONG_PRESS_WAIT:
                if (!is_pressed) {
                    state = BTN_IDLE;
                }
                break;

            case BTN_FIRST_RELEASE_WAIT:
                if (is_pressed) {
                    // 在等待窗口内按下了第二次
                    state = BTN_SECOND_PRESS;
                } else if ((now - release_start_ms) > DOUBLE_CLICK_GAP_MS) {
                    // 超时未按下第二次，判定为单击！
                    ESP_LOGI(TAG, "Boot button event: SINGLE CLICK");
                    if (s_btn_cb) s_btn_cb(BTN_EVENT_SINGLE_CLICK);
                    state = BTN_IDLE;
                }
                break;

            case BTN_SECOND_PRESS:
                if (!is_pressed) {
                    // 第二次释放，触发双击！
                    ESP_LOGI(TAG, "Boot button event: DOUBLE CLICK");
                    if (s_btn_cb) s_btn_cb(BTN_EVENT_DOUBLE_CLICK);
                    state = BTN_IDLE;
                }
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

void bsp_button_register_cb(btn_event_cb_t cb)
{
    s_btn_cb = cb;
}

esp_err_t bsp_board_init(void)
{
    ESP_LOGI(TAG, "Initializing Board Hardware...");

    // 挂载 SD 卡
    app_sdcard_init();

    // 启动按键监测任务 (优先级 5, 堆栈 3072 字节)
    xTaskCreatePinnedToCore(button_task, "button_task", 3072, NULL, 5, NULL, 0);

    return ESP_OK;
}
